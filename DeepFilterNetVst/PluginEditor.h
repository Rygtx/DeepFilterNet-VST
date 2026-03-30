#pragma once

#include "PluginProcessor.h"

#include <JuceHeader.h>

class DeepFilterNetVstAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                    private juce::Timer
{
public:
    explicit DeepFilterNetVstAudioProcessorEditor(DeepFilterNetVstAudioProcessor&);
    ~DeepFilterNetVstAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class AccentLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        AccentLookAndFeel();
        void drawLinearSlider(juce::Graphics&,
                              int x,
                              int y,
                              int width,
                              int height,
                              float sliderPos,
                              float minSliderPos,
                              float maxSliderPos,
                              const juce::Slider::SliderStyle,
                              juce::Slider&) override;
        void drawComboBox(juce::Graphics&,
                          int width,
                          int height,
                          bool isButtonDown,
                          int buttonX,
                          int buttonY,
                          int buttonW,
                          int buttonH,
                          juce::ComboBox&) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
        void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
        void drawPopupMenuItem(juce::Graphics&,
                               const juce::Rectangle<int>& area,
                               bool isSeparator,
                               bool isActive,
                               bool isHighlighted,
                               bool isTicked,
                               bool hasSubMenu,
                               const juce::String& text,
                               const juce::String& shortcutKeyText,
                               const juce::Drawable* icon,
                               const juce::Colour* textColour) override;
        juce::Font getPopupMenuFont() override;
    };

    void timerCallback() override;
    void updateValueLabels();
    void updateStatusLabel();
    void configureSlider(juce::Slider& slider, juce::Label& label, const juce::String& title);
    void configureComboBox(juce::ComboBox& comboBox, juce::Label& label, const juce::String& title);

    DeepFilterNetVstAudioProcessor& processor_;
    AccentLookAndFeel lookAndFeel_;

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label denoiseLabel_;
    juce::Label denoiseValueLabel_;
    juce::Label postLabel_;
    juce::Label postValueLabel_;
    juce::Label reduceMaskLabel_;
    juce::Label statusLabel_;

    juce::Slider denoiseSlider_;
    juce::Slider postSlider_;
    juce::ComboBox reduceMaskComboBox_;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    SliderAttachment denoiseAttachment_;
    SliderAttachment postAttachment_;
    std::unique_ptr<ComboBoxAttachment> reduceMaskAttachment_;
};
