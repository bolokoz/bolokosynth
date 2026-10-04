/*
 * Historical S3 MAX/USB-host bring-up: continuous quiet square test tone.
 * Note On changes pitch; Note Off deliberately does not stop this diagnostic.
 * This is not the active P4 v1 synth. The tested board's serial-port cold-start
 * issue remains electrically unresolved; see reset-startup.md for the observed
 * ROM-mode software-reset workaround. Never infer speaker/USB wiring health
 * from an LED or successful peripheral initialization alone.
 */

#include <Arduino.h>
#include <math.h>
#include "EspUsbHost.h"
#include "ESP_I2S.h"

EspUsbHost usb;
I2SClass i2s;
bool ledOn = false;

// These match the wiring currently made on the board:
// GPIO11 -> MAX98357A BCLK, GPIO12 -> LRC/LRCLK, GPIO13 -> DIN.
constexpr int I2S_BCLK = 11;
constexpr int I2S_LRC = 12;
constexpr int I2S_DIN = 13;
constexpr uint32_t SAMPLE_RATE = 8000;
constexpr int16_t TEST_AMPLITUDE = 900;
// USB callback supplies the latest note pitch; loop snapshots it per buffer.
// volatile provides visibility for this small scalar, not a general lock.
volatile uint16_t toneHz = 440;

// RGB_BUILTIN is an addressable LED, so digitalWrite would not select blue.
// This diagnostic toggles on Note On; it is not a connection-verification LED.
static void setTestLed(bool on)
{
    ledOn = on;
    // The DevKitC-1 onboard LED is an addressable RGB LED.
    rgbLedWrite(RGB_BUILTIN, 0, 0, ledOn ? 64 : 0);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    setTestLed(false);

    Serial.println("MAX98357A I2S + USB MIDI test starting...");
    Serial.printf("I2S pins: BCLK=%d LRC=%d DIN=%d\n",
                  I2S_BCLK, I2S_LRC, I2S_DIN);

    // Bind the made wiring before starting stereo I2S. Neither initialization
    // nor this error loop proves that the MAX or passive speaker is connected.
    i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
    if (!i2s.begin(I2S_MODE_STD,
                   SAMPLE_RATE,
                   I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO)) {
        Serial.println("ERROR: I2S initialization failed");
        while (true) {
            setTestLed(true);
            delay(100);
            setTestLed(false);
            delay(100);
        }
    }
    Serial.println("I2S ready. You should hear a 440 Hz test tone.");

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
        Serial.print("USB connected: ");
        espUsbHostPrint(device);
    });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
        Serial.print("USB disconnected: ");
        espUsbHostPrint(device);
    });

    // Historical behavior intentionally ignores Note Off: a continuously
    // sounding test tone makes power/startup faults easier to observe.
    usb.onMidiMessage([](const EspUsbHostMidiMessage &message) {
        const uint8_t type = message.status & 0xF0;

        // Change pitch and toggle once for a real Note On.
        if (type == 0x90 && message.data2 != 0) {
            setTestLed(!ledOn);
            // Equal-tempered MIDI conversion, A4(note69)=440Hz. Integer-Hz
            // rounding and the8kHz sample rate limit this diagnostic's fidelity.
            toneHz = static_cast<uint16_t>(roundf(
                440.0f * powf(2.0f, (static_cast<int>(message.data1) - 69) / 12.0f)));
            Serial.printf("NOTE ON: note=%u velocity=%u LED=%s tone=%u Hz\n",
                          message.data1,
                          message.data2,
                          ledOn ? "ON" : "OFF",
                          toneHz);
        }
    });

    if (!usb.begin()) {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    } else {
        Serial.println("USB host ready. Press a MIDI note to change pitch.");
    }
}

void loop()
{
    // Generate a continuous low-volume square-wave test tone. The tone
    // changes pitch when a MIDI Note On arrives.
    // Persist phase across buffers to avoid restarting each128-frame chunk.
    // Duplicate the one waveform into both slots because MAX selects one slot.
    static float phase = 0.0f;
    int16_t frames[128 * 2];
    const float increment = static_cast<float>(toneHz) / SAMPLE_RATE;

    for (size_t i = 0; i < 128; ++i) {
        const int16_t sample = phase < 0.5f ? TEST_AMPLITUDE : -TEST_AMPLITUDE;
        frames[i * 2] = sample;
        frames[i * 2 + 1] = sample;
        phase += increment;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
    // I2S consumption paces the continuous test; no delay is added. This
    // legacy path does not check short writes as the active P4 firmware does.
    i2s.write(reinterpret_cast<const uint8_t *>(frames), sizeof(frames));
}
