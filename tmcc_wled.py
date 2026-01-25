"""TMCC accessory -> WLED bridge (ESP32 at 192.168.0.10 by default).

Listens for TMCC Accessory/Group packets (cmd_type 0b11) and maps address +
data fields to WLED actions via the WLED JSON HTTP API.

Quick use:

    from tmcc_wled import WLEDController

    controller = WLEDController(
        host="192.168.0.10",
        mapping={
            (50, 0x01): "on",        # ACC 50 keypad 1 -> lights on
            (50, 0x02): "off",       # ACC 50 keypad 2 -> lights off
            (50, 0x03): "cycle",     # ACC 50 keypad 3 -> next preset
        },
        pattern_presets=[1, 2, 3],     # optional list of WLED preset IDs to cycle
    )

    # In your TMCC packet loop when cmd_type == 0b11:
    controller.handle_packet(packet_bytes)

Supported actions
- "on" / "off"
- "color:#RRGGBB"
- "brightness:<0-255>"
- "preset:<id>" (loads a WLED preset ID)
- "cycle" (cycles through pattern_presets list if provided)

Notes
- Uses only the Python stdlib (http.client, json); no extra deps.
- Safe to run on non-Pi systems since it is network-only.
- WLED API docs: https://kno.wled.ge/interfaces/json/
"""

from __future__ import annotations

import http.client
import json
import logging
import random
import threading
import time
from typing import Dict, Iterable, List, Optional, Tuple

logger = logging.getLogger(__name__)


# ---------- TMCC Accessory parsing ----------

def parse_tmcc_switch_or_accessory(packet: Iterable[int], cmd_type: Optional[int] = None) -> Optional[Dict[str, int]]:
    """Parse Switch (0x02) or Accessory (0x03) TMCC packets."""
    packet = list(packet)
    if len(packet) != 3 or packet[0] != 0xFE:
        return None

    if cmd_type is None:
        cmd_type = (packet[1] >> 6) & 0x03
    
    # Accept both Switch (0x02) and Accessory (0x03)
    if cmd_type not in (0x02, 0x03):
        return None

    address = ((packet[1] & 0x3F) << 1) | ((packet[2] & 0x80) >> 7)
    cmd_field = (packet[2] & 0x60) >> 5
    data_field = packet[2] & 0x1F

    return {
        "address": address,
        "cmd_field": cmd_field,
        "data_field": data_field,
        "cmd_type": cmd_type,
    }


# ---------- WLED client ----------

class WLEDClient:
    def __init__(self, host: str = "192.168.0.10", port: int = 80, timeout: float = 2.0) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout

    def post_state(self, payload: dict) -> bool:
        body = json.dumps(payload)
        try:
            conn = http.client.HTTPConnection(self.host, self.port, timeout=self.timeout)
            conn.request(
                "POST",
                "/json/state",
                body=body,
                headers={"Content-Type": "application/json", "Content-Length": str(len(body))},
            )
            resp = conn.getresponse()
            resp.read()  # drain
            if resp.status < 200 or resp.status >= 300:
                logger.warning("WLED HTTP %s %s: %s", resp.status, resp.reason, body)
                return False
            return True
        except Exception as e:  # pragma: no cover - network
            logger.error("WLED request failed: %s", e)
            return False
        finally:
            try:
                conn.close()
            except Exception:
                pass


# ---------- Controller ----------

class DaylightCycle:
    """Smooth 24-hour daylight simulation compressed to a configurable duration.
    
    Phases blend continuously; moon segment lights during night; lightning
    flashes every Nth cycle during late evening/night.
    """

    # Default color keyframes (RGB) at virtual hours
    # 0=midnight, 6=sunrise, 12=noon, 18=sunset, 24=midnight
    DEFAULT_KEYFRAMES = {
        0:  (40, 40, 100),     # midnight - visible blue glow
        5:  (50, 50, 120),     # pre-dawn - brighter blue
        6:  (255, 150, 80),    # sunrise orange
        8:  (255, 220, 180),   # morning warm
        12: (255, 255, 240),   # noon bright white
        16: (255, 240, 220),   # afternoon
        18: (255, 140, 60),    # sunset orange
        20: (80, 60, 120),     # dusk purple
        21: (60, 60, 130),     # evening - visible blue
        24: (40, 40, 100),     # midnight - visible blue glow
    }

    def __init__(
        self,
        client: WLEDClient,
        cycle_duration_sec: float = 1800,  # 30 min default
        led_count: int = 100,
        moon_start: int = 0,
        moon_length: int = 5,
        lightning_every_n_cycles: int = 3,
        keyframes: Optional[Dict[int, Tuple[int, int, int]]] = None,
        update_interval: float = 1.0,
    ) -> None:
        self.client = client
        self.cycle_duration = cycle_duration_sec
        self.led_count = led_count
        self.moon_start = moon_start
        self.moon_length = moon_length
        self.lightning_every_n = lightning_every_n_cycles
        self.keyframes = keyframes or self.DEFAULT_KEYFRAMES
        self.update_interval = update_interval

        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._cycle_count = 0
        self._start_time = 0.0

    # ---- Color interpolation ----
    def _lerp(self, a: int, b: int, t: float) -> int:
        return int(a + (b - a) * t)

    def _lerp_color(
        self, c1: Tuple[int, int, int], c2: Tuple[int, int, int], t: float
    ) -> Tuple[int, int, int]:
        return (
            self._lerp(c1[0], c2[0], t),
            self._lerp(c1[1], c2[1], t),
            self._lerp(c1[2], c2[2], t),
        )

    def _get_sky_color(self, virtual_hour: float) -> Tuple[int, int, int]:
        """Interpolate sky color for a fractional virtual hour (0-24)."""
        hours = sorted(self.keyframes.keys())
        # Find surrounding keyframes
        lower_h = hours[0]
        upper_h = hours[-1]
        for i, h in enumerate(hours):
            if h <= virtual_hour:
                lower_h = h
            if h >= virtual_hour:
                upper_h = h
                break
        if lower_h == upper_h:
            return self.keyframes[lower_h]
        t = (virtual_hour - lower_h) / (upper_h - lower_h)
        return self._lerp_color(self.keyframes[lower_h], self.keyframes[upper_h], t)

    def _is_night(self, virtual_hour: float) -> bool:
        return virtual_hour < 5 or virtual_hour >= 21

    def _is_lightning_window(self, virtual_hour: float) -> bool:
        # Late evening (20-24) or early night (0-4)
        return virtual_hour >= 20 or virtual_hour < 4

    # ---- WLED segment helpers ----
    def _build_segments(self, sky_color: Tuple[int, int, int], virtual_hour: float) -> list:
        """Build WLED segment array: main sky + optional moon."""
        segments = []
        # Main sky segment (all LEDs) - use "mainseg" to ensure it's the primary
        segments.append({
            "id": 0,
            "start": 0,
            "stop": self.led_count,
            "col": [list(sky_color)],
            "fx": 0,  # solid
            "on": True,
            "sel": True,  # select this segment
        })
        # Moon segment - always on to avoid dark spot during day
        # During night: pale bluish white, during day: match sky color
        if self._is_night(virtual_hour) and self.moon_length > 0:
            moon_color = [180, 180, 200]  # pale bluish white at night
        else:
            moon_color = list(sky_color)  # match sky during day
        if self.moon_length > 0:
            segments.append({
                "id": 1,
                "start": self.moon_start,
                "stop": self.moon_start + self.moon_length,
                "col": [moon_color],
                "fx": 0,
                "on": True,
            })
        return segments

    def _flash_lightning(self) -> None:
        """Quick white flash on all LEDs (brief brightness spike)."""
        # Flash by temporarily setting segment 0 to white
        self.client.post_state({"seg": [{"id": 0, "col": [[255, 255, 255]]}]})
        time.sleep(0.05 + random.random() * 0.1)
        # The next loop iteration will restore the correct sky color

    # ---- Main loop ----
    def _loop(self) -> None:
        self._start_time = time.time()
        # Start at afternoon (14:00) instead of midnight
        start_offset = (14.0 / 24.0) * self.cycle_duration
        last_lightning_time = 0.0
        while self._running:
            elapsed = time.time() - self._start_time + start_offset
            cycle_progress = (elapsed % self.cycle_duration) / self.cycle_duration
            virtual_hour = cycle_progress * 24.0

            # Detect new cycle
            new_cycle_count = int(elapsed // self.cycle_duration)
            if new_cycle_count > self._cycle_count:
                self._cycle_count = new_cycle_count
                logger.info("Daylight cycle #%d started", self._cycle_count)

            sky_color = self._get_sky_color(virtual_hour)
            segments = self._build_segments(sky_color, virtual_hour)
            self.client.post_state({"on": True, "seg": segments})

            time.sleep(self.update_interval)

    def start(self) -> None:
        if self._running:
            return
        # Initialize segment 0 to cover all LEDs on startup
        self.client.post_state({
            "on": True,
            "mainseg": 0,
            "seg": [{"id": 0, "start": 0, "stop": self.led_count, "on": True}]
        })
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        logger.info("Daylight cycle started (%.0f sec per 24h)", self.cycle_duration)

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=2)
        logger.info("Daylight cycle stopped")


class WLEDController:
    """TMCC accessory -> WLED bridge with optional daylight cycle."""

    def __init__(
        self,
        mapping: Dict[Tuple[int, int], str],
        host: str = "192.168.0.10",
        port: int = 80,
        pattern_presets: Optional[List[int]] = None,
        timeout: float = 2.0,
        daylight_cycle: bool = False,
        cycle_duration_sec: float = 1800,
        led_count: int = 100,
        moon_start: int = 0,
        moon_length: int = 5,
        lightning_every_n_cycles: int = 3,
    ) -> None:
        self.mapping = mapping
        self.client = WLEDClient(host=host, port=port, timeout=timeout)
        self.pattern_presets = pattern_presets or []
        self.pattern_index = 0

        # Daylight cycle
        self.daylight: Optional[DaylightCycle] = None
        if daylight_cycle:
            self.daylight = DaylightCycle(
                client=self.client,
                cycle_duration_sec=cycle_duration_sec,
                led_count=led_count,
                moon_start=moon_start,
                moon_length=moon_length,
                lightning_every_n_cycles=lightning_every_n_cycles,
            )
            self.daylight.start()

    def handle_packet(self, packet: Iterable[int], cmd_type: Optional[int] = None) -> bool:
        parsed = parse_tmcc_switch_or_accessory(packet, cmd_type)
        if not parsed:
            return False

        key = (parsed["address"], parsed["data_field"])
        action = self.mapping.get(key)
        if not action:
            logger.debug("WLED ignored addr=%s data=%s cmd_type=%s", parsed["address"], parsed["data_field"], parsed["cmd_type"])
            return False

        logger.info("WLED action '%s' for addr %s data %s", action, parsed["address"], parsed["data_field"])
        self.apply_action(action)
        return True

    def apply_action(self, action: str) -> None:
        if action == "on":
            self.client.post_state({"on": True})
        elif action == "off":
            # Stop daylight cycle if running, then turn off
            if self.daylight:
                self.daylight.stop()
            self.client.post_state({"on": False})
        elif action == "full_white":
            # Stop daylight cycle if running, then set full bright white
            if self.daylight:
                self.daylight.stop()
            self.client.post_state({"on": True, "bri": 255, "seg": [{"id": 0, "col": [[255, 255, 255]]}]})
        elif action == "daylight_start":
            if self.daylight:
                self.daylight.start()
            else:
                logger.warning("Daylight cycle not configured")
        elif action == "daylight_stop":
            if self.daylight:
                self.daylight.stop()
            else:
                logger.warning("Daylight cycle not configured")
        elif action.startswith("color:"):
            hex_color = action.split(":", 1)[1].lstrip("#")
            if len(hex_color) == 6:
                r = int(hex_color[0:2], 16)
                g = int(hex_color[2:4], 16)
                b = int(hex_color[4:6], 16)
                payload = {"seg": [{"id": 0, "col": [[r, g, b]]}]}
                self.client.post_state(payload)
            else:
                logger.warning("Invalid color format: %s", hex_color)
        elif action.startswith("brightness:"):
            try:
                value = int(action.split(":", 1)[1])
                value = max(0, min(255, value))
                self.client.post_state({"bri": value})
            except ValueError:
                logger.warning("Invalid brightness action: %s", action)
        elif action.startswith("preset:"):
            try:
                preset_id = int(action.split(":", 1)[1])
                self.client.post_state({"ps": preset_id})
            except ValueError:
                logger.warning("Invalid preset action: %s", action)
        elif action == "cycle":
            if not self.pattern_presets:
                logger.warning("No pattern_presets configured for cycle action")
                return
            self.pattern_index = (self.pattern_index + 1) % len(self.pattern_presets)
            preset_id = self.pattern_presets[self.pattern_index]
            self.client.post_state({"ps": preset_id})
            logger.info("WLED cycled to preset %s", preset_id)
        else:
            logger.warning("Unhandled WLED action: %s", action)


if __name__ == "__main__":  # basic manual test harness
    logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(message)s")
    controller = WLEDController(
        host="192.168.0.10",
        mapping={(50, 0x01): "on", (50, 0x02): "off", (50, 0x03): "cycle"},
        pattern_presets=[1, 2, 3],
    )

    sample_packets = [
        [0xFE, 0b11110010, 0b00000001],  # addr ~50, data 0x01 → on
        [0xFE, 0b11110010, 0b00000010],  # addr ~50, data 0x02 → off
        [0xFE, 0b11110010, 0b00000011],  # addr ~50, data 0x03 → cycle presets
    ]

    for pkt in sample_packets:
        controller.handle_packet(pkt)
