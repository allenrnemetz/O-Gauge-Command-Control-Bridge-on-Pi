/*
 * MTH WTIU Handler - Fixed Version
 * Handles 3-part command format: CMD:type:engine:value
 * Includes heartbeat, state management, and status reporting
 */

#include <Arduino.h>

// Command constants (MUST MATCH Python mcu_command_types)
#define CMD_DIRECTION       1
#define CMD_SPEED           2
#define CMD_FUNCTION        3
#define CMD_SMOKE           4
#define CMD_PFA             5
#define CMD_ENGINE          6
#define CMD_ENGINE_SELECT   7
#define CMD_PROTOWHISTLE    8

// Command packet
struct CommandPacket {
  uint8_t command_type;
  uint8_t engine_number;
  uint16_t value;
  bool bool_value;
};

// Engine state structure
struct EngineState {
  uint8_t speed = 0;        // 0-31
  bool direction = true;    // true=forward, false=reverse
  bool bell = false;
  bool whistle = false;
  bool smoke = false;
  unsigned long last_update = 0;
};

// Track multiple engines
#define MAX_ENGINES 20
EngineState engineStates[MAX_ENGINES];

// Status LED
#define STATUS_LED LED_BUILTIN

// Heartbeat settings
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 5000;  // 5 seconds

// Connection monitoring
unsigned long lastCommandTime = 0;
const unsigned long COMMAND_TIMEOUT = 10000;   // 10 seconds

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== MTH WTIU Handler (Fixed) ===");
  
  pinMode(STATUS_LED, OUTPUT);
  
  // Startup blink sequence
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(200);
    digitalWrite(STATUS_LED, LOW);
    delay(200);
  }
  
  // Initialize Serial1 for MPU communication
  Serial1.begin(115200);
  
  Serial.println("Ready - Waiting for commands...");
  Serial.println("Command format: CMD:type:engine:value");
  Serial.println("Status: HEARTBEAT, STATUS, ACK responses");
}

void loop() {
  checkSerialCommands();
  checkHeartbeat();
  checkConnectionStatus();
  
  delay(10);
}

void checkSerialCommands() {
  // Check for commands from MPU via Serial1
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      lastCommandTime = millis();
      
      if (command.startsWith("CMD:")) {
        processCommand(command);
      } else if (command == "STATUS") {
        sendStatusReport();
      } else if (command == "RESET") {
        resetConnection();
      } else {
        Serial.print("Unknown command: ");
        Serial.println(command);
      }
    }
  }
  
  // Check for USB Serial commands (for debugging)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("CMD:")) {
      processCommand(cmd);
    }
  }
}

void processCommand(String command) {
  // Parse command format: "CMD:type:engine:value"
  int colons[3];
  colons[0] = command.indexOf(':');        // After CMD
  colons[1] = command.indexOf(':', colons[0] + 1);  // After type
  colons[2] = command.indexOf(':', colons[1] + 1);  // After engine
  
  if (colons[0] > 0 && colons[1] > colons[0] && colons[2] > colons[1]) {
    int cmd_type = command.substring(colons[0] + 1, colons[1]).toInt();
    int engine_num = command.substring(colons[1] + 1, colons[2]).toInt();
    int cmd_value = command.substring(colons[2] + 1).toInt();
    
    Serial.print("Parsed - Type: ");
    Serial.print(cmd_type);
    Serial.print(", Engine: ");
    Serial.print(engine_num);
    Serial.print(", Value: ");
    Serial.println(cmd_value);
    
    // Create command packet
    CommandPacket cmd;
    cmd.command_type = cmd_type;
    cmd.engine_number = engine_num;
    cmd.value = cmd_value;
    cmd.bool_value = (cmd_value > 0);
    
    // Process command
    executeMTHCommand(&cmd);
    
    // Send detailed ACK back to MPU via Serial1
    Serial1.print("ACK:");
    Serial1.print(cmd_type);
    Serial1.print(":");
    Serial1.println(engine_num);
    
    // Visual feedback
    digitalWrite(STATUS_LED, HIGH);
    delay(50);
    digitalWrite(STATUS_LED, LOW);
    
  } else {
    Serial.print("Invalid command format: ");
    Serial.println(command);
    Serial1.println("ERROR:Invalid format");
  }
}

void executeMTHCommand(CommandPacket* cmd) {
  // Update local state first
  updateEngineState(cmd);
  
  // Log command execution
  Serial.print("Engine ");
  Serial.print(cmd->engine_number);
  Serial.print(" - CMD ");
  Serial.print(cmd->command_type);
  Serial.print(": ");
  Serial.println(cmd->value);
  
  // Handle different command types
  switch (cmd->command_type) {
    case CMD_DIRECTION:
      Serial.print("Direction: ");
      Serial.println(cmd->bool_value ? "Forward" : "Reverse");
      break;
      
    case CMD_SPEED:
      Serial.print("Speed: ");
      Serial.println(cmd->value);
      break;
      
    case CMD_FUNCTION:
      handleFunctionCommand(cmd);
      break;
      
    case CMD_SMOKE:
      Serial.print("Smoke: ");
      Serial.println(cmd->bool_value ? "On" : "Off");
      break;
      
    case CMD_ENGINE:
      Serial.print("Engine: ");
      Serial.println(cmd->value == 1 ? "Start" : "Stop");
      break;
      
    case CMD_PROTOWHISTLE:
      Serial.print("ProtoWhistle: ");
      Serial.println(cmd->value);
      break;
      
    default:
      Serial.print("Unknown command type: ");
      Serial.println(cmd->command_type);
      break;
  }
  
  Serial.println("Command processed (MTH connection handled by Python)");
}

void handleFunctionCommand(CommandPacket* cmd) {
  switch (cmd->value) {
    case 1:  // Horn
      Serial.println("Function: Horn");
      break;
    case 2:  // Bell
      Serial.println("Function: Bell");
      break;
    case 3:  // On
      Serial.println("Function: On");
      break;
    case 4:  // Off
      Serial.println("Function: Off");
      break;
    default:
      Serial.print("Function: Value ");
      Serial.println(cmd->value);
      break;
  }
}

void updateEngineState(CommandPacket* cmd) {
  uint8_t engine_idx = cmd->engine_number;
  
  if (engine_idx >= MAX_ENGINES) {
    Serial.print("Engine number out of range: ");
    Serial.println(engine_idx);
    return;
  }
  
  EngineState* state = &engineStates[engine_idx];
  state->last_update = millis();
  
  switch (cmd->command_type) {
    case CMD_SPEED:
      state->speed = cmd->value;
      break;
      
    case CMD_DIRECTION:
      state->direction = cmd->bool_value;
      break;
      
    case CMD_FUNCTION:
      if (cmd->value == 2) state->bell = true;      // Bell on
      else if (cmd->value == 4) state->bell = false; // Bell off
      break;
      
    case CMD_SMOKE:
      state->smoke = cmd->bool_value;
      break;
      
  }
}

void checkHeartbeat() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastHeartbeat > HEARTBEAT_INTERVAL) {
    // Send heartbeat to MPU
    Serial1.println("HEARTBEAT");
    
    // Visual heartbeat indicator
    digitalWrite(STATUS_LED, HIGH);
    delay(20);
    digitalWrite(STATUS_LED, LOW);
    
    lastHeartbeat = currentTime;
  }
}

void checkConnectionStatus() {
  unsigned long currentTime = millis();
  
  // Check if we haven't received commands for too long
  if (lastCommandTime > 0 && (currentTime - lastCommandTime) > COMMAND_TIMEOUT) {
    Serial.println("Connection timeout - no commands received");
    Serial1.println("TIMEOUT");
    lastCommandTime = currentTime; // Reset to avoid spam
  }
}

void sendStatusReport() {
  Serial1.print("STATUS:");
  Serial1.print("ENGINES:");
  
  // Report all active engines
  bool hasEngines = false;
  for (int i = 0; i < MAX_ENGINES; i++) {
    if (engineStates[i].speed > 0 || 
        engineStates[i].bell || 
        engineStates[i].whistle ||
        engineStates[i].smoke) {
      
      if (hasEngines) Serial1.print(";");
      hasEngines = true;
      
      Serial1.print(i);
      Serial1.print("=");
      Serial1.print(engineStates[i].speed);
      Serial1.print(",");
      Serial1.print(engineStates[i].direction ? "F" : "R");
      
      if (engineStates[i].bell) Serial1.print(",B1");
      if (engineStates[i].whistle) Serial1.print(",W1");
      if (engineStates[i].smoke) Serial1.print(",S1");
    }
  }
  
  if (!hasEngines) {
    Serial1.print("NONE");
  }
  
  Serial1.println();
  
  // Send system status
  Serial1.print("STATUS:UPTIME:");
  Serial1.println(millis() / 1000);
}

void resetConnection() {
  Serial.println("Resetting connection to MPU...");
  
  // Clear Serial1 buffer
  while (Serial1.available()) {
    Serial1.read();
  }
  
  // Send reset notification
  Serial1.println("RESET");
  
  // Reset engine states
  for (int i = 0; i < MAX_ENGINES; i++) {
    engineStates[i] = EngineState();
  }
  
  // Visual reset indicator
  for (int i = 0; i < 3; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(100);
    digitalWrite(STATUS_LED, LOW);
    delay(100);
  }
  
  Serial.println("Connection reset complete");
  lastCommandTime = millis();
}
