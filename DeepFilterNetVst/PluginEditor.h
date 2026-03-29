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
    };

    void timerCallback() override;
    void updateValueLabels();
    void updateStatusLabel();
    void configureSlider(juce::Slider& slider, juce::Label& label, const juce::String& title);

    DeepFilterNetVstAudioProcessor& processor_;
    AccentLookAndFeel lookAndFeel_;

    juce::Label titleLabel_;
    juce::Label subtitleLabel_;
    juce::Label denoiseLabel_;
    juce::Label denoiseValueLabel_;
    juce::Label postLabel_;
    juce::Label postValueLabel_;
    juce::Label statusLabel_;

    juce::Slider denoiseSlider_;
    juce::Slider postSlider_;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    SliderAttachment denoiseAttachment_;
    SliderAttachment postAttachment_;
};
