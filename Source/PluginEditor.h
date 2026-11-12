#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class FindNoteLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    FindNoteLookAndFeel();
    juce::Font displayFont(float height) const;
    juce::Font getTextButtonFont(juce::TextButton&, int height) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
private:
    juce::Typeface::Ptr cartoonTypeface;
};

class FindNoteEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit FindNoteEditor(FindNoteProcessor&);
    ~FindNoteEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override { reading = owner.getReading(); repaint(); }
    FindNoteProcessor& owner;
    juce::Image background;
    FindNoteLookAndFeel theme;
    std::array<juce::Slider, 4> sliders;
    std::array<juce::Label, 4> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> attachments;
    juce::TextButton reset { "Restablecer" };
    findnote::Reading reading;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FindNoteEditor)
};
