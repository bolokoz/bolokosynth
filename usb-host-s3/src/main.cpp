#include <Arduino.h>
#include "EspUsbHost.h"

// UART pins to communicate with AudioKit
// Connect GPIO 17 (TX) to AudioKit RX
// Connect GPIO 18 (RX) to AudioKit TX
#define MIDI_TX_PIN 17
#define MIDI_RX_PIN 18

class MidiHost : public EspUsbHost {
public:
    // Override onData to receive USB transfer data
    // This assumes the library successfully enumerates the MIDI device
    // and submits transfers for its endpoints.
    // If the device is strictly MIDI Class (not HID), EspUsbHost might need
    // adjustment or a specific MIDI driver.
    void onData(const usb_transfer_t *transfer) override {
        if (transfer->status == 0 && transfer->actual_num_bytes > 0) {
            uint8_t *data = transfer->data_buffer;
            size_t len = transfer->actual_num_bytes;

            // USB MIDI packets are 32-bit (4 bytes)
            for (size_t i = 0; i < len; i += 4) {
                if (i + 4 <= len) {
                    uint8_t cin = data[i] & 0x0F;
                    uint8_t m0 = data[i+1];
                    uint8_t m1 = data[i+2];
                    uint8_t m2 = data[i+3];

                    // Filter for Note On (0x9), Note Off (0x8), Control Change (0xB)
                    if (cin == 0x8 || cin == 0x9 || cin == 0xB) {
                         // Forward Standard MIDI message to UART
                         Serial1.write(m0);
                         Serial1.write(m1);
                         Serial1.write(m2);

                         Serial.printf("MIDI: Status=%02X D1=%02X D2=%02X\n", m0, m1, m2);
                    }
                }
            }
        }
    }

    // Note: If the device is not detected, check if it presents as HID or Audio Class.
    // EspUsbHost is optimized for HID but exposes generic USB Host capabilities.
    // If your MIDI controller is not detected, you may need to use a dedicated USB Host MIDI library
    // or ensure EspUsbHost is configured to accept Class 0x01 (Audio/MIDI).
};

MidiHost usbHost;

void setup() {
    Serial.begin(115200);
    // Initialize UART for MIDI output to AudioKit
    // Standard MIDI baud rate is 31250
    Serial1.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

    Serial.println("Starting USB Host...");
    usbHost.begin();
}

void loop() {
    usbHost.task();
}
