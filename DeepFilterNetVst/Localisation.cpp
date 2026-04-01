#include "Localisation.h"
#include "LocalisationBinaryData.h"

#include <vector>

namespace dfvst::localisation
{
namespace
{
struct LoadedLanguage
{
    LanguageOption option;
    juce::NamedValueSet strings;
};

juce::String getTextKey(TextId id)
{
    switch (id)
    {
        case TextId::subtitle:                     return "subtitle";
        case TextId::denoiseLabel:                 return "denoiseLabel";
        case TextId::postFilterLabel:              return "postFilterLabel";
        case TextId::reduceMaskLabel:              return "reduceMaskLabel";
        case TextId::reduceMaskChoiceIndependent:  return "reduceMaskChoiceIndependent";
        case TextId::reduceMaskChoiceMaximum:      return "reduceMaskChoiceMaximum";
        case TextId::reduceMaskChoiceMean:         return "reduceMaskChoiceMean";
        case TextId::diagnosticsButton:            return "diagnosticsButton";
        case TextId::diagnosticsWindowTitle:       return "diagnosticsWindowTitle";
        case TextId::diagnosticsHeading:           return "diagnosticsHeading";
        case TextId::unknown:                      return "unknown";
        case TextId::standalone:                   return "standalone";
        case TextId::localInstanceSection:         return "localInstanceSection";
        case TextId::sharedInstanceSection:        return "sharedInstanceSection";
        case TextId::localInstanceSuffix:          return "localInstanceSuffix";
        case TextId::instanceIdLabel:              return "instanceIdLabel";
        case TextId::processIdLabel:               return "processIdLabel";
        case TextId::hostLabel:                    return "hostLabel";
        case TextId::wrapperLabel:                 return "wrapperLabel";
        case TextId::prepareToPlayCountLabel:      return "prepareToPlayCountLabel";
        case TextId::processBlockCountLabel:       return "processBlockCountLabel";
        case TextId::releaseResourcesCountLabel:   return "releaseResourcesCountLabel";
        case TextId::lastPreparedLabel:            return "lastPreparedLabel";
        case TextId::lastProcessedLabel:           return "lastProcessedLabel";
        case TextId::currentSampleRateQueriedLabel:return "currentSampleRateQueriedLabel";
        case TextId::currentSampleRateLabel:       return "currentSampleRateLabel";
        case TextId::runtimeReadyLabel:            return "runtimeReadyLabel";
        case TextId::lastUpdatedLabel:             return "lastUpdatedLabel";
        case TextId::yes:                          return "yes";
        case TextId::no:                           return "no";
        case TextId::none:                         return "none";
    }

    jassertfalse;
    return {};
}

juce::String normaliseCodeForMatching(const juce::String& code)
{
    return code.trim().replaceCharacter('_', '-').toLowerCase();
}

class Catalogue final
{
public:
    Catalogue()
    {
        loadLanguages();
    }

    const std::vector<LanguageOption>& getLanguageOptions() const
    {
        return languageOptions_;
    }

    juce::String normaliseLanguageCode(const juce::String& code) const
    {
        if (languages_.empty())
            return {};

        const auto requested = normaliseCodeForMatching(code);

        if (requested.isEmpty())
            return {};

        if (const auto exactMatch = findExactOrRelatedLanguageCode(requested); exactMatch.isNotEmpty())
            return exactMatch;

        if (requested.startsWith("zh"))
        {
            if (const auto simplifiedChinese = findLanguageCodeByPrefix("zh-hans"); simplifiedChinese.isNotEmpty())
                return simplifiedChinese;

            if (const auto genericChinese = findLanguageCodeByPrefix("zh"); genericChinese.isNotEmpty())
                return genericChinese;
        }

        return {};
    }

    juce::String resolveSystemLanguage() const
    {
        if (const auto display = normaliseLanguageCode(juce::SystemStats::getDisplayLanguage()); display.isNotEmpty())
            return display;

        if (const auto user = normaliseLanguageCode(juce::SystemStats::getUserLanguage()); user.isNotEmpty())
            return user;

        if (const auto english = normaliseLanguageCode("en"); english.isNotEmpty())
            return english;

        return !languageOptions_.empty() ? languageOptions_.front().code : juce::String();
    }

    juce::String translate(TextId id, const juce::String& languageCode) const
    {
        const auto key = juce::Identifier(getTextKey(id));

        if (const auto* exactLanguage = findLanguage(languageCode))
        {
            if (const auto translated = exactLanguage->strings[key].toString(); translated.isNotEmpty())
                return translated;
        }

        if (const auto* englishLanguage = findLanguage("en"))
        {
            if (const auto fallback = englishLanguage->strings[key].toString(); fallback.isNotEmpty())
                return fallback;
        }

        for (const auto& language : languages_)
        {
            if (const auto fallback = language.strings[key].toString(); fallback.isNotEmpty())
                return fallback;
        }

        return getTextKey(id);
    }

private:
    juce::String findExactOrRelatedLanguageCode(const juce::String& requested) const
    {
        for (const auto& language : languages_)
        {
            if (normaliseCodeForMatching(language.option.code) == requested)
                return language.option.code;
        }

        for (const auto& language : languages_)
        {
            const auto candidate = normaliseCodeForMatching(language.option.code);
            if (candidate.startsWith(requested + "-") || requested.startsWith(candidate + "-"))
                return language.option.code;
        }

        return {};
    }

    juce::String findLanguageCodeByPrefix(const juce::String& prefix) const
    {
        const auto normalisedPrefix = normaliseCodeForMatching(prefix);

        for (const auto& language : languages_)
        {
            const auto candidate = normaliseCodeForMatching(language.option.code);
            if (candidate == normalisedPrefix || candidate.startsWith(normalisedPrefix + "-"))
                return language.option.code;
        }

        return {};
    }

    const LoadedLanguage* findLanguage(const juce::String& languageCode) const
    {
        const auto normalisedCode = normaliseLanguageCode(languageCode);
        if (normalisedCode.isEmpty())
            return nullptr;

        for (const auto& language : languages_)
        {
            if (language.option.code == normalisedCode)
                return &language;
        }

        return nullptr;
    }

    void loadLanguages()
    {
        for (int index = 0; index < DfvstLocalisationData::namedResourceListSize; ++index)
        {
            int dataSize = 0;
            if (const auto* data = DfvstLocalisationData::getNamedResource(DfvstLocalisationData::namedResourceList[index], dataSize))
            {
                const auto parsed = juce::JSON::parse(juce::String::fromUTF8(data, dataSize));
                if (const auto* object = parsed.getDynamicObject())
                    addLanguage(*object);
            }
        }

        if (languages_.empty())
        {
            jassertfalse;
            languages_.push_back({});
            languages_.back().option.code = "en";
            languages_.back().option.displayName = "English";
            languageOptions_.push_back(languages_.back().option);
        }
    }

    void addLanguage(const juce::DynamicObject& object)
    {
        const auto code = object.getProperty("code").toString().trim();
        const auto displayName = object.getProperty("displayName").toString().trim();
        const auto stringsValue = object.getProperty("strings");
        const auto* stringsObject = stringsValue.getDynamicObject();

        if (code.isEmpty() || displayName.isEmpty() || stringsObject == nullptr)
            return;

        LoadedLanguage language;
        language.option.code = code;
        language.option.displayName = displayName;

        for (const auto& property : stringsObject->getProperties())
            language.strings.set(property.name, property.value.toString());

        languages_.push_back(language);
        languageOptions_.push_back(language.option);
    }

    std::vector<LoadedLanguage> languages_;
    std::vector<LanguageOption> languageOptions_;
};

const Catalogue& getCatalogue()
{
    static const Catalogue catalogue;
    return catalogue;
}
} // namespace

const std::vector<LanguageOption>& getAvailableLanguages()
{
    return getCatalogue().getLanguageOptions();
}

juce::String tr(TextId id, const juce::String& languageCode)
{
    return getCatalogue().translate(id, languageCode);
}

juce::StringArray getReduceMaskUiChoices(const juce::String& languageCode)
{
    return {
        tr(TextId::reduceMaskChoiceIndependent, languageCode),
        tr(TextId::reduceMaskChoiceMaximum, languageCode),
        tr(TextId::reduceMaskChoiceMean, languageCode)
    };
}

juce::String normaliseLanguageCode(const juce::String& code)
{
    return getCatalogue().normaliseLanguageCode(code);
}

juce::String resolveSystemLanguage()
{
    return getCatalogue().resolveSystemLanguage();
}
} // namespace dfvst::localisation
