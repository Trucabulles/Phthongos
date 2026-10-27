#pragma once
#include "../project/BasicPitch.h"
#include <functional>
namespace phthongos::services {
class BasicPitchInference {
public:
 explicit BasicPitchInference(juce::File runtime);
 ~BasicPitchInference();
 bool available()const;
 basicPitch::Cache run(const juce::File&,std::atomic_bool&,const std::function<void(float)>&);
private:
 struct State;std::unique_ptr<State> state;juce::File runtime;
};
}
