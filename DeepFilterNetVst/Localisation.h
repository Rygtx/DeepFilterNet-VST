#pragma once

#include <JuceHeader.h>
#include <vector>

namespace dfvst::localisation
{
enum class TextId
{
    subtitle,
    denoiseLabel,
    postFilterLabel,
    reduceMaskLabel,
    reduceMaskChoiceIndependent,
    reduceMaskChoiceMaximum,
    reduceMaskChoiceMean,
    diagnosticsButton,
    diagnosticsWindowTitle,
    diagnosticsHeading,
    unknown,
    standalone,
    localInstanceSection,
    sharedInstanceSection,
    localInstanceSuffix,
    instanceIdLabel,
    processIdLabel,
    hostLabel,
    wrapperLabel,
    prepareToPlayCountLabel,
    processBlockCountLabel,
    releaseResourcesCountLabel,
    lastPreparedLabel,
    lastProcessedLabel,
    currentSampleRateQueriedLabel,
    currentSampleRateLabel,
    runtimeReadyLabel,
    lastUpdatedLabel,
    yes,
    no,
    none
};

struct LanguageOption
{
    juce::String code;
    juce::String displayName;
};

const std::vector<LanguageOption>& getAvailableLanguages();
juce::String tr(TextId id, const juce::String& languageCode);
juce::StringArray getReduceMaskUiChoices(const juce::String& languageCode);
juce::String normaliseLanguageCode(const juce::String& code);
juce::String resolveSystemLanguage();
} // namespace dfvst::localisation
