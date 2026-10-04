/*
 * Legacy S2 display/USB-host scaffold, separate from the active P4 v1 firmware.
 * The receive callback is a placeholder; this program does not implement the
 * complete MIDI-to-synth bridge. TFT pins in platformio.ini are tentative and
 * require the exact board schematic. A host-start message is not proof of MIDI
 * enumeration or adequate VBUS power.
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "EspUsbHost.h"

TFT_eSPI tft = TFT_eSPI();

// This scaffold receives USB transfers but does not yet parse/forward MIDI.
// Transfer data belongs to the host stack; later implementations must copy
// anything needed beyond the callback rather than keep the raw pointer.
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
    // TFT_eSPI takes driver/pin choices from platformio.ini build flags.
    // Those tentative values must match the physical S2/TTGO variant.
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
    // Only request host startup here. The return value is not checked in
    // this scaffold, so the text below must not be read as confirmed success.
    usbHost.begin();

    tft.println("Host Started");
}

void loop() {
    // Pump the legacy host API regularly so transfers/enumeration progress.
    // Unlike P4, this target is just a host/display experiment.
    usbHost.task();

    // Simple blink/update on screen to show it's alive
    // Redraw only the heartbeat once per second instead of continuously
    // clearing the TFT. Unsigned elapsed time tolerates millis wraparound.
    static uint32_t lastMillis = 0;
    if (millis() - lastMillis > 1000) {
        lastMillis = millis();
        tft.drawString("Alive: " + String(millis()/1000) + "s", 10, 60);
    }
}
