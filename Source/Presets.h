#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include "Shape.h"

namespace anvil
{

struct Preset
{
    juce::String name;
    juce::String category;
    std::vector<std::pair<juce::String, float>> values;            // param id -> real value
    std::vector<std::pair<int, std::vector<ShapePoint>>> shapes;    // module -> shape
};

const std::vector<Preset>& factoryPresets();

} // namespace anvil
