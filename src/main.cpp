#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <JustWifi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <DebounceEvent.h>
#include "credentials.h"

// =============================================================================
// Configuration
// =============================================================================

#define SERIAL_BAUDRATE                 115200

// OTA
#define HOSTNAME                        "XMASTREE"
#define OTA_PORT                        8266
#define OTA_PASS                        "fibonacci"

// LED strip
#define DEFAULT_BRIGHTNESS              128
#define MAX_BRIGHTNESS                  255
#define TOTAL_PIXELS                    41
#define TOTAL_MODES                     5

// mode config
#define RAINBOW_SPEED                   50
#define FADE_SPEED                      10
#define TWINKLE_SPEED                   200
#define TWINKLE_COLOR                   strip.Color(0, 0, 255)

// modes
#define MODE_OFF                        0
#define MODE_RAINBOW                    1
#define MODE_RAINBOW_CYCLE              2
#define MODE_FADE                       3
#define MODE_TWINKLE                    4
#define DEFAULT_MODE                    MODE_RAINBOW_CYCLE

// pin definitions
#define PIN_LED                         2
#define PIN_BUTTON_MODE                 13
#define PIN_LEDSTRIP                    5

// =============================================================================
// Instances
// =============================================================================

unsigned char mode = DEFAULT_MODE;
unsigned int brightness = DEFAULT_BRIGHTNESS;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(TOTAL_PIXELS, PIN_LEDSTRIP, NEO_RGB + NEO_KHZ800);
DebounceEvent button = DebounceEvent(PIN_BUTTON_MODE);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// =============================================================================
// Modes
// =============================================================================

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
unsigned long Wheel(unsigned short WheelPos) {

    if (WheelPos < 85) {
        return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
    } else if (WheelPos < 170) {
        WheelPos -= 85;
        return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
    } else {
        WheelPos -= 170;
        return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
    }

}

void setAll(unsigned short red, unsigned short green, unsigned short blue) {
    for (unsigned long i = 0; i < strip.numPixels(); i++ ) {
        strip.setPixelColor(i, strip.Color(red, green, blue));
    }
}

void setAll(unsigned long color) {
    for (unsigned long i = 0; i < strip.numPixels(); i++ ) {
        strip.setPixelColor(i, color);
    }
}


void modeTwinkle() {

    static unsigned long last = 0;
    if (millis() - last < TWINKLE_SPEED) return;
    last = millis();

    unsigned short i = random(strip.numPixels());
    if (strip.getPixelColor(i) == 0) {
        strip.setPixelColor(i, TWINKLE_COLOR);
    } else {
        strip.setPixelColor(i, 0);
    }

    strip.setBrightness(brightness-1);
    strip.show();

}

void modeFade() {

    static unsigned short position = 0;
    static int direction = 1;
    static unsigned short color = 0;

    static unsigned long last = 0;
    if (millis() - last < FADE_SPEED) return;
    last = millis();

    position += direction;
    if ((position == 254) || (position == 0)) direction = -direction;
    if (position == 0) color = (color + 1) % 3;

    switch (color) {
        case 0: setAll(position, 0, 0); break;
        case 1: setAll(0, position, 0); break;
        case 2: setAll(0, 0, position); break;
    }

    strip.setBrightness(brightness-1);
    strip.show();

}

void modeRainbow() {

    static unsigned long last = 0;
    if (millis() - last < RAINBOW_SPEED) return;
    last = millis();

    static unsigned long count = 0;
    uint32_t color;

    color = Wheel(count);
    setAll(color);

    strip.setBrightness(brightness-1);
    strip.show();
    count = (count + 1) % 256;

}

void modeRainbowCycle() {

    static unsigned long last = 0;
    if (millis() - last < RAINBOW_SPEED) return;
    last = millis();

    static unsigned long count = 0;
    uint32_t color;

    for (byte i=0; i<strip.numPixels(); i++) {
        color = Wheel((i * 256 / strip.numPixels() + count) & 255);
        strip.setPixelColor(strip.numPixels()-i-1, color);
    }

    strip.setBrightness(brightness-1);
    strip.show();
    count = (count + 1) % 256;

}

void stripLoop() {

    static byte previous_mode = 0xFF;

    if (mode != previous_mode) {
        setAll(0);
        Serial.printf("[LEDS] Mode set to %d\n", mode);
    }

    switch (mode) {
        case MODE_RAINBOW:
            modeRainbow();
            break;
        case MODE_RAINBOW_CYCLE:
            modeRainbowCycle();
            break;
        case MODE_FADE:
            modeFade();
            break;
        case MODE_TWINKLE:
            modeTwinkle();
            break;
        default:
            if (mode != previous_mode) {
                strip.clear();
                strip.show();
            }
        break;
    }

    previous_mode = mode;

}

// =============================================================================
// Helpers
// =============================================================================

void blink(unsigned long delayOff, unsigned long delayOn) {
    static unsigned long next = millis();
    static bool status = HIGH;
    if (next < millis()) {
        status = !status;
        digitalWrite(PIN_LED, status);
        next += ((status) ? delayOff : delayOn);
    }
}

void showStatus() {
    if (jw.connected()) {
        blink(2000, 2000);
    } else {
        blink(500, 500);
    }
}

void setMode(uint8_t new_mode) {
    if (mode != new_mode) {
        new_mode = new_mode % TOTAL_MODES;
        mode = new_mode;
        Serial.printf("Mode %d\n", mode);
    }
}

void increaseBrightness() {
    brightness *= 2;
    if (brightness > MAX_BRIGHTNESS) brightness = 32;
    Serial.printf("Brightness %d\n", brightness);
}

// =============================================================================
// Main
// =============================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  
    Serial.print("[MQTT] Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    int new_mode = payload[0] - '0';
    if ((0 <= new_mode) && (new_mode < TOTAL_MODES)) {
        mode = new_mode;
    }

}

void mqttSetup() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
}

void mqttLoop() {
  
    // Skip if no wifi
    if (!jw.connected()) return;

    // Connect if not connected
    if (!mqttClient.connected()) {
        
        static unsigned long last = millis();
        if (millis() - last > 5000) {
            last = millis();

            Serial.println("[MQTT] Attempting MQTT connection...");
            
            // Create a random client ID
            String clientId = "ESP8266Client-";
            clientId += String(random(0xffff), HEX);
            
            // Attempt to connect
            if (mqttClient.connect(clientId.c_str())) {
                
                Serial.println("[MQTT] Connected");
                mqttClient.publish(MQTT_STATUS_TOPIC, "1");
                mqttClient.subscribe(MQTT_MODE_TOPIC);
            
            } else {
                
                Serial.print("[MQTT] Failed, rc=");
                Serial.print(mqttClient.state());
                Serial.println(" try again in 5 seconds");

            }
        
        }
    
    }

    if (mqttClient.connected()) {
        mqttClient.loop();
    }

}

void wifiSetup() {

    jw.enableScan(true);
    jw.setHostname(HOSTNAME);
    #ifdef WIFI1_SSID
        jw.addNetwork(WIFI1_SSID, WIFI1_PASS);
    #endif
    #ifdef WIFI2_SSID
        jw.addNetwork(WIFI2_SSID, WIFI2_PASS);
    #endif
    jw.subscribe([](justwifi_messages_t code, char * parameter) {

        if (code == MESSAGE_SCANNING) {
            Serial.printf("[WIFI] Scanning\n");
        }

        if (code == MESSAGE_SCAN_FAILED) {
            Serial.printf("[WIFI] Scan failed\n");
        }

        if (code == MESSAGE_NO_NETWORKS) {
            Serial.printf("[WIFI] No networks found\n");
        }

        if (code == MESSAGE_NO_KNOWN_NETWORKS) {
            Serial.printf("[WIFI] No known networks found\n");
        }

        if (code == MESSAGE_FOUND_NETWORK) {
            Serial.printf("[WIFI] %s\n", parameter);
        }

        if (code == MESSAGE_CONNECTING) {
            Serial.printf("[WIFI] Connecting to %s\n", parameter);
        }

        if (code == MESSAGE_CONNECT_WAITING) {
            // too much noise
        }

        if (code == MESSAGE_CONNECT_FAILED) {
            Serial.printf("[WIFI] Could not connect to %s\n", parameter);
        }

        if (code == MESSAGE_CONNECTED) {

            Serial.printf("[WIFI] MODE STA -------------------------------------\n");
            Serial.printf("[WIFI] SSID %s\n", WiFi.SSID().c_str());
            Serial.printf("[WIFI] IP   %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("[WIFI] MAC  %s\n", WiFi.macAddress().c_str());
            Serial.printf("[WIFI] GW   %s\n", WiFi.gatewayIP().toString().c_str());
            Serial.printf("[WIFI] MASK %s\n", WiFi.subnetMask().toString().c_str());
            Serial.printf("[WIFI] DNS  %s\n", WiFi.dnsIP().toString().c_str());
            Serial.printf("[WIFI] HOST %s\n", WiFi.hostname().c_str());
            Serial.printf("[WIFI] ----------------------------------------------\n");

            if (MDNS.begin((char *) WiFi.hostname().c_str())) {
                MDNS.addService("http", "tcp", 80);
                Serial.printf("[MDNS] OK\n");
            } else {
                Serial.printf("[MDNS] FAIL\n");
            }

        }

        if (code == MESSAGE_DISCONNECTED) {
            Serial.printf("[WIFI] Disconnected\n");
        }

    });

}

void otaSetup() {

    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASS);

    ArduinoOTA.onStart([]() {
        Serial.printf("[OTA] Start\n");
    });

    ArduinoOTA.onEnd([]() {
        Serial.printf("\n[OTA] End\n");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        #if DEBUG_PORT
            Serial.printf("\n[OTA] Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.printf("Auth Failed\n");
            else if (error == OTA_BEGIN_ERROR) Serial.printf("Begin Failed\n");
            else if (error == OTA_CONNECT_ERROR) Serial.printf("Connect Failed\n");
            else if (error == OTA_RECEIVE_ERROR) Serial.printf("Receive Failed\n");
            else if (error == OTA_END_ERROR) Serial.printf("End Failed\n");
        #endif
    });

    ArduinoOTA.begin();

}


void stripSetup() {

    // Start display and initialize all to OFF
    strip.begin();
    strip.show();

}

void setup() {  

    // Init serial port and clean garbage
    Serial.begin(SERIAL_BAUDRATE);
    Serial.println();
    Serial.println();

    // Initialise random number generation
    randomSeed(analogRead(0));

    // MQTT
    mqttSetup();

    // Wifi
    wifiSetup();

    // OTA
    otaSetup();

    // LED strip
    stripSetup();

}

void loop() {

    // Wifi
    jw.loop();

    // MQTT
    mqttLoop();

    // OTA
    ArduinoOTA.handle();

    // Button
    if (button.loop()) {
        if (button.getEvent() == EVENT_LONG_CLICK) increaseBrightness();
        if (button.getEvent() == EVENT_SINGLE_CLICK) setMode(mode + 1);
    }

    // LEDs
    stripLoop();

    // Status
    showStatus();

    yield();

}
