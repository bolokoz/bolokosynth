#ifndef SYNTH_H
#define SYNTH_H

#include <Arduino.h>
#include "AudioTools.h"
#include "AudioTools/CoreAudio/AudioEffects/SoundGenerator.h"
#include "AudioTools/CoreAudio/AudioEffects/AudioEffects.h"

// ----------------------------------------------------------------------
// Custom generators / adapters
// ----------------------------------------------------------------------

// audio-tools ships Sine/Square/Saw/Noise but no Triangle.
// Naive triangle wave (-1..1 from phase 0..1). No band-limiting (same as
// upstream Square/Saw), acceptable for monophonic synth use.
template <typename T>
class TriangleWaveGenerator : public SoundGenerator<T> {
public:
    void setFrequency(float f) override {
        freq = f;
        phaseInc = f / this->audioInfo().sample_rate;
    }

    T readSample() override {
        float s = (phase < 0.5f) ? (-1.0f + 4.0f * phase)
                                 : ( 3.0f - 4.0f * phase);
        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;
        return (T)(s * 32767.0f);
    }

private:
    float freq     = 440.0f;
    float phase    = 0.0f;
    float phaseInc = 0.0f;
};

// Delegates readSample/setFrequency to the currently active child generator,
// so waveform can be switched at runtime without rebuilding the stream graph.
template <typename T>
class SwitchableGenerator : public SoundGenerator<T> {
public:
    void setChildren(SoundGenerator<T>** c, int n) {
        children = c;
        count    = n;
    }

    void setActive(int i) {
        if (i >= 0 && i < count) active = i;
    }

    int getActive() const { return active; }

    void setFrequency(float f) override {
        for (int i = 0; i < count; i++) {
            if (children[i]) children[i]->setFrequency(f);
        }
    }

    T readSample() override {
        if (active < 0 || active >= count || children[active] == nullptr) {
            return 0;
        }
        return children[active]->readSample();
    }

private:
    SoundGenerator<T>** children = nullptr;
    int count = 0;
    int active = 0;
};

// ----------------------------------------------------------------------
// One polyphonic voice: oscillator (4 switchable waveforms) + ADSR envelope.
// ----------------------------------------------------------------------
class Voice {
public:
    // True when voice produces no sound (ADSR fully released / never started).
    bool isIdle() const { return !adsr.isActive(); }

    // Wire up child generator pointers + initialize all generators with AudioInfo.
    // Must be called once before any keyOn.
    void setup(AudioInfo info) {
        children[0] = &tri;
        children[1] = &sine;
        children[2] = &square;
        children[3] = &saw;
        osc.setChildren(children, 4);
        tri.begin(info);
        sine.begin(info);
        square.begin(info);
        saw.begin(info);
        osc.begin(info);
        osc.setActive(0);
        osc.setFrequency(440.0f);
    }

    void keyOn(uint8_t midiNote, float freq) {
        note = midiNote;
        osc.setFrequency(freq);
        adsr.keyOn();
        gate = true;
    }

    void keyOff() {
        adsr.keyOff();
        gate = false;
    }

    void setWaveform(int w) { osc.setActive(w); }

    // Generate one sample. Returns 0 if idle (caller may also gate via isIdle).
    int16_t readSample() {
        if (isIdle()) return 0;
        int16_t s = osc.readSample();
        return adsr.process(s);
    }

    int8_t note = -1;   // MIDI note number, -1 = unassigned
    bool   gate = false; // true while note held

private:
    TriangleWaveGenerator<int16_t>  tri;
    SineFromTable<int16_t>          sine;
    SquareWaveGenerator<int16_t>    square;
    SawToothGenerator<int16_t>      saw;
    SoundGenerator<int16_t>*        children[4] = {nullptr, nullptr, nullptr, nullptr};
    SwitchableGenerator<int16_t>    osc;
    // A=5ms D=50ms S=0.7 R=200ms — same envelope as previous mono Synth.
    ADSRGain adsr{0.005f, 0.05f, 0.7f, 0.2f};
};

// ----------------------------------------------------------------------
// Polyphonic engine: fixed pool of voices, simple allocator + summer.
// Inherits SoundGenerator<int16_t> so it can feed GeneratedSoundStream.
// ----------------------------------------------------------------------
class PolySynthEngine : public SoundGenerator<int16_t> {
public:
    static const int NUM_VOICES = 8;

    bool begin(AudioInfo info) override {
        SoundGenerator<int16_t>::begin(info);
        this->info = info;
        for (int i = 0; i < NUM_VOICES; i++) voices[i].setup(info);
        return true;
    }

    // Mix all active voices. Divide by active count to normalize loudness
    // (one voice is as loud as eight). Clip to int16 range for safety.
    int16_t readSample() override {
        int32_t sum   = 0;
        int     count = 0;
        for (int i = 0; i < NUM_VOICES; i++) {
            if (!voices[i].isIdle()) {
                sum += voices[i].readSample();
                count++;
            }
        }
        if (count == 0) return 0;
        int32_t avg = sum / count;
        if (avg >  32767) avg =  32767;
        if (avg < -32768) avg = -32768;
        return (int16_t)avg;
    }

    Voice voices[NUM_VOICES];

private:
    AudioInfo info;
};

// ----------------------------------------------------------------------
// Synth facade
// Public API preserves the previous surface (WebManager unaffected) and
// adds polyphony via note-aware noteOn/noteOff.
// ----------------------------------------------------------------------

enum Waveform {
    WAVE_TRIANGLE = 0,
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_SAWTOOTH
};

class Synth {
public:
    Synth(int sampleRate = 44100)
        : info(sampleRate, 2, 16),
          outStream(engine) {}

    // Initializes all voices with the synth's AudioInfo.
    void begin() { engine.begin(info); }

    // ---- Public API (compatible with WebManager) ----------------------

    // Software volume (0-100). Applied at codec HW level by main.cpp via
    // out.setVolume(). Stored here as state for getVolume().
    void setVolume(int vol) { volume = constrain(vol, 0, 100); }

    // Switch waveform on ALL voices (current and future).
    void setWaveform(int wave) {
        if (wave >= 0 && wave <= 3) {
            for (int i = 0; i < PolySynthEngine::NUM_VOICES; i++) {
                engine.voices[i].setWaveform(wave);
            }
            currentWave = wave;
        }
    }

    int getVolume() const   { return volume; }
    int getWaveform() const { return currentWave; }

    // ---- Polyphonic note API (new) ------------------------------------

    // Allocate a voice for this note. If all voices busy, steal the oldest
    // non-held voice; if all held, steal voice 0.
    void noteOn(uint8_t note, float freq) {
        int idx = findFreeVoice();
        if (idx < 0) idx = findStealVoice();
        if (idx >= 0) engine.voices[idx].keyOn(note, freq);
        lastNote = note;
    }

    // Release the (first) voice currently holding this note. No-op if no
    // voice matches. Note Off during the release tail is a safe no-op.
    void noteOff(uint8_t note) {
        for (int i = 0; i < PolySynthEngine::NUM_VOICES; i++) {
            if (engine.voices[i].note == note && engine.voices[i].gate) {
                engine.voices[i].keyOff();
                return;
            }
        }
    }

    // Panic: release every voice.
    void allOff() {
        for (int i = 0; i < PolySynthEngine::NUM_VOICES; i++) {
            engine.voices[i].keyOff();
        }
    }

    // ---- Diagnostics for display --------------------------------------

    int   getActiveVoices() const {
        int c = 0;
        for (int i = 0; i < PolySynthEngine::NUM_VOICES; i++) {
            if (!engine.voices[i].isIdle()) c++;
        }
        return c;
    }
    int8_t getLastNote() const { return lastNote; }

    // ---- Stream access for main.cpp's StreamCopy ----------------------

    AudioInfo    getInfo()         { return info; }
    AudioStream& getOutputStream() { return outStream; }

private:
    AudioInfo info;
    PolySynthEngine              engine;
    GeneratedSoundStream<int16_t> outStream;

    int    volume      = 80;
    int    currentWave = WAVE_TRIANGLE;
    int8_t lastNote    = -1;

    // Returns index of an idle voice, or -1 if all busy.
    int findFreeVoice() const {
        for (int i = 0; i < PolySynthEngine::NUM_VOICES; i++) {
            if (engine.voices[i].isIdle()) return i;
        }
        return -1;
    }

    // Voice stealing: prefer voices in release tail (gate not held).
    // If all voices are held, steal voice 0 (FIFO-ish, simple).
    int findStealVoice() const {
        for (int i = 0; i < PolySynthEngine::NUM_VOICES; i++) {
            if (!engine.voices[i].gate) return i;
        }
        return 0;
    }
};

#endif
