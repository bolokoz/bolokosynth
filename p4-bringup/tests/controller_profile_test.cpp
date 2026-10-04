/*
 * Exact MiniLab identity/defaults, click edges and generic-profile fallback.
 * Native assert-based executable; run without NDEBUG so checks remain enabled.
 * No USB/display hardware is exercised; on-board listening/UI checks are separate.
 */

#include "../src/ControllerProfiles.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    SynthControls c;
    c.config().volume=.25f;
    assert(profileForUsb(0x1c75,0x0289)==ControllerProfile::MiniLabMkII);
    assert(profileForUsb(0x1c75,0x9999)==ControllerProfile::Generic);
    loadControllerProfile(c,ControllerProfile::MiniLabMkII);
    bool seen[128]{}, targets[PARAMETER_COUNT]{};
    for(const auto &k: miniLabKnobs()) {
        assert(!seen[k.cc]); seen[k.cc]=true;
        const auto b=c.binding(k.parameter);
        assert(b.assigned && b.cc==k.cc && b.mode==k.mode && b.channel==0);
        assert(!targets[static_cast<unsigned>(k.parameter)]); targets[static_cast<unsigned>(k.parameter)]=true;
    }
    assert(c.applyCC(0,112,64)==ControlResult::Ignored);
    assert(c.applyCC(0,112,1)==ControlResult::Changed && c.config().preset==1);
    assert(c.applyCC(0,112,65)==ControlResult::Changed && c.config().preset==0);
    assert(c.applyCC(0,112,0)==ControlResult::Ignored);
    assert(c.applyCC(1,112,1)==ControlResult::Ignored);
    const auto wave=c.config().osc3.waveform;
    assert(c.applyCC(0,114,1)==ControlResult::Changed);
    assert(c.config().osc3.waveform!=wave);
    assert(c.applyCC(0,114,65)==ControlResult::Changed && c.config().osc3.waveform==wave);
    // ABS defaults still protect the quiet master through pickup.
    assert(c.applyCC(0,72,127)==ControlResult::Ignored && c.config().volume==.25f);
    assert(c.applyCC(0,72,0)==ControlResult::Changed && c.config().volume==0);
    assert(c.applyCC(0,72,127)==ControlResult::Changed && c.config().volume==1);
    assert(c.hasBinding(0,112) && !c.hasBinding(0,1)); // CC1 retains vibrato.
    assert(profileMode(ControllerProfile::MiniLabMkII,112,MidiControlMode::Absolute)==MidiControlMode::RelativeSignMagnitude);
    c.beginLearn(Parameter::Attack,profileMode(ControllerProfile::MiniLabMkII,112,MidiControlMode::Absolute));
    assert(c.applyCC(0,112,64)==ControlResult::Learned);
    assert(!c.binding(Parameter::Preset).assigned);
    const float attack=c.config().attackMs;
    assert(c.applyCC(0,112,1)==ControlResult::Changed && c.config().attackMs>attack);
    assert(c.applyCC(0,112,65)==ControlResult::Changed);
    assert(c.binding(Parameter::Attack).mode==MidiControlMode::RelativeSignMagnitude);
    ControllerButtons buttons;
    assert(buttons.apply(ControllerProfile::MiniLabMkII,0,113,127)==ControllerAction::ReloadSound);
    assert(buttons.apply(ControllerProfile::MiniLabMkII,0,113,127)==ControllerAction::None);
    assert(buttons.apply(ControllerProfile::MiniLabMkII,0,113,0)==ControllerAction::None);
    assert(buttons.apply(ControllerProfile::MiniLabMkII,0,113,127)==ControllerAction::ReloadSound);
    assert(buttons.apply(ControllerProfile::MiniLabMkII,0,115,127)==ControllerAction::Panic);
    assert(buttons.apply(ControllerProfile::Generic,0,113,127)==ControllerAction::None);
    assert(buttons.apply(ControllerProfile::MiniLabMkII,1,113,127)==ControllerAction::None);
    loadControllerProfile(c,ControllerProfile::Generic);
    for(unsigned i=0;i<PARAMETER_COUNT;++i) assert(!c.binding(static_cast<Parameter>(i)).assigned);
    assert(c.config().volume==1); // Profile changes only mappings.
    c.beginLearn(Parameter::Sustain);
    assert(c.applyCC(3,42,64)==ControlResult::Learned);
    assert(c.binding(Parameter::Sustain).channel==3 && c.binding(Parameter::Sustain).cc==42);
    std::cout<<"MiniLab identity/default maps, signed relative knobs, clicks, learn override and generic controller tests passed.\n";
}
