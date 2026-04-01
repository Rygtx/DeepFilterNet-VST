#include "PluginEditor.h"

#include <utility>
#include <vector>

namespace
{
using dfvst::localisation::TextId;

const auto backgroundTop = juce::Colour::fromRGB(10, 16, 28);
const auto backgroundBottom = juce::Colour::fromRGB(4, 8, 16);
const auto backgroundGlow = juce::Colour::fromRGB(20, 54, 96);
const auto panelColour = juce::Colour::fromRGB(18, 28, 44);
const auto panelInner = juce::Colour::fromRGB(24, 37, 58);
const auto panelOutline = juce::Colour::fromRGB(54, 76, 108);
const auto panelOutlineStrong = juce::Colour::fromRGB(86, 120, 166);
const auto accent = juce::Colour::fromRGB(255, 167, 76);
const auto accentBright = juce::Colour::fromRGB(255, 202, 132);
const auto accentSoft = juce::Colour::fromRGB(92, 58, 31);
const auto textStrong = juce::Colour::fromRGB(237, 243, 252);
const auto textMuted = juce::Colour::fromRGB(145, 165, 194);
const auto textSubtle = juce::Colour::fromRGB(104, 126, 158);
const auto shadowColour = juce::Colour::fromRGBA(0, 0, 0, 84);

int getLanguageChoiceId(const juce::String& languageCode)
{
    const auto normalisedCode = dfvst::localisation::normaliseLanguageCode(languageCode);
    const auto& languages = dfvst::localisation::getAvailableLanguages();

    for (size_t index = 0; index < languages.size(); ++index)
    {
        if (languages[index].code == normalisedCode)
            return static_cast<int>(index) + 1;
    }

    return languages.empty() ? 0 : 1;
}

juce::String getLanguageCodeForChoiceId(int selectedId)
{
    const auto index = selectedId - 1;
    const auto& languages = dfvst::localisation::getAvailableLanguages();

    if (index < 0 || index >= static_cast<int>(languages.size()))
        return {};

    return languages[static_cast<size_t>(index)].code;
}

juce::String getLanguageBadgeText(const juce::String& languageCode)
{
    const auto normalisedCode = dfvst::localisation::normaliseLanguageCode(languageCode);

    if (normalisedCode.startsWithIgnoreCase("zh"))
        return "ZH";

    return "EN";
}

class DiagnosticContent final : public juce::Component,
                                private juce::Timer
{
public:
    static constexpr int minPreferredWidth = 480;
    static constexpr int maxPreferredWidth = 1000;

    explicit DiagnosticContent(DeepFilterNetVstAudioProcessor& processor)
        : processor_(processor)
    {
        viewport_.setViewedComponent(&display_, false);
        viewport_.setScrollBarsShown(false, true, false, true);
        addAndMakeVisible(viewport_);

        refresh();
        updatePreferredSize();
        startTimerHz(4);
    }

    void resized() override
    {
        viewport_.setBounds(getLocalBounds().reduced(14));
        updateDisplayLayout();
    }

    int getPreferredHeight() const
    {
        return getContentHeightForWidth(getPreferredWidth());
    }

    void refreshNow()
    {
        refresh();
    }

private:
    void restoreViewPosition(juce::Point<int> previousPosition)
    {
        const auto maxX = juce::jmax(0, display_.getWidth() - viewport_.getViewWidth());
        const auto maxY = juce::jmax(0, display_.getHeight() - viewport_.getViewHeight());
        viewport_.setViewPosition(juce::jlimit(0, maxX, previousPosition.x),
                                  juce::jlimit(0, maxY, previousPosition.y));
    }

    class DiagnosticDisplay final : public juce::Component
    {
    public:
        void setHeadingText(const juce::String& heading)
        {
            if (heading == headingText_)
                return;

            headingText_ = heading;
            repaint();
        }

        void setDiagnosticText(const juce::String& text)
        {
            if (text == text_)
                return;

            text_ = text;
            parseDiagnosticText();
            repaint();
        }

        int getPreferredWidth() const
        {
            const auto sectionCount = juce::jmax(1, static_cast<int>(sections_.size()));
            return 20 + sectionCount * preferredCardWidth + juce::jmax(0, sectionCount - 1) * cardGap + 20;
        }

        int getPreferredHeight(int width) const
        {
            return buildLayout(width).totalHeight;
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(juce::Colours::transparentBlack);

            auto area = getLocalBounds().reduced(10, 10);
            const auto layout = buildLayout(getWidth());

            graphics.setColour(accent);
            graphics.setFont(juce::FontOptions("Microsoft YaHei", 22.0f, juce::Font::bold));
            graphics.drawText(headingText_, area.removeFromTop(34), juce::Justification::centredLeft, false);

            area.removeFromTop(10);
            const auto sectionCount = juce::jmax(1, static_cast<int>(sections_.size()));
            auto cardsArea = area.removeFromTop(layout.cardHeight);

            for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
            {
                auto cardArea = juce::Rectangle<int>(cardsArea.getX() + sectionIndex * (layout.cardWidth + cardGap),
                                                     cardsArea.getY(),
                                                     layout.cardWidth,
                                                     layout.cardHeight);
                paintSectionCard(graphics, cardArea, getSection(sectionIndex));
            }
        }

    private:
        static constexpr int cardGap = 12;
        static constexpr int minCardWidth = 280;
        static constexpr int preferredCardWidth = 332;
        static constexpr int cardTopPadding = 12;
        static constexpr int titleHeight = 42;
        static constexpr int titleGap = 8;
        static constexpr int rowHeight = 30;
        static constexpr int cardBottomPadding = 12;

        struct DiagnosticEntry
        {
            juce::String key;
            juce::String value;
        };

        struct DiagnosticSection
        {
            juce::String title;
            std::vector<DiagnosticEntry> entries;
        };

        struct LayoutMetrics
        {
            int cardWidth = preferredCardWidth;
            int cardHeight = 120;
            int totalWidth = minCardWidth + 40;
            int totalHeight = 120;
        };

        static bool isSectionTitle(const juce::String& line)
        {
            return line.startsWith("### ");
        }

        static juce::String parseSectionTitle(const juce::String& line)
        {
            return line.fromFirstOccurrenceOf("### ", false, false).trim();
        }

        static std::pair<juce::String, juce::String> splitKeyValue(const juce::String& line)
        {
            auto colonIndex = line.indexOfChar(juce_wchar(0xff1a));
            constexpr int separatorLength = 1;

            if (colonIndex < 0)
                colonIndex = line.indexOfChar(juce_wchar(':'));

            if (colonIndex < 0)
                return { {}, line };

            return {
                line.substring(0, colonIndex + separatorLength),
                line.substring(colonIndex + separatorLength).trimStart()
            };
        }

        void parseDiagnosticText()
        {
            sections_.clear();
            DiagnosticSection* currentSection = nullptr;
            const auto lines = juce::StringArray::fromLines(text_);

            for (const auto& rawLine : lines)
            {
                const auto line = rawLine.trim();

                if (line.isEmpty())
                    continue;

                if (isSectionTitle(line))
                {
                    sections_.push_back({ parseSectionTitle(line), {} });
                    currentSection = &sections_.back();
                    continue;
                }

                if (currentSection == nullptr)
                    continue;

                const auto [key, value] = splitKeyValue(line);
                currentSection->entries.push_back({ key, value });
            }
        }

        const DiagnosticSection& getSection(int index) const
        {
            static const DiagnosticSection placeholder {};
            return index >= 0 && index < static_cast<int>(sections_.size()) ? sections_[static_cast<size_t>(index)] : placeholder;
        }

        LayoutMetrics buildLayout(int width) const
        {
            LayoutMetrics layout;
            const auto sectionCount = juce::jmax(1, static_cast<int>(sections_.size()));
            const auto gapWidth = juce::jmax(0, sectionCount - 1) * cardGap;
            const auto preferredCardsWidth = sectionCount * preferredCardWidth + gapWidth;
            const auto availableCardsWidth = juce::jmax(minCardWidth, width - 40);

            if (availableCardsWidth > preferredCardsWidth)
                layout.cardWidth = juce::jmax(minCardWidth, (availableCardsWidth - gapWidth) / sectionCount);

            for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
                layout.cardHeight = juce::jmax(layout.cardHeight, getSectionCardHeight(getSection(sectionIndex)));

            layout.totalWidth = juce::jmax(width, 20 + sectionCount * layout.cardWidth + gapWidth + 20);
            layout.totalHeight = 20 + 34 + 10 + layout.cardHeight + 20;
            return layout;
        }

        static void paintSectionCard(juce::Graphics& graphics, juce::Rectangle<int> area, const DiagnosticSection& section)
        {
            const auto cardBounds = area.toFloat();
            graphics.setColour(panelInner.withAlpha(0.92f));
            graphics.fillRoundedRectangle(cardBounds, 20.0f);
            graphics.setColour(panelOutline.withAlpha(0.95f));
            graphics.drawRoundedRectangle(cardBounds, 20.0f, 1.0f);

            auto content = area.reduced(14, 12);

            graphics.setColour(accentBright);
            graphics.setFont(juce::FontOptions("Microsoft YaHei", 19.0f, juce::Font::bold));
            graphics.drawFittedText(section.title, content.removeFromTop(titleHeight), juce::Justification::centredLeft, 2, 0.92f);

            content.removeFromTop(titleGap);

            const auto labelFont = juce::Font(juce::FontOptions("Microsoft YaHei", 16.0f, juce::Font::bold));
            const auto valueFont = juce::Font(juce::FontOptions("Microsoft YaHei", 16.0f, juce::Font::plain));
            const int keyWidth = juce::jlimit(118, content.getWidth() / 2, 190);

            for (const auto& entry : section.entries)
            {
                auto row = content.removeFromTop(30);
                auto keyArea = row.removeFromLeft(keyWidth);

                graphics.setColour(textMuted);
                graphics.setFont(labelFont);
                graphics.drawFittedText(entry.key, keyArea, juce::Justification::centredLeft, 1, 0.92f);

                graphics.setColour(textStrong);
                graphics.setFont(valueFont);
                graphics.drawFittedText(entry.value, row, juce::Justification::centredLeft, 1, 0.92f);
            }
        }

        static int getSectionCardHeight(const DiagnosticSection& section)
        {
            return cardTopPadding + titleHeight + titleGap + static_cast<int>(section.entries.size()) * rowHeight + cardBottomPadding;
        }

        juce::String headingText_ = "Diagnostics";
        juce::String text_;
        std::vector<DiagnosticSection> sections_;
    };

    void timerCallback() override
    {
        refresh();
    }

    void refresh()
    {
        const auto language = processor_.getUiLanguage();
        const auto heading = dfvst::localisation::tr(TextId::diagnosticsHeading, language);
        const auto text = processor_.getDiagnosticText(language);

        if (heading != lastHeading_ || text != lastText_)
        {
            const auto previousViewPosition = viewport_.getViewPosition();
            lastHeading_ = heading;
            lastText_ = text;
            display_.setHeadingText(lastHeading_);
            display_.setDiagnosticText(lastText_);
            updatePreferredSize();
            restoreViewPosition(previousViewPosition);
        }
    }

    void updateDisplayLayout()
    {
        const auto visibleWidth = viewport_.getMaximumVisibleWidth() > 0
                                      ? viewport_.getMaximumVisibleWidth()
                                      : minPreferredWidth - 28;
        const auto width = juce::jmax(visibleWidth, display_.getPreferredWidth());
        const auto height = display_.getPreferredHeight(width);
        display_.setBounds(0, 0, width, height);
    }

    int getPreferredWidth() const
    {
        return juce::jlimit(minPreferredWidth, maxPreferredWidth, display_.getPreferredWidth() + 28);
    }

    int getContentHeightForWidth(int width) const
    {
        const auto viewportWidth = juce::jmax(200, width - 28);
        return display_.getPreferredHeight(viewportWidth) + 28 + viewport_.getScrollBarThickness();
    }

    void updatePreferredSize()
    {
        auto maxDisplayArea = juce::Rectangle<int>(0, 0, 1280, 900);
        if (const auto* primaryDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            maxDisplayArea = primaryDisplay->userArea;

        const auto maxAllowedWidth = juce::jmax(minPreferredWidth,
                                                juce::roundToInt(static_cast<float>(maxDisplayArea.getWidth()) * 0.92f));
        const auto newWidth = juce::jmin(getPreferredWidth(), maxAllowedWidth);
        const auto newHeight = getContentHeightForWidth(newWidth);
        const auto clampedHeight = juce::jmin(newHeight,
                                              juce::jmax(360, juce::roundToInt(static_cast<float>(maxDisplayArea.getHeight()) * 0.84f)));

        if (getWidth() != newWidth || getHeight() != clampedHeight)
            setSize(newWidth, clampedHeight);
        else
            updateDisplayLayout();
    }

    DeepFilterNetVstAudioProcessor& processor_;
    juce::Viewport viewport_;
    DiagnosticDisplay display_;
    juce::String lastHeading_;
    juce::String lastText_;
};
}

DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::AccentLookAndFeel()
{
    setDefaultSansSerifTypefaceName("Microsoft YaHei");
    setColour(juce::Slider::thumbColourId, accent);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::Slider::backgroundColourId, panelOutline.withAlpha(0.35f));
    setColour(juce::PopupMenu::backgroundColourId, panelColour);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentSoft.withAlpha(0.45f));
    setColour(juce::PopupMenu::highlightedTextColourId, textStrong);
}

void DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::drawLinearSlider(
    juce::Graphics& graphics,
    int x,
    int y,
    int width,
    int height,
    float sliderPos,
    float minSliderPos,
    float maxSliderPos,
    const juce::Slider::SliderStyle,
    juce::Slider&)
{
    juce::ignoreUnused(minSliderPos, maxSliderPos);

    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto track = bounds.withTrimmedTop(bounds.getHeight() * 0.34f).withHeight(10.0f);
    const auto clampedSliderPos = juce::jlimit(track.getX(), track.getRight(), sliderPos);

    graphics.setColour(panelInner);
    graphics.fillRoundedRectangle(track, 4.0f);

    juce::ColourGradient fillGradient(accentBright, track.getX(), track.getY(),
                                      accent, track.getRight(), track.getBottom(), false);
    graphics.setGradientFill(fillGradient);
    graphics.fillRoundedRectangle(track.withWidth(clampedSliderPos - track.getX()), 5.0f);

    graphics.setColour(accent.withAlpha(0.14f));
    graphics.fillEllipse(juce::Rectangle<float>(0.0f, 0.0f, 30.0f, 30.0f).withCentre({ clampedSliderPos, track.getCentreY() }));

    const auto thumb = juce::Rectangle<float>(0.0f, 0.0f, 18.0f, 18.0f).withCentre({ clampedSliderPos, track.getCentreY() });
    graphics.setColour(shadowColour.withAlpha(0.5f));
    graphics.fillEllipse(thumb.translated(0.0f, 1.5f));
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(thumb);
    graphics.setColour(accentBright);
    graphics.drawEllipse(thumb, 1.5f);
}

void DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::drawComboBox(
    juce::Graphics& graphics,
    int width,
    int height,
    bool isButtonDown,
    int buttonX,
    int buttonY,
    int buttonW,
    int buttonH,
    juce::ComboBox& comboBox)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH);

    auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
    const auto radius = 14.0f;
    const auto arrowArea = bounds.removeFromRight(42.0f);

    auto background = comboBox.findColour(juce::ComboBox::backgroundColourId);
    auto outline = comboBox.findColour(juce::ComboBox::outlineColourId);

    if (!comboBox.isEnabled())
    {
        background = background.interpolatedWith(juce::Colours::white, 0.08f);
        outline = outline.withAlpha(0.45f);
    }
    else if (isButtonDown || comboBox.isPopupActive())
    {
        background = background.interpolatedWith(accentSoft, 0.22f);
        outline = accent.withAlpha(0.9f);
    }
    else if (comboBox.isMouseOver(true))
    {
        background = background.interpolatedWith(panelOutlineStrong, 0.12f);
        outline = outline.interpolatedWith(accent, 0.35f);
    }

    juce::ColourGradient comboGradient(background.brighter(0.08f), bounds.getX(), bounds.getY(),
                                       background.darker(0.14f), bounds.getRight(), bounds.getBottom(), false);
    graphics.setGradientFill(comboGradient);
    graphics.fillRoundedRectangle(bounds, radius);

    graphics.setColour(panelInner.withAlpha(0.95f));
    graphics.fillRoundedRectangle(arrowArea.reduced(4.0f, 4.0f), 10.0f);

    if (comboBox.isPopupActive())
    {
        graphics.setColour(accent.withAlpha(0.14f));
        graphics.fillRoundedRectangle(bounds.reduced(1.0f), radius);
    }

    graphics.setColour(outline);
    graphics.drawRoundedRectangle(bounds, radius, comboBox.hasKeyboardFocus(true) ? 1.8f : 1.2f);

    graphics.setColour(panelOutline.withAlpha(0.55f));
    graphics.drawLine(arrowArea.getX(), bounds.getY() + 7.0f, arrowArea.getX(), bounds.getBottom() - 7.0f, 1.0f);

    juce::Path arrow;
    const auto cx = arrowArea.getCentreX();
    const auto cy = arrowArea.getCentreY() + 1.0f;
    arrow.startNewSubPath(cx - 5.5f, cy - 2.5f);
    arrow.lineTo(cx, cy + 3.0f);
    arrow.lineTo(cx + 5.5f, cy - 2.5f);

    graphics.setColour(isButtonDown || comboBox.isPopupActive() ? accentBright : accent);
    graphics.strokePath(arrow,
                        juce::PathStrokeType(2.0f, juce::PathStrokeType::JointStyle::curved, juce::PathStrokeType::EndCapStyle::rounded));
}

juce::Font DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::FontOptions(14.5f, juce::Font::bold);
}

void DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::positionComboBoxText(juce::ComboBox& comboBox,
                                                                                    juce::Label& label)
{
    label.setBounds(16, 1, comboBox.getWidth() - 60, comboBox.getHeight() - 2);
    label.setFont(getComboBoxFont(comboBox));
    label.setJustificationType(juce::Justification::centredLeft);
}

void DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::drawPopupMenuBackground(juce::Graphics& graphics,
                                                                                       int width,
                                                                                       int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    graphics.fillAll(juce::Colours::transparentBlack);
    juce::ColourGradient popupGradient(panelInner, bounds.getX(), bounds.getY(),
                                       panelColour, bounds.getRight(), bounds.getBottom(), false);
    graphics.setGradientFill(popupGradient);
    graphics.fillRoundedRectangle(bounds.reduced(1.5f), 14.0f);
    graphics.setColour(panelOutlineStrong.withAlpha(0.85f));
    graphics.drawRoundedRectangle(bounds.reduced(1.5f), 14.0f, 1.1f);
}

void DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::drawPopupMenuItem(
    juce::Graphics& graphics,
    const juce::Rectangle<int>& area,
    bool isSeparator,
    bool isActive,
    bool isHighlighted,
    bool isTicked,
    bool hasSubMenu,
    const juce::String& text,
    const juce::String& shortcutKeyText,
    const juce::Drawable* icon,
    const juce::Colour* textColour)
{
    juce::ignoreUnused(icon);

    if (isSeparator)
    {
        const auto line = area.reduced(14, area.getHeight() / 2);
        graphics.setColour(panelOutline.withAlpha(0.8f));
        graphics.drawLine(static_cast<float>(line.getX()),
                          static_cast<float>(line.getY()),
                          static_cast<float>(line.getRight()),
                          static_cast<float>(line.getY()),
                          1.0f);
        return;
    }

    auto row = area.reduced(6, 2);
    const auto isEnabled = isActive;

    if (isHighlighted && isEnabled)
    {
        graphics.setColour(accentSoft.withAlpha(0.72f));
        graphics.fillRoundedRectangle(row.toFloat(), 10.0f);
        graphics.setColour(accent.withAlpha(0.95f));
        graphics.fillRoundedRectangle(row.removeFromLeft(4).toFloat(), 2.0f);
    }

    auto colour = textColour != nullptr ? *textColour : textStrong;
    if (!isEnabled)
        colour = textMuted.withAlpha(0.55f);
    else if (isHighlighted)
        colour = textStrong;

    const auto iconArea = row.removeFromLeft(24);

    if (isTicked)
    {
        graphics.setColour(accent);
        graphics.fillEllipse(iconArea.toFloat().reduced(5.0f));
        graphics.setColour(juce::Colours::white);
        juce::Path tick;
        tick.startNewSubPath(static_cast<float>(iconArea.getX() + 7), static_cast<float>(iconArea.getCentreY()));
        tick.lineTo(static_cast<float>(iconArea.getX() + 10), static_cast<float>(iconArea.getBottom() - 8));
        tick.lineTo(static_cast<float>(iconArea.getRight() - 6), static_cast<float>(iconArea.getY() + 7));
        graphics.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::JointStyle::curved, juce::PathStrokeType::EndCapStyle::rounded));
    }

    auto shortcutArea = row.removeFromRight(shortcutKeyText.isNotEmpty() ? 72 : 0);
    auto submenuArea = row.removeFromRight(hasSubMenu ? 18 : 0);

    graphics.setColour(colour);
    graphics.setFont(getPopupMenuFont());
    graphics.drawFittedText(text, row, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        graphics.setColour(textSubtle);
        graphics.setFont(juce::FontOptions(12.5f, juce::Font::plain));
        graphics.drawFittedText(shortcutKeyText, shortcutArea, juce::Justification::centredRight, 1);
    }

    if (hasSubMenu)
    {
        juce::Path arrow;
        const auto cx = static_cast<float>(submenuArea.getCentreX()) - 1.0f;
        const auto cy = static_cast<float>(submenuArea.getCentreY());
        arrow.startNewSubPath(cx - 2.0f, cy - 4.0f);
        arrow.lineTo(cx + 2.5f, cy);
        arrow.lineTo(cx - 2.0f, cy + 4.0f);
        graphics.setColour(colour.withAlpha(0.8f));
        graphics.strokePath(arrow,
                            juce::PathStrokeType(1.8f, juce::PathStrokeType::JointStyle::curved, juce::PathStrokeType::EndCapStyle::rounded));
    }
}

juce::Font DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::getPopupMenuFont()
{
    return juce::FontOptions(14.0f, juce::Font::bold);
}

void DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::drawButtonBackground(juce::Graphics& graphics,
                                                                                    juce::Button& button,
                                                                                    const juce::Colour&,
                                                                                    bool isMouseOverButton,
                                                                                    bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto background = panelInner;
    auto outline = panelOutlineStrong;

    if (!button.isEnabled())
    {
        background = background.withAlpha(0.6f);
        outline = outline.withAlpha(0.45f);
    }
    else if (isButtonDown)
    {
        background = accentSoft.withAlpha(0.9f);
        outline = accent;
    }
    else if (isMouseOverButton)
    {
        background = background.interpolatedWith(panelOutlineStrong, 0.15f);
        outline = outline.interpolatedWith(accent, 0.35f);
    }

    juce::ColourGradient gradient(background.brighter(0.08f), bounds.getX(), bounds.getY(),
                                  background.darker(0.14f), bounds.getRight(), bounds.getBottom(), false);
    graphics.setGradientFill(gradient);
    graphics.fillRoundedRectangle(bounds, 12.0f);
    graphics.setColour(outline);
    graphics.drawRoundedRectangle(bounds, 12.0f, 1.2f);
}

juce::Font DeepFilterNetVstAudioProcessorEditor::AccentLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions(static_cast<float>(juce::jmax(13, buttonHeight / 2)), juce::Font::bold);
}

class DeepFilterNetVstAudioProcessorEditor::DiagnosticWindow final : public juce::DocumentWindow
{
public:
    DiagnosticWindow(DeepFilterNetVstAudioProcessorEditor& owner, DeepFilterNetVstAudioProcessor& processor)
        : juce::DocumentWindow(dfvst::localisation::tr(TextId::diagnosticsWindowTitle, processor.getUiLanguage()),
                               panelColour,
                               juce::DocumentWindow::closeButton,
                               true),
          owner_(owner)
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(34);
        setResizable(false, false);
        setContentOwned(new DiagnosticContent(processor), true);
        centreAroundComponent(&owner, getWidth(), getHeight());
    }

    float getDesktopScaleFactor() const override
    {
        return juce::Component::getApproximateScaleFactorForComponent(&owner_);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync([safeOwner = juce::Component::SafePointer<DeepFilterNetVstAudioProcessorEditor>(&owner_)]
        {
            if (safeOwner != nullptr)
                safeOwner->closeDiagnosticWindow();
        });
    }

private:
    DeepFilterNetVstAudioProcessorEditor& owner_;
};

DeepFilterNetVstAudioProcessorEditor::DeepFilterNetVstAudioProcessorEditor(DeepFilterNetVstAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor),
      denoiseAttachment_(processor_.getParametersState(), DeepFilterNetVstAudioProcessor::attenParamId, denoiseSlider_),
      postAttachment_(processor_.getParametersState(), DeepFilterNetVstAudioProcessor::postParamId, postSlider_)
{
    setLookAndFeel(&lookAndFeel_);
    setSize(468, 420);
    processor_.addChangeListener(this);

    titleLabel_.setText(DeepFilterNetVstAudioProcessor::pluginDisplayName, juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, textStrong);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setFont(juce::FontOptions(13.5f, juce::Font::plain));
    subtitleLabel_.setColour(juce::Label::textColourId, textMuted);
    addAndMakeVisible(subtitleLabel_);

    configureSlider(denoiseSlider_, denoiseLabel_, {});
    configureSlider(postSlider_, postLabel_, {});
    configureComboBox(reduceMaskComboBox_, reduceMaskLabel_, {});
    populateReduceMaskChoices();
    reduceMaskAttachment_ = std::make_unique<ComboBoxAttachment>(processor_.getParametersState(),
                                                                 DeepFilterNetVstAudioProcessor::reduceMaskParamId,
                                                                 reduceMaskComboBox_);

    for (auto* valueLabel : { &denoiseValueLabel_, &postValueLabel_ })
    {
        valueLabel->setJustificationType(juce::Justification::centredRight);
        valueLabel->setFont(juce::FontOptions(16.0f, juce::Font::bold));
        valueLabel->setColour(juce::Label::textColourId, accent);
        addAndMakeVisible(*valueLabel);
    }
    configureButton(diagnosticButton_, {});
    diagnosticButton_.onClick = [this] { showDiagnosticWindow(); };

    denoiseSlider_.onValueChange = [this] { updateValueLabels(); };
    postSlider_.onValueChange = [this] { updateValueLabels(); };

    refreshLocalisedTexts();
    updateValueLabels();
}

DeepFilterNetVstAudioProcessorEditor::~DeepFilterNetVstAudioProcessorEditor()
{
    processor_.removeChangeListener(this);
    closeDiagnosticWindow();
    setLookAndFeel(nullptr);
}

void DeepFilterNetVstAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    juce::ColourGradient background(backgroundTop, 0.0f, 0.0f, backgroundBottom, 0.0f, static_cast<float>(getHeight()), false);
    graphics.setGradientFill(background);
    graphics.fillAll();

    juce::ColourGradient glow(backgroundGlow.withAlpha(0.48f),
                              static_cast<float>(getWidth()) * 0.82f,
                              28.0f,
                              juce::Colours::transparentBlack,
                              static_cast<float>(getWidth()) * 0.82f,
                              180.0f,
                              true);
    graphics.setGradientFill(glow);
    graphics.fillEllipse(static_cast<float>(getWidth()) - 180.0f, -36.0f, 240.0f, 220.0f);

    const auto topPanel = juce::Rectangle<float>(24.0f, 88.0f, static_cast<float>(getWidth() - 48), 88.0f);
    const auto bottomPanel = juce::Rectangle<float>(24.0f, 184.0f, static_cast<float>(getWidth() - 48), 88.0f);
    const auto modePanel = juce::Rectangle<float>(24.0f, 280.0f, static_cast<float>(getWidth() - 48), 84.0f);

    const auto paintPanel = [&](juce::Rectangle<float> panel)
    {
        graphics.setColour(shadowColour.withAlpha(0.42f));
        graphics.fillRoundedRectangle(panel.translated(0.0f, 6.0f), 20.0f);

        juce::ColourGradient panelGradient(panelInner,
                                           panel.getX(),
                                           panel.getY(),
                                           panelColour,
                                           panel.getRight(),
                                           panel.getBottom(),
                                           false);
        graphics.setGradientFill(panelGradient);
        graphics.fillRoundedRectangle(panel, 20.0f);

        graphics.setColour(panelOutline);
        graphics.drawRoundedRectangle(panel, 20.0f, 1.0f);

        graphics.setColour(juce::Colours::white.withAlpha(0.035f));
        graphics.drawLine(panel.getX() + 18.0f,
                          panel.getY() + 12.0f,
                          panel.getRight() - 18.0f,
                          panel.getY() + 12.0f,
                          1.0f);
    };

    paintPanel(topPanel);
    paintPanel(bottomPanel);
    paintPanel(modePanel);

    graphics.setColour(accent.withAlpha(0.9f));
    graphics.fillRoundedRectangle(24.0f, 22.0f, 86.0f, 6.0f, 3.0f);
    paintLanguageButton(graphics);
}

void DeepFilterNetVstAudioProcessorEditor::resized()
{
    const auto content = getLocalBounds().reduced(28, 86);
    const int cardWidth = content.getWidth();
    const int valueWidth = 94;

    languageButtonBounds_ = { getWidth() - 190, 22, 38, 38 };
    const int titleRight = languageButtonBounds_.getX() - 18;

    titleLabel_.setBounds(28, 18, juce::jmax(120, titleRight - 28), 34);
    subtitleLabel_.setBounds(30, 50, juce::jmax(120, titleRight - 30), 20);

    denoiseLabel_.setBounds(40, 96, 180, 22);
    denoiseValueLabel_.setBounds(getWidth() - 124, 96, valueWidth, 22);
    denoiseSlider_.setBounds(36, 126, cardWidth - 12, 28);

    postLabel_.setBounds(40, 192, 180, 22);
    postValueLabel_.setBounds(getWidth() - 124, 192, valueWidth, 22);
    postSlider_.setBounds(36, 222, cardWidth - 12, 28);

    reduceMaskLabel_.setBounds(40, 290, 220, 22);
    reduceMaskComboBox_.setBounds(36, 320, cardWidth - 12, 36);
    diagnosticButton_.setBounds(getWidth() - 144, 24, 108, 32);
}

void DeepFilterNetVstAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    updateLanguageButtonState(event.position, false);
}

void DeepFilterNetVstAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    isLanguageButtonHovered_ = false;
    isLanguageButtonPressed_ = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(languageButtonBounds_);
}

void DeepFilterNetVstAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
        return;

    isLanguageButtonPressed_ = isLanguageButtonHit(event.position);
    if (isLanguageButtonPressed_)
        repaint(languageButtonBounds_);
}

void DeepFilterNetVstAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    const auto shouldOpenLanguageMenu = isLanguageButtonPressed_ && isLanguageButtonHit(event.position);
    isLanguageButtonPressed_ = false;
    updateLanguageButtonState(event.position, false);

    if (shouldOpenLanguageMenu)
        showLanguageMenu();
}

void DeepFilterNetVstAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &processor_)
        refreshLocalisedTexts();
}

void DeepFilterNetVstAudioProcessorEditor::updateValueLabels()
{
    denoiseValueLabel_.setText(juce::String(denoiseSlider_.getValue(), 0) + " dB", juce::dontSendNotification);
    postValueLabel_.setText(juce::String(postSlider_.getValue(), 3), juce::dontSendNotification);
}

void DeepFilterNetVstAudioProcessorEditor::populateReduceMaskChoices()
{
    auto targetSelectedId = reduceMaskComboBox_.getSelectedId();

    if (targetSelectedId == 0)
        targetSelectedId = 1;

    if (const auto* reduceMaskParam = processor_.getParametersState().getRawParameterValue(DeepFilterNetVstAudioProcessor::reduceMaskParamId))
        targetSelectedId = juce::jlimit(1, 3, juce::roundToInt(reduceMaskParam->load()) + 1);

    reduceMaskComboBox_.clear(juce::dontSendNotification);
    reduceMaskComboBox_.addItemList(dfvst::localisation::getReduceMaskUiChoices(processor_.getUiLanguage()), 1);
    reduceMaskComboBox_.setSelectedId(targetSelectedId, juce::dontSendNotification);
}

void DeepFilterNetVstAudioProcessorEditor::refreshLocalisedTexts()
{
    const auto language = processor_.getUiLanguage();

    subtitleLabel_.setText(dfvst::localisation::tr(TextId::subtitle, language), juce::dontSendNotification);
    denoiseLabel_.setText(dfvst::localisation::tr(TextId::denoiseLabel, language), juce::dontSendNotification);
    postLabel_.setText(dfvst::localisation::tr(TextId::postFilterLabel, language), juce::dontSendNotification);
    reduceMaskLabel_.setText(dfvst::localisation::tr(TextId::reduceMaskLabel, language), juce::dontSendNotification);
    diagnosticButton_.setButtonText(dfvst::localisation::tr(TextId::diagnosticsButton, language));

    populateReduceMaskChoices();

    if (diagnosticWindow_ != nullptr)
    {
        diagnosticWindow_->setName(dfvst::localisation::tr(TextId::diagnosticsWindowTitle, language));
        if (auto* diagnosticContent = dynamic_cast<DiagnosticContent*>(diagnosticWindow_->getContentComponent()))
            diagnosticContent->refreshNow();
    }

    repaint();
}

void DeepFilterNetVstAudioProcessorEditor::paintLanguageButton(juce::Graphics& graphics) const
{
    const auto bounds = languageButtonBounds_.toFloat();
    const auto circleBounds = juce::Rectangle<float>(bounds.getX() + 2.5f, bounds.getY() + 2.5f, 30.0f, 30.0f);
    const auto badgeBounds = juce::Rectangle<float>(bounds.getRight() - 19.0f, bounds.getBottom() - 11.0f, 17.0f, 10.0f);
    auto background = panelInner.withAlpha(0.96f);
    auto outline = panelOutlineStrong;
    auto globeColour = textStrong.withAlpha(0.95f);

    if (isLanguageButtonPressed_)
    {
        background = accentSoft.withAlpha(0.94f);
        outline = accent;
        globeColour = juce::Colours::white;
    }
    else if (isLanguageButtonHovered_)
    {
        background = background.interpolatedWith(panelOutlineStrong, 0.18f);
        outline = accent.withAlpha(0.92f);
    }

    juce::ColourGradient gradient(background.brighter(0.08f), circleBounds.getX(), circleBounds.getY(),
                                  background.darker(0.12f), circleBounds.getRight(), circleBounds.getBottom(), false);
    graphics.setGradientFill(gradient);
    graphics.fillEllipse(circleBounds);

    graphics.setColour(outline);
    graphics.drawEllipse(circleBounds, isLanguageButtonHovered_ || isLanguageButtonPressed_ ? 1.5f : 1.1f);

    const auto globeBounds = circleBounds.reduced(7.5f);
    graphics.setColour(globeColour);
    graphics.drawEllipse(globeBounds, 1.05f);
    graphics.drawLine(globeBounds.getCentreX(), globeBounds.getY() + 1.0f,
                      globeBounds.getCentreX(), globeBounds.getBottom() - 1.0f, 0.9f);
    graphics.drawEllipse(globeBounds.reduced(globeBounds.getWidth() * 0.24f, 0.0f), 0.75f);
    graphics.drawEllipse(globeBounds.reduced(0.0f, globeBounds.getHeight() * 0.24f), 0.75f);
    graphics.drawLine(globeBounds.getX() + 1.0f, globeBounds.getCentreY(),
                      globeBounds.getRight() - 1.0f, globeBounds.getCentreY(), 0.9f);

    graphics.setColour(accent);
    graphics.fillRoundedRectangle(badgeBounds, 5.0f);
    graphics.setColour(backgroundTop.withAlpha(0.94f));
    graphics.setFont(juce::FontOptions(7.8f, juce::Font::bold));
    graphics.drawFittedText(getLanguageBadgeText(processor_.getUiLanguage()),
                            badgeBounds.toNearestInt(),
                            juce::Justification::centred,
                            1,
                            1.0f);
}

void DeepFilterNetVstAudioProcessorEditor::updateLanguageButtonState(juce::Point<float> position, bool isPressed)
{
    const auto isHovered = isLanguageButtonHit(position);
    if (isLanguageButtonHovered_ == isHovered && isLanguageButtonPressed_ == isPressed)
    {
        setMouseCursor(isHovered ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        return;
    }

    isLanguageButtonHovered_ = isHovered;
    isLanguageButtonPressed_ = isPressed;
    setMouseCursor(isHovered ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint(languageButtonBounds_);
}

bool DeepFilterNetVstAudioProcessorEditor::isLanguageButtonHit(juce::Point<float> position) const
{
    return languageButtonBounds_.contains(position.toInt());
}

void DeepFilterNetVstAudioProcessorEditor::showLanguageMenu()
{
    juce::PopupMenu menu;
    int itemId = 1;

    for (const auto& language : dfvst::localisation::getAvailableLanguages())
    {
        menu.addItem(itemId,
                     language.displayName,
                     true,
                     itemId == getLanguageChoiceId(processor_.getUiLanguage()));
        ++itemId;
    }

    auto options = juce::PopupMenu::Options()
                       .withTargetComponent(this)
                       .withTargetScreenArea(localAreaToGlobal(languageButtonBounds_))
                       .withMinimumWidth(languageButtonBounds_.getWidth());

    menu.showMenuAsync(options,
                       [safeThis = juce::Component::SafePointer<DeepFilterNetVstAudioProcessorEditor>(this)](int selectedId)
                       {
                           if (safeThis == nullptr || selectedId == 0)
                               return;

                           if (const auto selectedLanguage = getLanguageCodeForChoiceId(selectedId);
                               selectedLanguage.isNotEmpty())
                           {
                               safeThis->processor_.setUiLanguage(selectedLanguage);
                           }
                       });
}

void DeepFilterNetVstAudioProcessorEditor::configureSlider(juce::Slider& slider, juce::Label& label, const juce::String& title)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour(juce::Slider::textBoxOutlineColourId, panelOutline);
    addAndMakeVisible(slider);

    label.setText(title, juce::dontSendNotification);
    label.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, textStrong);
    addAndMakeVisible(label);
}

void DeepFilterNetVstAudioProcessorEditor::configureButton(juce::TextButton& button, const juce::String& text)
{
    button.setButtonText(text);
    button.setColour(juce::TextButton::textColourOffId, textStrong);
    button.setColour(juce::TextButton::textColourOnId, textStrong);
    addAndMakeVisible(button);
}

void DeepFilterNetVstAudioProcessorEditor::configureComboBox(juce::ComboBox& comboBox,
                                                             juce::Label& label,
                                                             const juce::String& title)
{
    comboBox.setColour(juce::ComboBox::backgroundColourId, panelColour);
    comboBox.setColour(juce::ComboBox::textColourId, textStrong);
    comboBox.setColour(juce::ComboBox::outlineColourId, panelOutlineStrong);
    comboBox.setColour(juce::ComboBox::buttonColourId, panelInner);
    comboBox.setColour(juce::ComboBox::arrowColourId, accent);
    comboBox.setColour(juce::PopupMenu::backgroundColourId, panelColour);
    comboBox.setColour(juce::PopupMenu::highlightedBackgroundColourId, accentSoft.withAlpha(0.45f));
    comboBox.setColour(juce::PopupMenu::highlightedTextColourId, textStrong);
    comboBox.setScrollWheelEnabled(false);
    addAndMakeVisible(comboBox);

    label.setText(title, juce::dontSendNotification);
    label.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, textStrong);
    addAndMakeVisible(label);
}

void DeepFilterNetVstAudioProcessorEditor::showDiagnosticWindow()
{
    if (diagnosticWindow_ == nullptr)
        diagnosticWindow_ = std::make_unique<DiagnosticWindow>(*this, processor_);

    diagnosticWindow_->setName(dfvst::localisation::tr(TextId::diagnosticsWindowTitle, processor_.getUiLanguage()));
    if (auto* diagnosticContent = dynamic_cast<DiagnosticContent*>(diagnosticWindow_->getContentComponent()))
        diagnosticContent->refreshNow();
    diagnosticWindow_->setVisible(true);
    diagnosticWindow_->toFront(true);
}

void DeepFilterNetVstAudioProcessorEditor::closeDiagnosticWindow()
{
    if (diagnosticWindow_ == nullptr)
        return;

    diagnosticWindow_->setVisible(false);
    diagnosticWindow_.reset();
}

