#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Pin definitions for CC 1-10
const int CC_PINS[] = {
    26,  // CC1
    27,  // CC2
    33,  // CC3
    32,  // CC4
    25,  // CC5
    14,  // CC6
    12,  // CC7
    13,  // CC8
    15,  // CC9
    4    // CC10
};
const int NUM_PINS = 10;

// LED indicator
const int LED_PIN = 2;

// Message structure matching sender's PWM format
typedef struct {
    uint8_t cc_number;     // CC 1-10
    uint8_t cc_value;      // 0-127
} pwm_message;

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
    pwm_message incomingMsg;
    memcpy(&incomingMsg, data, sizeof(incomingMsg));
    
    // Validate CC number
    if (incomingMsg.cc_number >= 1 && incomingMsg.cc_number <= NUM_PINS) {
        int pinIndex = incomingMsg.cc_number - 1;  // Convert CC (1-10) to array index (0-9)
        
        // Map MIDI value (0-127) to PWM range (0-255)
        int pwmValue = map(incomingMsg.cc_value, 0, 127, 0, 255);
        
        // Set PWM value
        analogWrite(CC_PINS[pinIndex], pwmValue);
        
        // Debug output
        Serial.print("CC");
        Serial.print(incomingMsg.cc_number);
        Serial.print(" (Pin ");
        Serial.print(CC_PINS[pinIndex]);
        Serial.print(") = ");
        Serial.println(pwmValue);
        
        // Visual feedback
        digitalWrite(LED_PIN, LOW);
        delay(1);
        digitalWrite(LED_PIN, HIGH);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Configure LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    // Configure PWM pins
    for (int i = 0; i < NUM_PINS; i++) {
        pinMode(CC_PINS[i], OUTPUT);
        analogWrite(CC_PINS[i], 0);  // Start with all pins at 0
    }

    // Initialize ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Set to channel 1 for PWM communication
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);

    Serial.println("\n--- ESP-NOW PWM Receiver ---");
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.println("Ready to receive PWM commands on channel 1");
    
    // Startup test sequence
    Serial.println("Running test sequence...");
    for (int value = 0; value <= 255; value += 51) {
        for (int pin = 0; pin < NUM_PINS; pin++) {
            analogWrite(CC_PINS[pin], value);
        }
        delay(100);
    }
    // Reset all to 0
    for (int pin = 0; pin < NUM_PINS; pin++) {
        analogWrite(CC_PINS[pin], 0);
    }
}

void loop() {
    // Just keep LED on to show we're running
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink >= 1000) {
        lastBlink = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
}