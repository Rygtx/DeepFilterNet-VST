#include "PluginEditor.h"

namespace
{
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

juce::String utf8Text(const char* text)
{
    return juce::String::fromUTF8(text);
}
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

DeepFilterNetVstAudioProcessorEditor::DeepFilterNetVstAudioProcessorEditor(DeepFilterNetVstAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor),
      denoiseAttachment_(processor_.getParametersState(), DeepFilterNetVstAudioProcessor::attenParamId, denoiseSlider_),
      postAttachment_(processor_.getParametersState(), DeepFilterNetVstAudioProcessor::postParamId, postSlider_)
{
    setLookAndFeel(&lookAndFeel_);
    setSize(468, 420);

    titleLabel_.setText(DeepFilterNetVstAudioProcessor::pluginDisplayName, juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, textStrong);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setText(utf8Text("语音降噪与后置滤波控制"), juce::dontSendNotification);
    subtitleLabel_.setFont(juce::FontOptions(13.5f, juce::Font::plain));
    subtitleLabel_.setColour(juce::Label::textColourId, textMuted);
    addAndMakeVisible(subtitleLabel_);

    configureSlider(denoiseSlider_, denoiseLabel_, utf8Text("降噪强度"));
    configureSlider(postSlider_, postLabel_, utf8Text("后置滤波"));
    configureComboBox(reduceMaskComboBox_, reduceMaskLabel_, utf8Text("声道掩码合并"));
    reduceMaskComboBox_.addItemList(DeepFilterNetVstAudioProcessor::getReduceMaskChoices(), 1);
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

    statusLabel_.setJustificationType(juce::Justification::topLeft);
    statusLabel_.setFont(juce::FontOptions(13.5f, juce::Font::plain));
    statusLabel_.setColour(juce::Label::textColourId, textMuted);
    addAndMakeVisible(statusLabel_);

    denoiseSlider_.onValueChange = [this] { updateValueLabels(); };
    postSlider_.onValueChange = [this] { updateValueLabels(); };

    updateValueLabels();
    updateStatusLabel();
    startTimerHz(6);
}

DeepFilterNetVstAudioProcessorEditor::~DeepFilterNetVstAudioProcessorEditor()
{
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

    for (const auto& panel : { topPanel, bottomPanel, modePanel })
    {
        graphics.setColour(shadowColour.withAlpha(0.42f));
        graphics.fillRoundedRectangle(panel.translated(0.0f, 6.0f), 20.0f);

        juce::ColourGradient panelGradient(panelInner, panel.getX(), panel.getY(),
                                           panelColour, panel.getRight(), panel.getBottom(), false);
        graphics.setGradientFill(panelGradient);
        graphics.fillRoundedRectangle(panel, 20.0f);

        graphics.setColour(panelOutline);
        graphics.drawRoundedRectangle(panel, 20.0f, 1.0f);

        graphics.setColour(juce::Colours::white.withAlpha(0.035f));
        graphics.drawLine(panel.getX() + 18.0f, panel.getY() + 12.0f, panel.getRight() - 18.0f, panel.getY() + 12.0f, 1.0f);
    }

    graphics.setColour(accent.withAlpha(0.9f));
    graphics.fillRoundedRectangle(24.0f, 22.0f, 86.0f, 6.0f, 3.0f);

    const auto badgeBounds = juce::Rectangle<int>(getWidth() - 118, 28, 88, 24);
    graphics.setColour(accentSoft.withAlpha(0.7f));
    graphics.fillRoundedRectangle(badgeBounds.toFloat(), 12.0f);
    graphics.setColour(accentBright);
    graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    graphics.drawFittedText("VST", badgeBounds, juce::Justification::centred, 1);
}

void DeepFilterNetVstAudioProcessorEditor::resized()
{
    titleLabel_.setBounds(28, 18, getWidth() - 56, 34);
    subtitleLabel_.setBounds(30, 50, getWidth() - 120, 20);

    const auto content = getLocalBounds().reduced(28, 86);
    const int cardWidth = content.getWidth();
    const int valueWidth = 94;

    denoiseLabel_.setBounds(40, 96, 180, 22);
    denoiseValueLabel_.setBounds(getWidth() - 124, 96, valueWidth, 22);
    denoiseSlider_.setBounds(36, 126, cardWidth - 12, 28);

    postLabel_.setBounds(40, 192, 180, 22);
    postValueLabel_.setBounds(getWidth() - 124, 192, valueWidth, 22);
    postSlider_.setBounds(36, 222, cardWidth - 12, 28);

    reduceMaskLabel_.setBounds(40, 290, 180, 22);
    reduceMaskComboBox_.setBounds(36, 320, cardWidth - 12, 36);

    statusLabel_.setBounds(30, 376, getWidth() - 60, 28);
}

void DeepFilterNetVstAudioProcessorEditor::timerCallback()
{
    updateStatusLabel();
}

void DeepFilterNetVstAudioProcessorEditor::updateValueLabels()
{
    denoiseValueLabel_.setText(juce::String(denoiseSlider_.getValue(), 0) + " dB", juce::dontSendNotification);
    postValueLabel_.setText(juce::String(postSlider_.getValue(), 3), juce::dontSendNotification);
}

void DeepFilterNetVstAudioProcessorEditor::updateStatusLabel()
{
    if (processor_.getCurrentSampleRateHz() <= 0.0)
    {
        statusLabel_.setText(utf8Text("等待宿主播放配置。"), juce::dontSendNotification);
        return;
    }

    if (processor_.isSampleRateCompatible())
    {
        const auto suffix = processor_.isDenoiserReady() ? utf8Text("运行时已加载") : utf8Text("正在初始化运行时");
        statusLabel_.setText(utf8Text("宿主采样率为 48 kHz，") + suffix + utf8Text("。"), juce::dontSendNotification);
        return;
    }

    const auto suffix = processor_.isDenoiserReady() ? utf8Text("运行时已加载") : utf8Text("正在初始化运行时");
    statusLabel_.setText(utf8Text("当前宿主采样率为 ") + juce::String(processor_.getCurrentSampleRateHz(), 1)
                             + utf8Text(" Hz，插件会在内部重采样到 48 kHz，") + suffix + utf8Text("。"),
                         juce::dontSendNotification);
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

