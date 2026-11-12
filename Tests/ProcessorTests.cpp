#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void feed(FindNoteProcessor& processor, int channels, double hz, int stereoMode = 0) {
    juce::AudioBuffer<float> buffer(channels, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 80; ++block) {
        for (int sample = 0; sample < 256; ++sample) {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * hz * (block * 256 + sample) / 48000.0;
            const auto harmonic = stereoMode == 4 ? 2.0 : stereoMode == 5 ? 3.0 : 5.0;
            const auto value = static_cast<float>(stereoMode >= 4 ? 0.1 * std::sin(phase) + 0.6 * std::sin(phase * harmonic) : 0.5 * std::sin(phase));
            for (int channel = 0; channel < channels; ++channel) {
                auto channelValue = channel == 1 && stereoMode == 1 ? -value : value;
                if (channel == 1 && stereoMode == 2)
                    channelValue = static_cast<float>(0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * hz * (block * 256 + sample) / 48000.0));
                if (channel == 0 && stereoMode == 3) channelValue = 0.0f;
                buffer.setSample(channel, sample, channelValue);
            }
        }
        juce::AudioBuffer<float> original;
        original.makeCopyOf(buffer);
        processor.processBlock(buffer, midi);
        for (int channel = 0; channel < channels; ++channel)
            require(std::memcmp(buffer.getReadPointer(channel), original.getReadPointer(channel), 256 * sizeof(float)) == 0, "Audio changed");
        juce::Thread::sleep(6);
    }
    const auto frequency = processor.getReading().frequency;
    const auto harmonic = stereoMode == 4 ? 2.0 : stereoMode == 5 ? 3.0 : stereoMode == 6 ? 5.0 : 1.0;
    require(std::abs(frequency - hz * harmonic) < 1.0, "Processor automatic peak incorrect");
}
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    try {
        FindNoteProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        feed(processor, 2, 440.0, 1);
        feed(processor, 2, 110.0, 2);
        feed(processor, 2, 440.0, 3);
        feed(processor, 2, 110.0, 5);
        feed(processor, 2, 110.0, 6);
        feed(processor, 2, 220.0, 4);
        require(processor.getLatencySamples() == 0, "Passthrough must have zero latency");
        auto* reference = processor.parameters.getParameter("reference");
        reference->setValueNotifyingHost(reference->convertTo0to1(442.0f));
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        FindNoteProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(std::abs(restored.parameters.getRawParameterValue("reference")->load() - 442.0f) < 0.01f, "State not restored");
        const char invalid[] = "invalid state";
        restored.setStateInformation(invalid, sizeof(invalid));
        require(std::abs(restored.parameters.getRawParameterValue("reference")->load() - 442.0f) < 0.01f, "Invalid state changed parameters");
        if (argc > 1) {
            // Preview: automatic 440-Hz peak above the weaker 220-Hz fundamental.
            reference->setValueNotifyingHost(reference->convertTo0to1(440.0f));
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
            const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
            juce::FileOutputStream output(destination);
            require(output.openedOk() && juce::PNGImageFormat().writeImageToStream(snapshot, output), "Preview write failed");
        }
        juce::Thread::sleep(350);
        require(processor.getReading().frequency == 0.0, "Stale note after transport stops");
        processor.releaseResources();
        auto mono = processor.getBusesLayout();
        mono.inputBuses.set(0, juce::AudioChannelSet::mono());
        mono.outputBuses.set(0, juce::AudioChannelSet::mono());
        require(processor.setBusesLayout(mono), "Mono layout rejected");
        processor.prepareToPlay(48000.0, 256);
        feed(processor, 1, 261.625565);
        juce::AudioBuffer<float> silence(1, 256);
        juce::MidiBuffer midi;
        silence.clear();
        for (int i = 0; i < 60; ++i) {
            processor.processBlock(silence, midi);
            juce::Thread::sleep(6);
        }
        require(processor.getReading().frequency == 0.0, "Silence must clear note");
        processor.releaseResources();
        std::cout << "PASS: dominant 2nd/3rd/5th harmonics, stereo, opposite phase, mono, passthrough, reference persistence, invalid state, silence, stopped transport, lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
