#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Array of PWM receiver MAC addresses (Channel 1)
uint8_t pwmReceiverMacs[][6] = {
    {0x08, 0xA6, 0xF7, 0x66, 0x0E, 0x84}, // First receiver MAC: 08:a6:f7:66:0E:84 CC 1 
    {0x10, 0x06, 0x1C, 0x82, 0x76, 0x9C}, // Second receiver MAC: 10:06:1c:80:b0:e8 CC 2
    {0x10, 0x06, 0x1C, 0x80, 0xb0, 0xe8}, // Third receiver MAC: 10:06:1c:81:31:7c  CC3 10:06:1C:80:B0:E8 
    {0x08, 0xa6, 0xf7, 0x65, 0xeb, 0xa0}, // Fourth receiver MAC: 08:a6:f7:65:eb:a0 CC4 
    {0x08, 0xa6, 0xf7, 0x65, 0xe5, 0x58}
};
const int NUM_PWM_RECEIVERS = 5;

// Relay receiver MAC address (Channel 2)
uint8_t relayReceiverMac[] = {0x08, 0xa6, 0xf7, 0x66, 0x08, 0xFF};  // Update with actual MAC

// Message structures
typedef struct {
    uint8_t cc_number;     // CC 1-10
    uint8_t cc_value;      // 0-127
} pwm_message;

typedef struct {
    uint8_t relay_mask[2];  // Two bytes to handle up to 16 relays
} relay_message;

pwm_message pwmMsg;
relay_message relayMsg;

// Status LED
const int LED_PIN = 2;

// MAC address to string helper function
void macToString(const uint8_t* mac, char* str) {
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    char macStr[18];
    macToString(mac_addr, macStr);
    Serial.print("Sent to: ");
    Serial.println(macStr);
    Serial.print("Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success!" : "Fail");
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);

    // Add PWM receivers (Channel 1)
    for (int i = 0; i < NUM_PWM_RECEIVERS; i++) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, pwmReceiverMacs[i], 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;

        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            char macStr[18];
            macToString(pwmReceiverMacs[i], macStr);
            Serial.print("Failed to add PWM receiver: ");
            Serial.println(macStr);
        }
    }

    // Add relay receiver (Channel 2)
    esp_now_peer_info_t relayPeer = {};
    memcpy(relayPeer.peer_addr, relayReceiverMac, 6);
    relayPeer.channel = 2;
    relayPeer.encrypt = false;
    
    if (esp_now_add_peer(&relayPeer) != ESP_OK) {
        char macStr[18];
        macToString(relayReceiverMac, macStr);
        Serial.print("Failed to add relay receiver: ");
        Serial.println(macStr);
    }

    Serial.println("\n--- ESP-NOW Dual Channel Controller ---");
    Serial.println("Ready for commands:");
    Serial.println("  PWM: m{1-10} {0-127}");
    Serial.println("  Relay: n{XXXX} (hex mask)");
}

void broadcastPWM(uint8_t cc, uint8_t value) {
    pwmMsg.cc_number = cc;
    pwmMsg.cc_value = value;
    
    Serial.print("Broadcasting CC");
    Serial.print(cc);
    Serial.print(" = ");
    Serial.println(value);
    Serial.println();
    
    for (int i = 0; i < NUM_PWM_RECEIVERS; i++) {
        esp_err_t result = esp_now_send(pwmReceiverMacs[i], 
                                      (uint8_t *)&pwmMsg, 
                                      sizeof(pwmMsg));
        if (result != ESP_OK) {
            char macStr[18];
            macToString(pwmReceiverMacs[i], macStr);
            Serial.print("Failed to send to: ");
            Serial.println(macStr);
        }
        delay(5);  // Small delay between transmissions
    }
}

void sendRelayTrigger(uint16_t relayMask) {
    relayMsg.relay_mask[0] = relayMask & 0xFF;
    relayMsg.relay_mask[1] = (relayMask >> 8) & 0xFF;
    
    Serial.print("Sending relay mask: 0x");
    Serial.println(relayMask, HEX);
    Serial.println();
    
    esp_err_t result = esp_now_send(relayReceiverMac, 
                                   (uint8_t *)&relayMsg, 
                                   sizeof(relayMsg));
    
    if (result != ESP_OK) {
        char macStr[18];
        macToString(relayReceiverMac, macStr);
        Serial.print("Failed to send to relay board: ");
        Serial.println(macStr);
    }
}

void processCommand(String cmd) {
    if (cmd.startsWith("m")) {  // PWM command
        cmd = cmd.substring(1);
        cmd.trim();
        
        int spaceIndex = cmd.indexOf(' ');
        if (spaceIndex == -1) return;
        
        int cc = cmd.substring(0, spaceIndex).toInt();
        int value = cmd.substring(spaceIndex + 1).toInt();
        
        if (cc >= 1 && cc <= 10 && value >= 0 && value <= 127) {
            broadcastPWM(cc, value);
        }
    }
    else if (cmd.startsWith("n")) {  // Relay command
        if (cmd.length() >= 5) {
            String hexMask = cmd.substring(1);
            uint16_t relayMask = strtol(hexMask.c_str(), NULL, 16);
            sendRelayTrigger(relayMask);
        }
    }
}

void loop() {
    // LED heartbeat
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink >= 1000) {
        lastBlink = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    // Process serial commands
    if (Serial.available()) {
        String cmd = "";
        while (Serial.available() > 0) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (cmd.length() > 0) {
                    processCommand(cmd);
                }
                cmd = "";
            } else {
                cmd += c;
            }
        }
        
        if (cmd.length() > 0) {
            processCommand(cmd);
        }
    }
}