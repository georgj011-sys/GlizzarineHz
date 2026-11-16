#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace Theme
{
    inline const juce::Colour bg       { 0xff1c1c1e };
    inline const juce::Colour panel    { 0xff242427 };
    inline const juce::Colour edge     { 0xff333337 };
    inline const juce::Colour track    { 0xff3a3a3e };
    inline const juce::Colour accent   { 0xfff2a541 };
    inline const juce::Colour text     { 0xffe8e6e3 };
    inline const juce::Colour textDim  { 0xff8e8c88 };
}

class HarmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HarmLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name);
    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class HarmonizerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HarmonizerEditor (HarmonizerProcessor&);
    ~HarmonizerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    static juce::String noteName (float hz);

    HarmonizerProcessor& proc;
    HarmLookAndFeel lnf;

    Knob input, dry, harmony, formant, consonants, velocity;
    Knob attack, release, spread;
    Knob chorus, delay, delayTime, feedback, reverb, size, output;
    juce::ComboBox lowest;
    juce::Label lowestLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lowestAttachment;

    juce::String statusNote = "-", statusVoices = "0";
    bool statusSinging = false;
    float statusLevel = 0.0f;

    juce::Rectangle<int> secVoice, secPlay, secSpace, secStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerEditor)
};
