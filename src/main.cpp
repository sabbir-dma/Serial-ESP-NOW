/*
 * ESP-NOW Bridge Firmware v2.0
 * 
 * This device reads sensor data from Serial2 (from first ESP),
 * formats it into the Message structure, and broadcasts via ESP-NOW
 * ONLY when new data is received.
 * 
 * Hardware: ESP32
 * Serial2: Connected to first ESP's serial output
 * ESP-NOW: Broadcasts formatted messages to gateway
 */

#include <esp_now.h>
#include <WiFi.h>

// ==================== CONFIGURATION ====================
#define SERIAL_BAUD 115200           // Serial baud rate for communication with first ESP
#define SCANNER_ID "102"          // Scanner ID (should match first ESP)
#define GATEWAY_ID "gw0"             // Gateway receiver ID
#define BROADCAST_ADDR {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}  // Broadcast MAC
#define MSG_ID_PREFIX "MSG"          // Message ID prefix
#define MAX_RETRY 3                  // Max retry attempts for ESP-NOW send

// ==================== MESSAGE STRUCTURE ====================
// This matches your gateway format
struct Message {
    char sender_id[10];      // SCAN01
    char receiver_id[10];    // gw0
    char command[60];        // Sensor data: MAC,NAME,RSSI,FILTERED,TEMP,BATT
    uint8_t type;            // 1 for sensor data
    char msg_id[6];          // Unique message ID (e.g., MSG001)
    char last_hop[10];       // Bridge ID (e.g., "BRIDGE01")
    uint8_t hop_count;       // 1 (first hop)
};

// ==================== ESP-NOW CALLBACKS ====================
// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("✅ ESP-NOW Send Success");
    } else {
        Serial.println("❌ ESP-NOW Send Failed");
    }
}

// ==================== ESP-NOW SETUP ====================
bool setupESPNow() {
    // Initialize WiFi in station mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Error initializing ESP-NOW");
        return false;
    }
    
    // Set send callback
    esp_now_register_send_cb(OnDataSent);
    
    // Add broadcast peer
    uint8_t broadcastAddress[] = BROADCAST_ADDR;
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("❌ Failed to add broadcast peer");
        return false;
    }
    
    Serial.println("✅ ESP-NOW initialized successfully");
    Serial.printf("📡 Broadcasting to: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  broadcastAddress[0], broadcastAddress[1], broadcastAddress[2],
                  broadcastAddress[3], broadcastAddress[4], broadcastAddress[5]);
    return true;
}

// ==================== MESSAGE CREATION ====================
uint16_t msgCounter = 0;

void createMessage(Message &msg, const char* command, uint8_t type = 4 ) {
    // Clear message
    memset(&msg, 0, sizeof(Message));
    
    // Set sender (scanner ID)
    strncpy(msg.sender_id, SCANNER_ID, sizeof(msg.sender_id) - 1);
    
    // Set receiver (gateway)
    strncpy(msg.receiver_id, GATEWAY_ID, sizeof(msg.receiver_id) - 1);
    
    // Set command (sensor data)
    strncpy(msg.command, command, sizeof(msg.command) - 1);
    
    // Set type (1 = sensor data)
    msg.type = type;
    
    // Generate unique message ID
    msgCounter++;
    if (msgCounter > 999) msgCounter = 0;
    snprintf(msg.msg_id, sizeof(msg.msg_id), "%s%03d", MSG_ID_PREFIX, msgCounter);
    
    // Set last hop (bridge ID)
    strncpy(msg.last_hop, SCANNER_ID, sizeof(msg.sender_id) - 1);
    
    // Set hop count (always 1 for direct broadcast from bridge)
    msg.hop_count = 0;
}

// ==================== BROADCAST VIA ESP-NOW ====================
bool broadcastMessage(Message &msg) {
    uint8_t broadcastAddress[] = BROADCAST_ADDR;
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(Message));
    
    if (result == ESP_OK) {
        // Print debug info
        Serial.printf("📤 Broadcast: Sender=%s, Receiver=%s, Type=%d, ID=%s, Hop=%d\n",
                      msg.sender_id, msg.receiver_id, msg.type, msg.msg_id, msg.hop_count);
        Serial.printf("   Command: %s\n", msg.command);
        return true;
    } else {
        Serial.printf("❌ ESP-NOW send error: %d\n", result);
        return false;
    }
}

// ==================== PARSE SENSOR DATA FROM SERIAL ====================
bool parseAndBroadcastSensorData(const String& data) {
    // Expected format: [scanner_id,mac,name,rssi,filtered_rssi,temperature,battery]
    // Example: [SCAN01,AA:BB:CC:DD:EE:FF,Sensor1,-65,-62.5,25.3,85]
    
    // Check if data starts with '[' and ends with ']'
    if (!data.startsWith("[") || !data.endsWith("]")) {
        Serial.printf("⚠️ Invalid format (missing brackets): %s\n", data.c_str());
        return false;
    }
    
    // Remove brackets
    String content = data.substring(1, data.length() - 1);
    
    // Parse fields
    int fields[7]; // Store positions of commas
    int fieldCount = 0;
    
    for (int i = 0; i < content.length(); i++) {
        if (content.charAt(i) == ',' && fieldCount < 7) {
            fields[fieldCount++] = i;
        }
    }
    
    // We need at least 6 commas (7 fields)
    if (fieldCount < 6) {
        Serial.printf("⚠️ Invalid format (not enough fields): %s\n", data.c_str());
        return false;
    }
    
    // Extract fields
    // Field 0: scanner_id (we'll use our own)
    // Field 1: mac
    String mac = content.substring(fields[0] + 1, fields[1]);
    
    // Field 2: name
    String name = content.substring(fields[1] + 1, fields[2]);
    
    // Field 3: raw_rssi
    String rawRssi = content.substring(fields[2] + 1, fields[3]);
    
    // Field 4: filtered_rssi
    String filteredRssi = content.substring(fields[3] + 1, fields[4]);
    
    // Field 5: temperature
    String temp = content.substring(fields[4] + 1, fields[5]);
    
    // Field 6: battery
    String battery = content.substring(fields[5] + 1);
    
    // Remove any trailing whitespace
    battery.trim();
    
    // Create command string with sensor data in EXACT format required by gateway
    // Format: mac,name,raw_rssi,filtered_rssi,temperature,battery
    char command[80];
    snprintf(command, sizeof(command), "%s,%s,%s,%s,%s,%s",
             mac.c_str(),
             name.c_str(),
             rawRssi.c_str(),
             filteredRssi.c_str(),
             temp.c_str(),
             battery.c_str());
    
    // Create message
    Message msg;
    createMessage(msg, command, 1);  // type 1 = sensor data
    
    // Broadcast via ESP-NOW
    bool success = broadcastMessage(msg);
    
    // Print received data for debugging
    if (success) {
        Serial.printf("✅ Sensor data broadcast: %s\n", command);
    }
    
    return success;
}

// ==================== PROCESS SERIAL DATA ====================
void processSerialData() {
    static String buffer = "";
    
    // Read all available data from Serial2
    while (Serial2.available()) {
        char c = Serial2.read();
        
        // If we get a newline, process the line
        if (c == '\n') {
            if (buffer.length() > 0) {
                // Trim whitespace
                buffer.trim();
                
                // Log received data
                Serial.printf("📥 Received: %s\n", buffer.c_str());
                
                // Parse and broadcast immediately
                parseAndBroadcastSensorData(buffer);
                
                buffer = ""; // Clear buffer for next line
            }
        } else if (c != '\r') {
            // Ignore carriage returns
            buffer += c;
            
            // Prevent buffer overflow
            if (buffer.length() > 512) {
                Serial.println("⚠️ Buffer overflow, clearing");
                buffer = "";
            }
        }
    }
}

// ==================== SYSTEM STATUS ====================
void printStatus() {
    Serial.println("\n========== SYSTEM STATUS ==========");
    Serial.printf("Scanner ID: %s\n", SCANNER_ID);
    Serial.printf("Gateway ID: %s\n", GATEWAY_ID);
    Serial.printf("Bridge ID: %s\n", SCANNER_ID);
    Serial.printf("Message Counter: %d\n", msgCounter);
    Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("ESP-NOW: %s\n", esp_now_init() == ESP_OK ? "Active ✅" : "Failed ❌");
    Serial.println("==================================\n");
}

// ==================== SETUP ====================
void setup() {
    // Initialize Serial for debugging
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("ESP-NOW Bridge Firmware v2.0");
    Serial.println("Broadcast ONLY on new data");
    Serial.println("========================================\n");
    
    // Initialize Serial2 for communication with first ESP
    Serial2.begin(SERIAL_BAUD, SERIAL_8N1, 16, 17); // RX=16, TX=17
    Serial.println("✅ Serial2 initialized for sensor data");
    Serial.printf("   Baud: %d, RX: GPIO16, TX: GPIO17\n\n", SERIAL_BAUD);
    
    // Setup ESP-NOW
    if (!setupESPNow()) {
        Serial.println("❌ ESP-NOW setup failed. Restarting in 5 seconds...");
        delay(5000);
        ESP.restart();
    }
    
    // Print initial status
    printStatus();
    Serial.println("✅ System ready! Waiting for sensor data...\n");
}

// ==================== LOOP ====================
void loop() {
    // Process incoming serial data (this is the ONLY thing we do)
    processSerialData();
    
    // Small delay to prevent watchdog issues and allow other tasks
    delay(10);
}