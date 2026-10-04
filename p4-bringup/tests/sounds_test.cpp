/*
 * Oscillator pitch, distinct patches, normalized output and preset/edit behavior.
 * Native assert-based executable; run without NDEBUG so checks remain enabled.
 * No USB/display hardware is exercised; on-board listening/UI checks are separate.
 */

#include "../src/MonoSynth.h"
#include "../src/SynthControls.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

static void advance(MonoSynth &s, int frames) {
    for (int i = 0; i < frames; ++i) s.nextSample();
}

int main() {
    // Listen numerically to each isolated oscillator, including octave/fine tuning.
    for (unsigned osc = 0; osc < 3; ++osc) {
        SynthConfig c;
        c.attackMs = c.decayMs = 0; c.sustain = 1; c.volume = 1;
        c.osc1Level = 0;
        const int coarse = osc == 0 ? -12 : osc == 1 ? 0 : 12;
        setOscillatorConfig(c, osc, {Waveform::Sine, 1, coarse, 17});
        MonoSynth voice;
        voice.configure(c); voice.noteOn(0, 69, 127);
        advance(voice, 1000);
        const float expected = 440 * std::pow(2.0f, (coarse + .17f) / 12);
        assert(std::fabs(voice.oscillatorFrequency(osc) - expected) < .01f);
        int crossings = 0, previous = voice.nextSample();
        for (int i = 0; i < 48000; ++i) {
            const int next = voice.nextSample();
            crossings += previous <= 0 && next > 0;
            assert(std::abs(next) <= 7200);
            previous = next;
        }
        assert(std::fabs(crossings - expected) < 2);
    }

    uint64_t signatures[SOUND_COUNT]{};
    for (unsigned patch = 0; patch < SOUND_COUNT; ++patch) {
        SynthConfig c = soundPreset(patch);
        MonoSynth voice;
        voice.configure(c); voice.noteOn(0, 60, 127);
        for (int i = 0; i < 48000; ++i) {
            const auto sample = voice.nextSample();
            assert(std::abs(sample) <= 3600); // 50% startup master, including three-oscillator mixes.
            signatures[patch] = signatures[patch] * 1099511628211ULL + static_cast<uint16_t>(sample);
        }
        assert(signatures[patch] != 0);
        voice.noteOff(0, 60);
        advance(voice, static_cast<int>(c.releaseMs * 48) + 2);
        for (int i = 0; i < 1000; ++i) assert(voice.nextSample() == 0);
    }
    for (unsigned a = 0; a < SOUND_COUNT; ++a)
        for (unsigned b = a + 1; b < SOUND_COUNT; ++b) assert(signatures[a] != signatures[b]);

    MonoSynth voice;
    SynthConfig mixed = soundPreset(3, 1);
    voice.configure(mixed); voice.noteOn(0, 127, 127);
    for (unsigned i = 0; i < 3; ++i) assert(voice.oscillatorFrequency(i) <= 21600);
    advance(voice, 4800);
    mixed.osc1Level = mixed.osc2.level = mixed.osc3.level = 0;
    voice.configure(mixed);
    advance(voice, 1000);
    for (int i = 0; i < 1000; ++i) assert(voice.nextSample() == 0);

    // Coarse/fine encoder increments and boundaries for all oscillator rows.
    SynthControls controls;
    for (unsigned i = 7; i < PARAMETER_COUNT; ++i) {
        const auto parameter = static_cast<Parameter>(i);
        const int field = oscillatorField(parameter);
        assert(controls.setNormalized(parameter, .5f) || field == 2 || field == 3);
        controls.adjust(parameter, 1);
        const auto o = oscillatorConfig(controls.config(), oscillatorIndex(parameter));
        if (field == 2) assert(o.coarse == 1);
        if (field == 3) assert(o.fine == 1);
        controls.setNormalized(parameter, 0);
        controls.setNormalized(parameter, 1);
        const auto maximum = oscillatorConfig(controls.config(), oscillatorIndex(parameter));
        if (field == 0) assert(maximum.waveform == Waveform::Sine);
        if (field == 1) assert(maximum.level == 1);
        if (field == 2) assert(maximum.coarse == 24);
        if (field == 3) assert(maximum.fine == 100);
    }
    controls.setNormalized(Parameter::Volume, .13f);
    controls.beginLearn(Parameter::Osc2Fine, MidiControlMode::RelativeTwosComplement);
    assert(controls.applyCC(0, 10, 1) == ControlResult::Learned);
    assert(controls.adjust(Parameter::Preset, 3));
    assert(controls.config().preset == 3);
    assert(!controls.config().custom && controls.config().volume == .13f);
    assert(controls.binding(Parameter::Osc2Fine).assigned);
    assert(controls.applyCC(0, 10, 1) == ControlResult::Changed);
    assert(controls.config().osc2.fine == -7 && controls.config().custom);
    controls.setNormalized(Parameter::Preset, 3.5f / SOUND_COUNT);
    assert(!controls.config().custom && controls.config().osc2.fine == -8);

    // Shape pickup works independently on oscillator 2, and preset changes rearm pickup.
    controls.beginLearn(Parameter::Osc2Wave);
    assert(controls.applyCC(0, 11, 0) == ControlResult::Learned);
    assert(controls.applyCC(0, 11, 0) == ControlResult::Ignored);
    assert(controls.applyCC(0, 11, 40) == ControlResult::Ignored); // Same Saw, acquires pickup.
    assert(controls.binding(Parameter::Osc2Wave).pickedUp);
    assert(controls.applyCC(0, 11, 100) == ControlResult::Changed);
    assert(controls.config().osc2.waveform == Waveform::Sine);
    controls.adjust(Parameter::Preset, 1);
    assert(!controls.binding(Parameter::Osc2Wave).pickedUp);
    assert(controls.binding(Parameter::Osc2Wave).assigned);
    assert(controls.config().volume == .13f);

    auto bad = controls.config(); bad.osc2.fine = std::numeric_limits<float>::quiet_NaN();
    assert(!controls.setConfig(bad));
    bad = controls.config(); bad.osc3.coarse = 25; assert(!controls.setConfig(bad));
    bad = controls.config(); bad.osc1Level = 2; assert(!controls.setConfig(bad));
    bad = controls.config(); bad.preset = SOUND_COUNT; assert(!controls.setConfig(bad));

    std::cout << "Three-oscillator pitch/output, quiet distinct presets, release/mute, range and MIDI/preset preservation tests passed.\n";
}
