#include <Arduino.h>
#include <TFT_eSPI.h>
#include "EspUsbHost.h"

TFT_eSPI tft = TFT_eSPI();

class MyEspUsbHost : public EspUsbHost {
    void onData(const usb_transfer_t *transfer) override {
        // Placeholder for data handling
    }
};

MyEspUsbHost usbHost;

void setup() {
    // Setup Serial for debugging (UART0)
    Serial.begin(115200);
    Serial.println("Starting ESP32-S2 USB Host...");

    // Setup TFT
    tft.init();
    tft.setRotation(1); // Adjust rotation as needed
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 0);
    tft.println("USB Host S2");
    tft.setTextSize(1);
    tft.println("Initializing...");

    // Setup USB Host
    // Note: This might fail if the board hardware setup isn't exactly as expected
    // or if the internal USB PHY is used differently.
    usbHost.begin();

    tft.println("Host Started");
}

void loop() {
    usbHost.task();

    // Simple blink/update on screen to show it's alive
    static uint32_t lastMillis = 0;
    if (millis() - lastMillis > 1000) {
        lastMillis = millis();
        tft.drawString("Alive: " + String(millis()/1000) + "s", 10, 60);
    }
}
