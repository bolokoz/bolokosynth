/*
 * Single-voice ADSR timing, release/overlap/channel semantics and sample bounds.
 * Native assert-based executable; run without NDEBUG so checks remain enabled.
 * No USB/display hardware is exercised; on-board listening/UI checks are separate.
 */

#include "../src/MonoSynth.h"
#include <cassert>
#include <cmath>
#include <iostream>

static void advance(MonoSynth &s, int samples) {
    for (int i = 0; i < samples; ++i) s.nextSample();
}
static void silence(MonoSynth &s) {
    assert(s.stage() == AdsrEnvelope::Stage::Idle);
    for (int i = 0; i < 512; ++i) assert(s.nextSample() == 0);
}

int main() {
    AdsrEnvelope envelope;
    assert(envelope.next() == 0);
    envelope.noteOn();
    for (int i = 0; i < 240; ++i) envelope.next();
    assert(envelope.level() == 1.0f);
    assert(envelope.stage() == AdsrEnvelope::Stage::Decay);
    for (int i = 0; i < 2400; ++i) envelope.next();
    assert(std::fabs(envelope.level() - 0.7f) < 0.00001f);
    assert(envelope.stage() == AdsrEnvelope::Stage::Sustain);
    envelope.noteOff();
    for (int i = 0; i < 9600; ++i) envelope.next();
    assert(envelope.stage() == AdsrEnvelope::Stage::Idle);
    assert(envelope.level() == 0);

    envelope.noteOn();
    for (int i = 0; i < 60; ++i) envelope.next();
    const float partial = envelope.level();
    envelope.noteOff();
    assert(envelope.level() == partial);
    assert(envelope.next() < partial);
    for (int i = 1; i < 9600; ++i) envelope.next();
    assert(envelope.level() == 0);
    envelope.noteOn();
    for (int i = 0; i < 240; ++i) envelope.next();
    envelope.noteOff();
    for (int i = 0; i < 400; ++i) envelope.next();
    const float tail = envelope.level();
    envelope.noteOn();
    assert(envelope.level() == tail);
    assert(envelope.next() > tail);

    MonoSynth synth;
    silence(synth);
    synth.noteOn(0, 69, 127);
    assert(std::fabs(synth.frequency() - 440) < 0.001);
    advance(synth, 2640);
    synth.noteOff(0, 69);
    assert(synth.stage() == AdsrEnvelope::Stage::Release);
    advance(synth, 9600);
    silence(synth);

    synth.noteOn(0, 60, 100);
    synth.noteOn(0, 64, 100);
    synth.noteOff(0, 60);
    assert(synth.currentNote() == 64 && synth.heldCount() == 1);
    assert(synth.stage() != AdsrEnvelope::Stage::Release);
    synth.noteOff(1, 64);
    assert(synth.heldCount() == 1);
    synth.panic();
    silence(synth);

    synth.noteOn(0, 60, 100);
    synth.noteOn(0, 64, 100);
    synth.noteOff(0, 64);
    assert(synth.currentNote() == 60);
    synth.noteOn(0, 60, 0);
    assert(synth.heldCount() == 0);
    advance(synth, 9600);
    silence(synth);

    synth.noteOn(0, 60, 100);
    synth.noteOn(1, 60, 100);
    synth.allNotesOff(1);
    assert(synth.heldCount() == 1 && synth.currentNote() == 60);
    synth.allNotesOff(0);
    advance(synth, 9600);
    silence(synth);
    synth.noteOn(0, 60, 100);
    synth.allNotesOff(0, true);
    silence(synth);
    synth.noteOn(0, 60, 100);
    advance(synth, 2640);
    synth.noteOff(0, 60);
    synth.allNotesOff(1, true);
    assert(synth.stage() == AdsrEnvelope::Stage::Release);
    synth.allNotesOff(0, true);
    silence(synth);

    synth.noteOn(0, 127, 127);
    int positive = 0, negative = 0;
    for (int i = 0; i < 4800; ++i) {
        const auto sample = synth.nextSample();
        positive += sample > 0;
        negative += sample < 0;
    }
    assert(positive > 100 && negative > 100);
    synth.panic();
    synth.noteOff(0, 127);
    silence(synth);
    // Nondefault stage timing, live sustain retargeting, and release settings.
    SynthConfig settings;
    settings.attackMs = 10;
    settings.decayMs = 100;
    settings.sustain = 0.6f;
    settings.releaseMs = 37;
    AdsrEnvelope edited(1000);
    edited.configure(settings);
    edited.noteOn();
    for (int i = 0; i < 10; ++i) edited.next();
    assert(edited.stage() == AdsrEnvelope::Stage::Decay);
    for (int i = 0; i < 25; ++i) edited.next();
    const float decayBefore = edited.level();
    settings.sustain = 0.2f;
    edited.configure(settings);
    assert(edited.level() == decayBefore);
    for (int i = 0; i < 74; ++i) edited.next();
    assert(edited.stage() == AdsrEnvelope::Stage::Decay);
    edited.next();
    assert(edited.stage() == AdsrEnvelope::Stage::Sustain);
    assert(std::fabs(edited.level() - 0.2f) < 0.00001f);
    settings.sustain = 0.8f;
    edited.configure(settings);
    for (int i = 0; i < 5; ++i) edited.next();
    assert(std::fabs(edited.level() - 0.8f) < 0.00001f);
    edited.noteOff();
    for (int i = 0; i < 36; ++i) edited.next();
    assert(edited.stage() == AdsrEnvelope::Stage::Release);
    edited.next();
    assert(edited.stage() == AdsrEnvelope::Stage::Idle);

    // Each waveform is bounded, bipolar, distinct, and silent after release.
    long signatures[4]{};
    for (unsigned wave = 0; wave < 4; ++wave) {
        MonoSynth voice;
        SynthConfig config;
        config.waveform = static_cast<Waveform>(wave);
        voice.configure(config);
        voice.noteOn(0, 69, 127);
        advance(voice, 3000);
        int pos = 0, neg = 0;
        for (int frame = 0; frame < 1000; ++frame) {
            const int value = voice.nextSample();
            assert(std::abs(value) <= 7200);
            pos += value > 0;
            neg += value < 0;
            signatures[wave] += std::abs(value);
        }
        assert(pos > 300 && neg > 300);
        voice.noteOff(0, 69);
        advance(voice, 9600);
        silence(voice);
    }
    for (unsigned a = 0; a < 4; ++a)
        for (unsigned b = a + 1; b < 4; ++b) assert(signatures[a] != signatures[b]);

    MonoSynth live;
    SynthConfig liveConfig;
    live.noteOn(0, 69, 127);
    advance(live, 3000);
    liveConfig.waveform = Waveform::Saw;
    live.configure(liveConfig);
    advance(live, 32);
    MonoSynth reference = live;
    liveConfig.waveform = Waveform::Sine;
    live.configure(liveConfig);
    liveConfig.waveform = Waveform::Triangle; // Two changes in the same audio block.
    live.configure(liveConfig);
    assert(live.nextSample() == reference.nextSample());
    liveConfig.volume = 0;
    live.configure(liveConfig);
    advance(live, 481);
    for (int i = 0; i < 512; ++i) assert(live.nextSample() == 0);
    liveConfig.volume = 1;
    live.configure(liveConfig);
    advance(live, 481);
    int nonzero = 0;
    for (int i = 0; i < 512; ++i) nonzero += live.nextSample() != 0;
    assert(nonzero > 400);
    live.panic();
    silence(live);

    std::cout << "ADSR timing, silence, overlap, channels, zero-velocity, panic, high notes, live ADSR, waveforms and volume tests passed.\n";
}
