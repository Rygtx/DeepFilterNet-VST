#include "PluginEditor.h"

namespace
{
const auto backgroundTop = juce::Colour::fromRGB(247, 242, 235);
const auto backgroundBottom = juce::Colour::fromRGB(236, 227, 216);
const auto panelColour = juce::Colour::fromRGB(255, 250, 243);
const auto panelOutline = juce::Colour::fromRGB(223, 212, 198);
const auto accent = juce::Colour::fromRGB(205, 101, 44);
const auto accentSoft = juce::Colour::fromRGB(237, 189, 151);
const auto textStrong = juce::Colour::fromRGB(41, 34, 29);
const auto textMuted = juce::Colour::fromRGB(108, 96, 85);

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
    setColour(juce::Slider::backgroundColourId, accentSoft.withAlpha(0.45f));
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
    const auto track = bounds.withTrimmedTop(bounds.getHeight() * 0.38f).withHeight(8.0f);
    const auto clampedSliderPos = juce::jlimit(track.getX(), track.getRight(), sliderPos);

    graphics.setColour(accentSoft.withAlpha(0.42f));
    graphics.fillRoundedRectangle(track, 4.0f);

    graphics.setColour(accent);
    graphics.fillRoundedRectangle(track.withWidth(clampedSliderPos - track.getX()), 4.0f);

    const auto thumb = juce::Rectangle<float>(0.0f, 0.0f, 18.0f, 18.0f).withCentre({ clampedSliderPos, track.getCentreY() });
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(thumb);
    graphics.setColour(accent);
    graphics.drawEllipse(thumb, 1.5f);
}

DeepFilterNetVstAudioProcessorEditor::DeepFilterNetVstAudioProcessorEditor(DeepFilterNetVstAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor),
      denoiseAttachment_(processor_.getParametersState(), DeepFilterNetVstAudioProcessor::attenParamId, denoiseSlider_),
      postAttachment_(processor_.getParametersState(), DeepFilterNetVstAudioProcessor::postParamId, postSlider_)
{
    setLookAndFeel(&lookAndFeel_);
    setSize(460, 320);

    titleLabel_.setText(DeepFilterNetVstAudioProcessor::pluginDisplayName, juce::dontSendNotification);
    titleLabel_.setFont(juce::FontOptions(31.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, textStrong);
    addAndMakeVisible(titleLabel_);

    subtitleLabel_.setText(utf8Text("语音降噪与后置滤波控制"), juce::dontSendNotification);
    subtitleLabel_.setFont(juce::FontOptions(14.0f, juce::Font::plain));
    subtitleLabel_.setColour(juce::Label::textColourId, textMuted);
    addAndMakeVisible(subtitleLabel_);

    configureSlider(denoiseSlider_, denoiseLabel_, utf8Text("降噪强度"));
    configureSlider(postSlider_, postLabel_, utf8Text("后置滤波"));

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

    graphics.setColour(accent);
    graphics.fillRect(0, 0, getWidth(), 10);

    const auto panel = getLocalBounds().reduced(22, 78);
    const auto topPanel = juce::Rectangle<float>(24.0f, 88.0f, static_cast<float>(getWidth() - 48), 88.0f);
    const auto bottomPanel = juce::Rectangle<float>(24.0f, 184.0f, static_cast<float>(getWidth() - 48), 88.0f);

    graphics.setColour(panelColour);
    graphics.fillRoundedRectangle(topPanel, 18.0f);
    graphics.fillRoundedRectangle(bottomPanel, 18.0f);
    graphics.setColour(panelOutline);
    graphics.drawRoundedRectangle(topPanel, 18.0f, 1.0f);
    graphics.drawRoundedRectangle(bottomPanel, 18.0f, 1.0f);

    const auto badgeBounds = juce::Rectangle<int>(getWidth() - 118, 28, 88, 24);
    graphics.setColour(accentSoft.withAlpha(0.35f));
    graphics.fillRoundedRectangle(badgeBounds.toFloat(), 12.0f);
    graphics.setColour(accent);
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

    statusLabel_.setBounds(30, 282, getWidth() - 60, 30);
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
    addAndMakeVisible(slider);

    label.setText(title, juce::dontSendNotification);
    label.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, textStrong);
    addAndMakeVisible(label);
}

