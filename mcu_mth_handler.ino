/*
 * mcu_mth_handler.ino
 * 
 * MTH WTIU Handler for Arduino UNO Q MCU (Sub-processor)
 * Communicates with MPU via serial port and controls MTH trains via WiFi
 * 
 * Author: Allen Nemetz
 * Credits:
 * - Mark DiVecchio for his immense work translating MTH commands to and from the MTH WTIU
 *   http://www.silogic.com/trains/RTC_Running.html
 * - Lionel LLC for publishing TMCC and Legacy protocol specifications
 * - O Gauge Railroading Forum (https://www.ogrforum.com/) for the model railroad community
 * 
 * Disclaimer: This software is provided "as-is" without warranty. The author assumes no liability 
 * for any damages resulting from the use or misuse of this software. Users are responsible for 
 * ensuring safe operation of their model railroad equipment.
 * 
 * Copyright (c) 2026 Allen Nemetz. All rights reserved.
 * 
 * License: GNU General Public License v3.0
 * 
 * This sketch runs on the Arduino UNO Q MCU (sub-processor) and handles:
 * - Command reception from MPU via Serial1 (internal UART)
 * - Command processing and local response
 * - Real-time train control status monitoring
 * - USB Serial debugging output
 * 
 * Note: WiFi, mDNS, and Speck encryption are handled by Python on MPU
 */

// Arduino UNO Q MCU (STM32U585) - No WiFi on MCU
// WiFi is handled by MPU (Python side)
#include <Arduino.h>
// Note: WiFi libraries removed - MCU has no WiFi capability
// Note: Monitor.h removed - use Serial for USB debug output

// Command constants (must match MPU)
#define CMD_SPEED           1
#define CMD_DIRECTION       2
#define CMD_BELL            3
#define CMD_WHISTLE         4
#define CMD_STARTUP         5
#define CMD_SHUTDOWN        6
#define CMD_ENGINE_SELECT   7
#define CMD_PROTOWHISTLE    8
#define CMD_WLED            9

// Command packet structure (must match MPU)
struct CommandPacket {
  uint8_t command_type;
  uint8_t engine_number;
  uint16_t value;
  bool bool_value;
};

// Note: WiFi/MTH WTIU connection is handled by Python on MPU
// MCU only receives commands and sends acknowledgments

// MPU-MCU communication via internal serial (Serial1 on Arduino UNO Q)
// Note: Serial is for USB/Monitor, Serial1 is for MPU communication

// Status LED
#define STATUS_LED LED_BUILTIN

// ProtoWhistle state
bool protowhistle_enabled = false;
int protowhistle_pitch = 0; // 0-3 pitch levels

// Note: Speck encryption handled by Python on MPU

void setup() {
  // Initialize Serial for USB debugging
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== MTH WTIU Handler Starting ===");
  
  // Initialize status LED
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  
  // Test LED blink to show MCU is running
  for (int i = 0; i < 5; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(200);
    digitalWrite(STATUS_LED, LOW);
    delay(200);
  }
  
  Serial.println("MCU initialized - LED test complete");
  
  // Initialize Serial1 for MPU-MCU communication (internal UART)
  Serial1.begin(115200);
  Serial.println("Serial1 initialized for MPU communication");
  
  Serial.println("=== MTH WTIU Handler Ready ===");
  Serial.println("Note: WiFi/MTH handled by Python on MPU");
  Serial.println("Waiting for commands from MPU...");
}

void loop() {
  // Check for commands from MPU via Serial1 (internal UART)
  checkSerialCommands();
  
  // Check for test commands from USB Serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      Serial.print("RX USB: ");
      Serial.println(command);
      
      if (command.startsWith("CMD:")) {
        processCommand(command);
      }
    }
  }
  
  delay(10);
}

void processCommand(String command) {
  // Parse command format: "CMD:type:value"
  int firstColon = command.indexOf(':');
  int secondColon = command.indexOf(':', firstColon + 1);
  
  if (firstColon > 0 && secondColon > firstColon) {
    int cmd_type = command.substring(firstColon + 1, secondColon).toInt();
    int cmd_value = command.substring(secondColon + 1).toInt();
    
    Serial.print("Parsed - Type: ");
    Serial.print(cmd_type);
    Serial.print(", Value: ");
    Serial.println(cmd_value);
    
    // Create command packet
    CommandPacket cmd;
    cmd.command_type = cmd_type;
    cmd.engine_number = 1;
    cmd.value = cmd_value;
    cmd.bool_value = (cmd_value > 0);
    
    // Process command
    executeMTHCommand(&cmd);
    
    // Send ACK back to MPU via Serial1
    Serial1.println("ACK");
    Serial.println("Sent ACK to MPU");
    
    // Blink LED
    digitalWrite(STATUS_LED, HIGH);
    delay(50);
    digitalWrite(STATUS_LED, LOW);
  }
}

void checkSerialCommands() {
  // Check for commands from MPU via Serial1 (internal UART)
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      Serial.print("RX Serial1: ");
      Serial.println(command);
      
      if (command.startsWith("CMD:")) {
        processCommand(command);
      }
    }
  }
}

void executeMTHCommand(CommandPacket* cmd) {
  // Log command execution
  Serial.print("Executing command: type=");
  Serial.print(cmd->command_type);
  Serial.print(", engine=");
  Serial.print(cmd->engine_number);
  Serial.print(", value=");
  Serial.print(cmd->value);
  Serial.println();
  
  switch (cmd->command_type) {
    case CMD_ENGINE_SELECT:
      Serial.print("Engine Select: ");
      Serial.println(cmd->engine_number);
      break;
      
    case CMD_SPEED:
      Serial.print("Speed: ");
      Serial.println(cmd->value);
      break;
      
    case CMD_DIRECTION:
      Serial.print("Direction: ");
      Serial.println(cmd->bool_value ? "Forward" : "Reverse");
      break;
      
    case CMD_BELL:
      Serial.print("Bell: ");
      Serial.println(cmd->bool_value ? "On" : "Off");
      break;
      
    case CMD_WHISTLE:
      Serial.print("Whistle: ");
      Serial.println(cmd->bool_value ? "On" : "Off");
      break;
      
    case CMD_PROTOWHISTLE:
      Serial.print("ProtoWhistle: ");
      Serial.println(cmd->value);
      if (cmd->value == 0) {
        protowhistle_enabled = cmd->bool_value;
      }
      break;
      
    case CMD_WLED:
      Serial.print("WLED: ");
      Serial.println(cmd->value);
      break;
      
    case CMD_STARTUP:
      Serial.println("Startup");
      break;
      
    case CMD_SHUTDOWN:
      Serial.println("Shutdown");
      break;
      
    default:
      Serial.print("Unknown command type: ");
      Serial.println(cmd->command_type);
      break;
  }
  
  Serial.println("Command processed (MTH connection handled by Python)");
}
