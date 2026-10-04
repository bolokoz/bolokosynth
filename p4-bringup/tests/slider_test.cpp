/*
 * Pitch bend precision/channels, modulation depth and learned-control pickup.
 * Native assert-based executable; run without NDEBUG so checks remain enabled.
 * No USB/display hardware is exercised; on-board listening/UI checks are separate.
 */

#include "../src/MonoSynth.h"
#include "../src/SynthControls.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    MonoSynth s;
    auto c = soundPreset(3); c.attackMs=c.decayMs=0; c.sustain=1;
    s.configure(c);
    s.pitchBend(0,127,127); s.noteOn(0,69,127);
    assert(std::fabs(s.frequency()-440*std::pow(2.f,2.f/12))<.01f);
    for(int i=0;i<1000;++i)s.nextSample();
    const auto stage=s.stage(); const auto level=s.envelopeLevel();
    s.pitchBend(0,0,0);
    assert(std::fabs(s.frequency()-440*std::pow(2.f,-2.f/12))<.01f);
    assert(s.stage()==stage && s.envelopeLevel()==level);
    for(unsigned i=0;i<3;++i) {
        auto o=oscillatorConfig(c,i);
        assert(std::fabs(s.oscillatorFrequency(i)-s.frequency()*std::pow(2.f,(o.coarse+o.fine/100)/12))<.01f);
    }
    s.pitchBend(0,0,64); assert(std::fabs(s.frequency()-440)<.01f);
    s.pitchBend(0,1,64); assert(s.frequency()>440 && s.frequency()<440.1f);
    const float prior=s.frequency();
    assert(!s.pitchBend(16,0,0) && !s.pitchBend(0,128,0) && !s.pitchBend(0,0,128));
    s.pitchBend(1,0,0); assert(s.frequency()==prior);
    s.noteOn(1,69,127); assert(s.frequency()<440);
    s.noteOff(1,69); assert(s.frequency()==prior);
    s.noteOff(0,69); assert(s.stage()==AdsrEnvelope::Stage::Release);
    s.pitchBend(0,127,127); assert(s.frequency()>440);
    s.resetPitchBend(0); assert(std::fabs(s.frequency()-440)<.01f);
    assert(s.stage()==AdsrEnvelope::Stage::Release);
    for(int i=0;i<9601;++i)s.nextSample();
    assert(s.nextSample()==0);
    s.pitchBend(0,0,0); s.panic(); assert(s.bendSemitones(0)==0 && s.bendSemitones(1)==0);

    s.panic(); s.noteOn(0,69,127);
    assert(s.modulation(1,127));
    for(int i=0;i<1000;++i)s.nextSample();
    assert(std::fabs(s.vibratoSemitones())<.001f); // Different channel has no effect.
    assert(s.modulation(0,127));
    float max=0,min=0;
    for(int i=0;i<48000;++i) {
        const int sample=s.nextSample(); assert(std::abs(sample)<=3600);
        max=std::fmax(max,s.vibratoSemitones()); min=std::fmin(min,s.vibratoSemitones());
    }
    assert(max>.49f && max<=.501f && min<-.49f && min>=-.501f);
    s.resetControllers(0); assert(s.modulationValue(0)==0);
    for(int i=0;i<2000;++i)s.nextSample();
    assert(std::fabs(s.vibratoSemitones())<.001f);
    assert(!s.modulation(16,1) && !s.modulation(0,128));
    s.panic(); assert(s.modulationValue(1)==0 && s.nextSample()==0);

    SynthControls controls;
    controls.setLearnMode(MidiControlMode::RelativeOffset16);
    controls.beginSliderLearn();
    assert(controls.learnMode()==MidiControlMode::Absolute && controls.learningParameter()==Parameter::Volume);
    assert(controls.applyCC(0,7,0)==ControlResult::Learned);
    assert(controls.config().volume==DEFAULT_VOLUME);
    assert(controls.hasBinding(0,7) && !controls.hasBinding(1,7));
    assert(controls.applyCC(0,7,1)==ControlResult::Ignored);
    assert(controls.applyCC(0,7,32)==ControlResult::Ignored);
    assert(controls.applyCC(0,7,64)==ControlResult::Changed);
    assert(controls.applyCC(0,7,0)==ControlResult::Changed && controls.config().volume==0);
    assert(controls.applyCC(0,7,127)==ControlResult::Changed && controls.config().volume==1);
    controls.beginLearn(Parameter::Attack);
    controls.applyCC(0,10,17);
    const float attack=controls.config().attackMs;
    assert(controls.applyCC(0,10,17)==ControlResult::Ignored);
    assert(controls.setBindingMode(Parameter::Attack,MidiControlMode::RelativeOffset16));
    assert(controls.applyCC(0,10,17)==ControlResult::Changed);
    assert(controls.config().attackMs>attack);
    assert(controls.applyCC(0,10,0)==ControlResult::Ignored);
    assert(controls.applyCC(0,10,15)==ControlResult::Changed);
    assert(controls.binding(Parameter::Attack).cc==10);
    assert(!controls.setBindingMode(Parameter::Count,MidiControlMode::Absolute));
    std::cout<<"Pitch slider range/precision/channels/tails, volume pickup, and correcting learned relative-knob mode passed.\n";
}
