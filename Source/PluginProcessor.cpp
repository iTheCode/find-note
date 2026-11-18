#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout FindNoteProcessor::makeParameters() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"reference", 1}, "La4 (Hz)", juce::NormalisableRange<float>{400.0f, 480.0f, 0.1f}, 440.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"low", 1}, "Desde (Hz)", juce::NormalisableRange<float>{35.0f, 1000.0f, 1.0f, 0.4f}, 35.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"high", 1}, "Hasta (Hz)", juce::NormalisableRange<float>{100.0f, 2000.0f, 1.0f, 0.4f}, 2000.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"gate", 1}, "Umbral (dB)", juce::NormalisableRange<float>{-90.0f, -20.0f, 1.0f}, -60.0f));
    return layout;
}

FindNoteProcessor::FindNoteProcessor()
    : AudioProcessor(BusesProperties().withInput("Entrada", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Salida", juce::AudioChannelSet::stereo(), true)),
      Thread("FindNote analysis"), parameters(*this, nullptr, "FindNoteState", makeParameters()) {
    low = parameters.getRawParameterValue("low");
    high = parameters.getRawParameterValue("high");
    gate = parameters.getRawParameterValue("gate");
    reference = parameters.getRawParameterValue("reference");
}

FindNoteProcessor::~FindNoteProcessor() { stopThread(-1); }

bool FindNoteProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto input = layout.getMainInputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && input == layout.getMainOutputChannelSet();
}

void FindNoteProcessor::prepareToPlay(double sampleRate, int) {
    stopThread(-1);
    fifo.reset();
    discontinuity.store(false);
    resetAnalysis.store(false);
    history = {};
    analysisChannel = 0;
    channelEnergy = {};
    analysisSampleRate = sampleRate;
    detector.prepare(sampleRate);
    frame.assign(static_cast<size_t>(detector.frameSize()), 0.0f);
    { const juce::ScopedLock lock(readingLock); latest = {}; publishedResults = {}; }
    setLatencySamples(0);
    startThread();
}

void FindNoteProcessor::releaseResources() {
    stopThread(-1);
    const juce::ScopedLock lock(readingLock);
    latest = {};
}

void FindNoteProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0) return;
    // Hysteresis avoids repeatedly switching equal-level, phase-shifted channels.
    // Never sum stereo: opposite-phase signals would cancel.
    if (buffer.getNumChannels() > 1) {
        const auto retain = std::exp(-buffer.getNumSamples() / (analysisSampleRate * 0.05));
        for (int channel = 0; channel < 2; ++channel) {
            const auto rms = buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
            channelEnergy[static_cast<size_t>(channel)] = retain * channelEnergy[static_cast<size_t>(channel)] + (1.0 - retain) * rms * rms;
        }
        const auto other = 1 - analysisChannel;
        if (channelEnergy[static_cast<size_t>(other)] > channelEnergy[static_cast<size_t>(analysisChannel)] * 4.0)
            analysisChannel = other;
    } else analysisChannel = 0;
    int start1, size1, start2, size2;
    fifo.prepareToWrite(buffer.getNumSamples(), start1, size1, start2, size2);
    const auto* input = buffer.getReadPointer(analysisChannel);
    std::copy_n(input, size1, queue.data() + start1);
    std::copy_n(input + size1, size2, queue.data() + start2);
    fifo.finishedWrite(size1 + size2);
    if (size1 + size2 < buffer.getNumSamples()) discontinuity.store(true);
    // Intentionally never write to buffer: bit-identical passthrough.
}

void FindNoteProcessor::run() {
    int filled = 0;
    auto lastInput = juce::Time::getMillisecondCounter();
    std::array<float, 4> previousSettings { low->load(), high->load(), gate->load(), reference->load() };
    while (!threadShouldExit()) {
        const std::array<float, 4> settings { low->load(), high->load(), gate->load(), reference->load() };
        const auto requestedReset = resetAnalysis.exchange(false);
        if (requestedReset || settings != previousSettings) {
            // Discard queued/overlapping audio from the previous capture.
            fifo.finishedRead(fifo.getNumReady());
            filled = 0;
            detector.resetTracking();
            history = {};
            previousSettings = settings;
            const juce::ScopedLock lock(readingLock);
            latest = {};
            publishedResults = {};
        }
        if (discontinuity.exchange(false)) {
            fifo.finishedRead(fifo.getNumReady());
            filled = 0;
            detector.resetTracking();
            const juce::ScopedLock lock(readingLock);
            latest = {};
        }
        int start1, size1, start2, size2;
        fifo.prepareToRead(detector.frameSize() - filled, start1, size1, start2, size2);
        std::copy_n(queue.data() + start1, size1, frame.data() + filled);
        std::copy_n(queue.data() + start2, size2, frame.data() + filled + size1);
        fifo.finishedRead(size1 + size2);
        filled += size1 + size2;
        if (size1 + size2 > 0) lastInput = juce::Time::getMillisecondCounter();
        if (filled == detector.frameSize()) {
            const auto value = detector.analyse(frame.data(), settings[0], settings[1], settings[2], settings[3]);
            history.add(value);
            { const juce::ScopedLock lock(readingLock); latest = value; publishedResults = history; }
            const auto hop = detector.frameSize() / 4;
            std::memmove(frame.data(), frame.data() + hop, static_cast<size_t>(detector.frameSize() - hop) * sizeof(float));
            filled -= hop;
        } else {
            if (juce::Time::getMillisecondCounter() - lastInput > 250) {
                filled = 0;
                detector.resetTracking();
                const juce::ScopedLock lock(readingLock);
                latest = {};
            }
            wait(4);
        }
    }
}

findnote::Reading FindNoteProcessor::getReading() const {
    const juce::ScopedLock lock(readingLock);
    return latest;
}

findnote::PeakResults FindNoteProcessor::getResults() const {
    const juce::ScopedLock lock(readingLock);
    return resetAnalysis.load() ? findnote::PeakResults{} : publishedResults;
}

void FindNoteProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (const auto xml = parameters.copyState().createXml()) copyXmlToBinary(*xml, destination);
}

void FindNoteProcessor::setStateInformation(const void* data, int length) {
    if (const auto xml = getXmlFromBinary(data, length))
        if (xml->hasTagName(parameters.state.getType())) parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* FindNoteProcessor::createEditor() { return new FindNoteEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FindNoteProcessor(); }
