/*
 * Shared parameter IDs, patch defaults and eight original three-oscillator sounds.
 * ID ordering is used by MIDI, UI and oscillator helpers; change them together.
 * Startup master is 50%; the signal ceiling is a sample amplitude, not amplifier
 * watts. PolySynth reserves fixed headroom for three voices. Preset selection
 * preserves master volume through the control layer. Configuration lives in RAM.
 */

#pragma once
#include <cstddef>
#include <cstdint>

enum class Waveform : uint8_t { Square, Saw, Triangle, Sine };
enum class Parameter : uint8_t { Attack, Decay, Sustain, Release, Waveform, Volume, Preset,
    Osc1Level, Osc1Coarse, Osc1Fine,
    Osc2Wave, Osc2Level, Osc2Coarse, Osc2Fine,
    Osc3Wave, Osc3Level, Osc3Coarse, Osc3Fine, Count };

constexpr std::size_t PARAMETER_COUNT = static_cast<std::size_t>(Parameter::Count);

// Quiet startup level during hardware bring-up; still adjustable from the menu.
constexpr float DEFAULT_VOLUME = 0.50f;
constexpr int16_t DEFAULT_SIGNAL_AMPLITUDE = 7200; // Incremental +6 dB from 3600; keep 50% startup master.

struct OscillatorConfig {
    Waveform waveform = Waveform::Saw;
    float level = 0;
    int coarse = 0;
    float fine = 0; // cents
};

struct SynthConfig {
    float attackMs = 5.0f;
    float decayMs = 50.0f;
    float sustain = 0.7f;
    float releaseMs = 200.0f;
    float volume = DEFAULT_VOLUME;
    Waveform waveform = Waveform::Square; // Oscillator 1; legacy MIDI target.
    float osc1Level = 1;
    int osc1Coarse = 0;
    float osc1Fine = 0;
    OscillatorConfig osc2{};
    OscillatorConfig osc3{Waveform::Sine, 0, 0, 0};
    unsigned preset = 0;
    bool custom = false;
};


// Original patches, inspired by three-oscillator synthesis; no FL presets copied.
constexpr unsigned SOUND_COUNT = 8;
inline const char *soundName(unsigned index) {
    static constexpr const char *names[] = {
        "Init Square", "Soft Sine", "Sub Bass", "Detuned Saw",
        "Warm Pad", "Organ", "Pluck", "Hollow Lead"
    };
    return index < SOUND_COUNT ? names[index] : "Unknown";
}
inline SynthConfig soundPreset(unsigned index, float volume = DEFAULT_VOLUME) {
    SynthConfig c;
    c.volume = volume;
    c.preset = index < SOUND_COUNT ? index : 0;
    switch (c.preset) {
        case 1: c.waveform = Waveform::Sine; c.attackMs = 15; c.releaseMs = 350; break;
        case 2:
            c.waveform = Waveform::Triangle;
            c.osc2 = {Waveform::Sine, 0.75f, -12, 0};
            c.attackMs = 5; c.decayMs = 180; c.sustain = 0.5f; c.releaseMs = 100; break;
        case 3:
            c.waveform = Waveform::Saw;
            c.osc2 = {Waveform::Saw, 1, 0, -8};
            c.osc3 = {Waveform::Saw, 1, 0, 8}; break;
        case 4:
            c.waveform = Waveform::Triangle;
            c.osc2 = {Waveform::Saw, 0.35f, 0, -7};
            c.osc3 = {Waveform::Sine, 0.6f, 12, 7};
            c.attackMs = 450; c.decayMs = 700; c.sustain = 0.75f; c.releaseMs = 1400; break;
        case 5:
            c.waveform = Waveform::Sine;
            c.osc2 = {Waveform::Sine, 0.65f, 12, 0};
            c.osc3 = {Waveform::Sine, 0.3f, 19, 0};
            c.attackMs = 5; c.decayMs = 30; c.sustain = 1; c.releaseMs = 90; break;
        case 6:
            c.waveform = Waveform::Saw;
            c.osc2 = {Waveform::Triangle, 0.5f, 12, 3};
            c.attackMs = 3; c.decayMs = 350; c.sustain = 0; c.releaseMs = 150; break;
        case 7:
            c.osc2 = {Waveform::Square, 0.45f, 12, -5};
            c.osc3 = {Waveform::Triangle, 0.5f, -12, 0};
            c.attackMs = 10; c.decayMs = 120; c.sustain = 0.6f; c.releaseMs = 300; break;
        default: break;
    }
    return c;
}

// Shared mappings keep menu, MIDI, validation and audio oscillator IDs aligned.
inline int oscillatorIndex(Parameter p) {
    const unsigned v = static_cast<unsigned>(p);
    if (p == Parameter::Waveform || (v >= 7 && v <= 9)) return 0;
    if (v >= 10 && v <= 13) return 1;
    if (v >= 14 && v <= 17) return 2;
    return -1;
}
inline int oscillatorField(Parameter p) { // shape=0, level=1, coarse=2, fine=3
    if (p == Parameter::Waveform) return 0;
    const int osc = oscillatorIndex(p);
    return osc < 0 ? -1 : osc == 0 ? static_cast<int>(p) - 6
                                         : (static_cast<int>(p) - 10) % 4;
}
inline OscillatorConfig oscillatorConfig(const SynthConfig &c, unsigned index) {
    return index == 0 ? OscillatorConfig{c.waveform, c.osc1Level, c.osc1Coarse, c.osc1Fine}
                     : index == 1 ? c.osc2 : c.osc3;
}
inline void setOscillatorConfig(SynthConfig &c, unsigned index, const OscillatorConfig &o) {
    if (index == 0) {
        c.waveform = o.waveform; c.osc1Level = o.level;
        c.osc1Coarse = o.coarse; c.osc1Fine = o.fine;
    } else if (index == 1) c.osc2 = o;
    else c.osc3 = o;
}
