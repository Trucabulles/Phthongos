#pragma once
#include "../analysis/Dynamics.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>
namespace phthongos::services {
struct VerificationFrame {double time=0,rms=0,flatness=0,transient=0;std::vector<float> amplitudes,prominence;double amp(int,int)const;double tonal(int)const;};
struct VerificationGrid {unsigned sampleRate=0;double start=0,end=0;std::vector<VerificationFrame> frames;std::size_t nearest(double)const;};
VerificationGrid verificationSpectrum(const analysis::StereoPcm&,unsigned,double,double,const std::atomic_bool&);
juce::var verifyMidi(const VerificationGrid&,const analysis::StereoPcm&,unsigned,const std::vector<Note>&,
                     const std::vector<NoteId>&,std::optional<std::pair<int,int>>,const std::atomic_bool&,const std::vector<NoteId>* completeIds=nullptr);
}
