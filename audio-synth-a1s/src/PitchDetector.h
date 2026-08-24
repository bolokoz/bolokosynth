#ifndef PITCH_DETECTOR_H
#define PITCH_DETECTOR_H

#include <Arduino.h>
#include <Preferences.h>
#include "AudioTools.h"
#include "AudioTools/AudioLibs/AudioBoardStream.h"

// ----------------------------------------------------------------------
// PitchDetector
//
// Listens to the on-board microphone for ~2 seconds, runs a Goertzel
// sweep over 24 semitones (octaves 3 and 4 = 130.8Hz..493.9Hz), sums
// magnitudes into 12 pitch classes (C, C#, ..., B), picks the loudest.
//
// Triggered via MIDI CC (see WebManager::ccPitch). Because the AC101
// codec + I2S peripheral can't cleanly switch from TX_MODE back to
// TX_MODE after a RX_MODE capture on the fly, this implementation
// reboots the ESP32 at the end. The result is persisted in NVS and
// reloaded by setup() on the next boot, so the user experience is:
//
//   1. Press CC button      -> audio stops (~2s capture)
//   2. ESP reboots          (~3s)
//   3. Synth ready, OLED shows detected tone
//
// Octave is intentionally NOT detected (user only wants the tone).
// ----------------------------------------------------------------------

class PitchDetector {
public:
    struct Result {
        bool   valid       = false;       // false if signal below threshold
        int    pitchClass  = -1;          // 0..11 (C..B), -1 if invalid
        float  frequency   = 0.0f;        // canonical octave-3 freq, Hz
        String name        = "--";        // "C", "F#", ..., or "Low signal"
    };

    static const char* NOTE_NAMES[12];

    // 2 octaves of semitones: MIDI 48..71 (C3..B4).
    static const int   NUM_OCTAVES    = 2;
    static const int   BASE_MIDI      = 48;       // C3
    static const int   NUM_FREQS      = NUM_OCTAVES * 12;  // 24
    static const int   BLOCK_SIZE     = 2048;     // ~46ms at 44.1kHz
    static const int   CAPTURE_MS     = 2000;
    // Default ratio used if NVS has nothing stored. Tunable live via CC during
    // capture. Winning class must exceed average by this factor.
    static constexpr float DEFAULT_SIGNAL_RATIO = 3.0f;

    // ----------------------------------------------------------------
    // Runs the full capture. Mutes + ends `out`, opens a temporary
    // RX_MODE stream on the codec mic input, runs Goertzel for ~2s,
    // persists result + final ratio to NVS, then reboots.
    //
    // During the 2s capture window, MIDI CC messages on Serial2 are
    // parsed inline: if CC number == ccRatioNum, the knob value (0-127)
    // is mapped to ratio 1.0-10.0 and written to `liveRatio`. The final
    // validity test uses whatever ratio is current at end of capture.
    //
    // NEVER RETURNS.
    // ----------------------------------------------------------------
    static void detectPersistAndReboot(AudioBoardStream& out,
                                       AudioInfo info,
                                       int ccRatioNum,
                                       volatile float& liveRatio) {
        Serial.println(F("[PitchDetector] capture starting"));

        // Reset accumulator
        for (int i = 0; i < 12; i++) magnitudes_[i] = 0.0f;

        // Tear down TX path so we can re-claim the I2S peripheral for RX.
        out.end();

        // Set up RX path on the same codec + I2S, using the on-board mic.
        AudioBoardStream in(AudioKitAC101);
        auto cfg = in.defaultConfig(RX_MODE);
        cfg.copyFrom(info);
        cfg.sd_active    = false;
        cfg.input_device = ADC_INPUT_LINE2;   // on-board MEMS mic
        in.begin(cfg);

        // Configure Goertzel with 24 target frequencies.
        GoertzelStream goertzel;
        for (int i = 0; i < NUM_FREQS; i++) {
            int octave = i / 12;
            int cls    = i % 12;
            int midi   = BASE_MIDI + octave * 12 + cls;
            float freq = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
            freqs_[i]    = freq;
            indices_[i]  = i;
            goertzel.addFrequency(freq, &indices_[i]);
        }
        goertzel.setFrequencyDetectionCallback(&PitchDetector::goertzelCallback_);

        auto gcfg = goertzel.defaultConfig();
        gcfg.copyFrom(info);
        gcfg.block_size = BLOCK_SIZE;
        gcfg.threshold  = 0.0f;             // capture all magnitudes
        goertzel.begin(gcfg);

        // Drive samples from mic through Goertzel for CAPTURE_MS, while
        // also polling Serial2 for MIDI CC updates to liveRatio.
        StreamCopy copier(goertzel, in);
        uint8_t midi_state = 0;
        uint8_t midi_status = 0;
        uint8_t midi_d1     = 0;
        uint32_t start = millis();
        while (millis() - start < CAPTURE_MS) {
            copier.copy();

            // Inline MIDI parser — only handles CC for the ratio knob.
            while (Serial2.available()) {
                uint8_t b = Serial2.read();
                if (b >= 0x80) {            // status byte
                    midi_status = b;
                    midi_state  = 1;
                } else if (midi_state == 1) {
                    midi_d1     = b;
                    midi_state  = 2;
                } else if (midi_state == 2) {
                    if ((midi_status & 0xF0) == 0xB0   // CC
                        && midi_d1 == ccRatioNum) {
                        // Map 0..127 to 1.0..10.0
                        liveRatio = 1.0f + (b / 127.0f) * 9.0f;
                    }
                    midi_state = 0;
                }
            }
        }

        // Find winning pitch class + compute average for ratio test.
        int    bestClass = -1;
        float  bestMag   = 0.0f;
        float  total     = 0.0f;
        for (int c = 0; c < 12; c++) {
            total += magnitudes_[c];
            if (magnitudes_[c] > bestMag) {
                bestMag   = magnitudes_[c];
                bestClass = c;
            }
        }
        float avg = total / 12.0f;
        float ratio = avg > 0.0f ? bestMag / avg : 0.0f;
        Serial.printf("[PitchDetector] best class=%d mag=%.2f avg=%.2f ratio=%.2f (threshold=%.2f)\n",
                      bestClass, bestMag, avg, ratio, (float)liveRatio);

        // Persist the live-tuned ratio so it survives the reboot and is
        // available next time the user opens the web UI.
        Preferences pf;
        pf.begin("synth-config", false);
        pf.putFloat("signal_ratio", (float)liveRatio);
        pf.end();

        // Validity: winning class must beat average by current ratio.
        Result r;
        if (bestClass >= 0 && ratio >= (float)liveRatio) {
            int midi = BASE_MIDI + bestClass;          // C3 + offset
            r.valid      = true;
            r.pitchClass = bestClass;
            r.frequency  = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
            r.name       = String(NOTE_NAMES[bestClass]);
        } else {
            r.valid      = false;
            r.pitchClass = -1;
            r.frequency  = 0.0f;
            r.name       = F("Low signal");
        }

        // Persist result for next-boot display.
        Preferences prefs;
        prefs.begin("synth-config", false);
        prefs.putBool ("det_valid", r.valid);
        prefs.putInt  ("det_class", r.pitchClass);
        prefs.putFloat("det_freq",  r.frequency);
        prefs.putString("det_name",  r.name);
        prefs.end();

        Serial.println(F("[PitchDetector] rebooting"));
        delay(50);     // let Serial flush
        ESP.restart(); // never returns
    }

    // ----------------------------------------------------------------
    // Reads last detection result from NVS. Call once in setup().
    // Returns default-constructed invalid Result if none stored.
    // ----------------------------------------------------------------
    static Result loadFromNVS() {
        Result r;
        Preferences prefs;
        prefs.begin("synth-config", true);
        if (prefs.isKey("det_name")) {
            r.valid      = prefs.getBool("det_valid", false);
            r.pitchClass = prefs.getInt ("det_class", -1);
            r.frequency  = prefs.getFloat("det_freq", 0.0f);
            r.name       = prefs.getString("det_name", "--");
        }
        prefs.end();
        return r;
    }

private:
    // Static accumulator state (callback has no `this`).
    static float magnitudes_[12];
    static int   indices_[NUM_FREQS];
    static float freqs_[NUM_FREQS];

    static void goertzelCallback_(float frequency, float magnitude, void* ref) {
        if (ref == nullptr) return;
        int  idx = *static_cast<int*>(ref);
        int  cls = idx % 12;
        magnitudes_[cls] += magnitude;
    }
};

// Static definitions.
inline const char* PitchDetector::NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
inline float PitchDetector::magnitudes_[12] = {0};
inline int   PitchDetector::indices_[PitchDetector::NUM_FREQS] = {0};
inline float PitchDetector::freqs_[PitchDetector::NUM_FREQS]   = {0};

#endif
