#ifndef SYNTH_H
#define SYNTH_H

#include <Arduino.h>
#include <cmath>

enum Waveform {
    WAVE_TRIANGLE = 0,
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_SAWTOOTH
};

class Synth {
public:
    Synth(int sampleRate = 44100) : sample_rate(sampleRate), frequency(440.0f), volume(80), waveform(WAVE_TRIANGLE), note_on(false), phase(0.0f) {
        updatePhaseInc();
    }

    void setFrequency(float freq) {
        frequency = freq;
        updatePhaseInc();
    }

    void setVolume(int vol) {
        volume = constrain(vol, 0, 100);
    }

    void setWaveform(int wave) {
        if (wave >= 0 && wave <= 3) {
            waveform = (Waveform)wave;
        }
    }

    void noteOn(float freq) {
        setFrequency(freq);
        note_on = true;
    }

    void noteOff() {
        note_on = false;
    }

    int16_t getSample() {
        if (!note_on) {
            return 0;
        }

        float sample_f = 0.0f;

        switch (waveform) {
            case WAVE_TRIANGLE:
                if (phase < 1.0f) {
                    sample_f = -1.0f + 2.0f * phase;
                } else {
                    sample_f = 1.0f - 2.0f * (phase - 1.0f);
                }
                break;
            case WAVE_SINE:
                // phase is 0..2. sin expects 0..2PI
                sample_f = sinf(phase * PI);
                break;
            case WAVE_SQUARE:
                sample_f = (phase < 1.0f) ? 1.0f : -1.0f;
                break;
            case WAVE_SAWTOOTH:
                // -1 to 1. phase goes 0..2
                sample_f = -1.0f + phase;
                break;
        }

        // Advance phase
        phase += phase_inc;
        if (phase >= 2.0f) phase -= 2.0f;

        // Apply volume (0-100). Base amplitude 10000 provides headroom.
        return (int16_t)(sample_f * 10000.0f * (volume / 100.0f));
    }

    int getVolume() const { return volume; }
    int getWaveform() const { return (int)waveform; }

private:
    int sample_rate;
    float frequency;
    int volume; // 0-100
    Waveform waveform;
    bool note_on;
    float phase;
    float phase_inc;

    void updatePhaseInc() {
        phase_inc = (frequency * 2.0f) / (float)sample_rate;
    }
};

#endif
