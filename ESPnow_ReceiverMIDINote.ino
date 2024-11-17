#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Relay configuration
const int NUM_RELAYS = 16;  // Increased to handle more relays
const int RELAY_PINS[] = {
    16, 17, 18, 19,  // First bank
    21, 22, 23, 25,  // Second bank
    26, 27, 32, 33,  // Third bank
    13, 14, 15, 4    // Fourth bank
};

// Timing configuration
const int TRIGGER_DURATION = 10;  // 10ms trigger duration
const int LED_PIN = 2;

// Structure for incoming messages
typedef struct {
    uint8_t relay_mask[2];  // Two bytes to handle up to 16 relays
} relay_message;

// Timing tracking
struct RelayState {
    bool active;
    unsigned long triggerTime;
};
RelayState relayStates[NUM_RELAYS];

void activateRelays(uint16_t relayMask) {
    // Activate all requested relays simultaneously
    for (int i = 0; i < NUM_RELAYS; i++) {
        if (relayMask & (1 << i)) {
            digitalWrite(RELAY_PINS[i], HIGH);
            relayStates[i].active = true;
            relayStates[i].triggerTime = millis();
            
            Serial.print("Relay ");
            Serial.print(i);
            Serial.println(" activated");
        }
    }
    
    // Visual feedback
    digitalWrite(LED_PIN, LOW);
    delay(1);
    digitalWrite(LED_PIN, HIGH);
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
    relay_message incomingMsg;
    memcpy(&incomingMsg, data, sizeof(incomingMsg));
    
    // Combine the two bytes into a 16-bit mask
    uint16_t relayMask = (incomingMsg.relay_mask[1] << 8) | incomingMsg.relay_mask[0];
    
    // Activate relays based on mask
    if (relayMask > 0) {
        activateRelays(relayMask);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Configure pins
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    // Initialize all relay pins and states
    for (int i = 0; i < NUM_RELAYS; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], LOW);
        relayStates[i].active = false;
        relayStates[i].triggerTime = 0;
    }

    // Initialize ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(2, WIFI_SECOND_CHAN_NONE);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);

    Serial.println("\n--- ESP-NOW Relay Receiver (Multi-Trigger) ---");
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    
    // Test sequence
    Serial.println("Running test sequence...");
    // Test individual relays
    for (int i = 0; i < NUM_RELAYS; i++) {
        digitalWrite(RELAY_PINS[i], HIGH);
        delay(100);
        digitalWrite(RELAY_PINS[i], LOW);
        delay(100);
    }
    
    // Test simultaneous activation
    uint16_t testMask = 0xFFFF;  // All relays
    activateRelays(testMask);
    delay(TRIGGER_DURATION);
    for (int i = 0; i < NUM_RELAYS; i++) {
        digitalWrite(RELAY_PINS[i], LOW);
    }
    
    Serial.println("Ready for operation!");
}

void loop() {
    // Check for relays that need to be turned off
    unsigned long currentTime = millis();
    for (int i = 0; i < NUM_RELAYS; i++) {
        if (relayStates[i].active && 
            (currentTime - relayStates[i].triggerTime >= TRIGGER_DURATION)) {
            digitalWrite(RELAY_PINS[i], LOW);
            relayStates[i].active = false;
            Serial.print("Relay ");
            Serial.print(i);
            Serial.println(" deactivated");
        }
    }
    
    // Status LED heartbeat
    static unsigned long lastBlink = 0;
    if (currentTime - lastBlink >= 1000) {
        lastBlink = currentTime;
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
}