#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PitchDetector.h"

class FindNoteProcessor final : public juce::AudioProcessor, private juce::Thread {
public:
    FindNoteProcessor();
    ~FindNoteProcessor() override;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FindNote"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    findnote::Reading getReading() const;
    juce::AudioProcessorValueTreeState parameters;
private:
    void run() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    static constexpr int capacity = 131072;
    juce::AbstractFifo fifo { capacity };
    std::array<float, capacity> queue {};
    findnote::PitchDetector detector;
    std::vector<float> frame;
    mutable juce::CriticalSection readingLock;
    findnote::Reading latest;
    std::atomic<float>* low = nullptr;
    std::atomic<float>* high = nullptr;
    std::atomic<float>* gate = nullptr;
    std::atomic<bool> discontinuity { false };
    int analysisChannel = 0;
    std::array<double, 2> channelEnergy {};
    double analysisSampleRate = 48000.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FindNoteProcessor)
};
