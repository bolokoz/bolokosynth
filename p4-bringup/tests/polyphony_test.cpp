/*
 * Three-pitch spectral coexistence, independent releases, stealing and mix bounds.
 * Native assert-based executable; run without NDEBUG so checks remain enabled.
 * No USB/display hardware is exercised; on-board listening/UI checks are separate.
 */

#include "../src/PolySynth.h"
#include <cassert>
#include <cmath>
#include <iostream>
static void advance(PolySynth &s, unsigned count) {
    for (unsigned i = 0; i < count; ++i) s.nextSample();
}
int main() {
    SynthConfig c; c.waveform = Waveform::Sine;
    c.attackMs = c.decayMs = 0; c.sustain = 1; c.volume = 1; c.releaseMs = 100;
    PolySynth s; s.configure(c);
    s.noteOn(0, 60, 127); s.noteOn(0, 64, 127); s.noteOn(0, 67, 127);
    assert(s.activeVoiceCount() == 3 && s.heldCount() == 3);
    // Frequency-selective measurements prove three pitches coexist, rather
    // than simply counting allocated voices while only the last one sounds.
    double real[3]{}, imag[3]{};
    const unsigned notes[] = {60,64,67};
    advance(s, 1000);
    for (unsigned frame = 0; frame < 48000; ++frame) {
        const int sample = s.nextSample();
        assert(std::abs(sample) <= DEFAULT_SIGNAL_AMPLITUDE);
        for (unsigned i = 0; i < 3; ++i) {
            const double f = 440 * std::pow(2.0, (int(notes[i])-69)/12.0);
            const double phase = 6.283185307179586 * f * frame / 48000;
            real[i] += sample * std::cos(phase); imag[i] += sample * std::sin(phase);
        }
    }
    for (unsigned i = 0; i < 3; ++i)
        assert(std::hypot(real[i],imag[i]) / 48000 > 1000);
    s.noteOff(0,64); assert(s.activeVoiceCount()==3 && s.heldCount()==2);
    advance(s,4801);
    assert(!s.hasVoice(0,64) && s.hasVoice(0,60) && s.hasVoice(0,67));
    // Releasing slots are stolen before keys that are still held.
    s.noteOn(0,64,127); s.noteOff(0,64); s.noteOn(0,69,127);
    assert(s.hasVoice(0,60) && s.hasVoice(0,67) && s.hasVoice(0,69));
    s.noteOn(0,72,127);
    assert(!s.hasVoice(0,60) && s.hasVoice(0,72));
    s.noteOff(0,60); assert(s.hasVoice(0,72)); // Stale stolen-key Note Off.
    s.noteOn(0,72,127); assert(s.activeVoiceCount()==3 && s.heldCount()==3);
    s.panic(); assert(s.heldCount()==0 && s.activeVoiceCount()==0 && s.nextSample()==0);
    // Same pitch on different MIDI channels remains independent.
    s.pitchBend(1,127,127);
    s.noteOn(0,69,127); s.noteOn(1,69,127);
    assert(s.heldCount()==2 && s.hasVoice(0,69) && s.hasVoice(1,69));
    assert(std::fabs(s.frequency()-440*std::pow(2.0f,2.0f/12))<.01f);
    s.allNotesOff(1,true); assert(s.hasVoice(0,69) && !s.hasVoice(1,69));
    s.noteOn(0,69,0); advance(s,4801); assert(s.activeVoiceCount()==0);
    s.noteOn(1,69,127); s.noteOff(1,69); s.allNotesOff(1,true);
    assert(s.nextSample()==0); // All Sound Off also kills release tails.
    s.resetControllers(1); s.noteOn(1,69,127); assert(std::fabs(s.frequency()-440)<.01f);
    s.panic(); s.noteOn(16,60,127); s.noteOn(0,128,127); s.noteOn(0,60,128);
    assert(s.heldCount()==0 && s.nextSample()==0);
    // Every patch, three voices, maximum master and live editing stay bounded.
    for (unsigned patch=0; patch<SOUND_COUNT; ++patch) {
        s.panic(); c=soundPreset(patch,1); s.configure(c);
        s.noteOn(0,48,127); s.noteOn(0,60,127); s.noteOn(0,72,127);
        for (unsigned i=0;i<12000;++i) assert(std::abs(int(s.nextSample()))<=DEFAULT_SIGNAL_AMPLITUDE);
        c.volume=0; s.configure(c); advance(s,1000); assert(s.nextSample()==0);
        s.panic();
    }
    std::cout << "Three simultaneous pitches, independent release/channels, stealing, stale offs, panic and mix bounds passed.\n";
}
