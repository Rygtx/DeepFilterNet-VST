#pragma once

#include <JuceHeader.h>
#include <optional>

namespace dfvst
{
struct RuntimeAssetPaths
{
    juce::File rootDirectory;
    juce::File dfDll;
    juce::File modelArchive;
};

class EmbeddedRuntimeAssets
{
public:
    static std::optional<RuntimeAssetPaths> ensureExtracted();
};
}
