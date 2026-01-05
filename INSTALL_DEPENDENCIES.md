# Installing Python Dependencies on Arduino UNO Q

## Required Python Packages

The Python bridge needs these packages for full functionality:

### 1. **python3-serial** - Serial communication (Required)
```bash
apt install -y python3-serial
```

### 2. **python3-zeroconf** - mDNS discovery (Optional but recommended)
```bash
apt install -y python3-zeroconf
```

### 3. **python3-pycryptodome** - Speck encryption (Optional)
```bash
apt install -y python3-pycryptodome
```

## Installation on Arduino UNO Q

### Via SSH:
```bash
# SSH into the board (replace with your board's IP)
ssh root@<YOUR_BOARD_IP>

# Update package list
apt update

# Install Python packages using apt
apt install -y python3-serial python3-zeroconf python3-pycryptodome

# Or install one at a time
apt install -y python3-serial
apt install -y python3-zeroconf
apt install -y python3-pycryptodome
```

**Note:** Arduino UNO Q uses `apt` (Debian package manager) for Python packages, not `pip`.

## What Each Package Does

### **pyserial** (Required)
- Reads TMCC packets from Lionel Base 3 via SER2 FTDI adapter
- **Status**: Required for Lionel Base 3 connection

### **zeroconf** (Optional)
- Auto-discovers MTH WTIU on your network via mDNS
- **Fallback**: Manual IP configuration if not installed
- **Benefit**: No need to hardcode WTIU IP address

### **pycryptodome** (Optional)
- Encrypts commands to MTH WTIU using Speck cipher
- **Fallback**: Sends plain text commands if not installed
- **Benefit**: Secure communication with WTIU

## Graceful Degradation

The Python script will work without optional packages:

```
✅ With all packages:
   - Auto-discovers WTIU via mDNS
   - Encrypts commands with Speck
   - Full functionality

⚠️ Without zeroconf:
   - Uses manual IP: 192.168.0.100
   - You can edit the script to change IP
   - Everything else works

⚠️ Without pycryptodome:
   - Sends plain text commands
   - May work depending on WTIU settings
   - Everything else works

❌ Without pyserial:
   - Cannot read Lionel Base 3
   - But MCU communication still works
```

## Verify Installation

```bash
# Check if packages are installed
python3 -c "import serial; print('✅ pyserial installed')"
python3 -c "import zeroconf; print('✅ zeroconf installed')"
python3 -c "from Crypto.Cipher import AES; print('✅ pycryptodome installed')"
```

## Troubleshooting

### Package not found
```bash
# Update package list
apt update

# Search for available packages
apt search python3-serial
apt search python3-zeroconf
apt search python3-pycryptodome
```

### Import errors
```bash
# Check Python version (needs 3.7+)
python3 --version

# Check installed packages
dpkg -l | grep -E "python3-serial|python3-zeroconf|python3-pycryptodome"
```

## Architecture Summary

```
┌─────────────────────────────────────────────────┐
│ Python on MPU (lionel_mth_bridge_fixed.py)     │
│                                                 │
│  📦 pyserial                                    │
│     └─ Reads Lionel Base 3 TMCC packets        │
│                                                 │
│  📦 zeroconf (optional)                         │
│     └─ Auto-discovers MTH WTIU via mDNS         │
│                                                 │
│  📦 pycryptodome (optional)                     │
│     └─ Encrypts commands with Speck cipher      │
│                                                 │
│  Built-in: socket                               │
│     ├─ Connects to MCU via arduino-router       │
│     └─ Connects to MTH WTIU via WiFi            │
└─────────────────────────────────────────────────┘
```

## Quick Install Command

```bash
# Install everything at once (replace with your board's IP)
ssh root@<YOUR_BOARD_IP> "apt update && apt install -y python3-serial python3-zeroconf python3-pycryptodome"
```
