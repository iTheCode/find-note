#include "PitchDetector.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {
constexpr double pi = 3.14159265358979323846;
int checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
std::vector<float> tone(int size, double rate, double hz, int kind = 0) {
    std::vector<float> data(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) {
        const auto phase = 2.0 * pi * hz * i / rate + 0.43;
        double value = 0.6 * std::sin(phase);
        if (kind == 1) value = 0.12 * std::sin(phase) + 0.6 * std::sin(phase * 2) + 0.25 * std::sin(phase * 3);
        if (kind == 2) value = 0.45 * std::sin(phase * 2) + 0.35 * std::sin(phase * 3) + 0.15 * std::sin(phase * 4);
        if (kind == 3) value = std::sin(phase) >= 0 ? 0.5 : -0.5;
        data[static_cast<size_t>(i)] = static_cast<float>(value);
    }
    return data;
}

std::vector<float> pair(int size, double rate, double firstHz, double firstAmplitude,
                        double secondHz, double secondAmplitude, double phase = 0.43) {
    std::vector<float> data(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) {
        const auto t = 2.0 * pi * i / rate;
        data[static_cast<size_t>(i)] = static_cast<float>(firstAmplitude * std::sin(t * firstHz + phase)
            + secondAmplitude * std::sin(t * secondHz + phase * 0.7));
    }
    return data;
}

void testTracking(findnote::PitchDetector& detector, double rate) {
    const auto left = pair(detector.frameSize(), rate, 220.0, 0.505, 440.0, 0.5);
    const auto right = pair(detector.frameSize(), rate, 220.0, 0.5, 440.0, 0.505);
    const auto frequencyIs = [&](const std::vector<float>& signal, double hz, const char* message) {
        require(std::abs(detector.analyse(signal.data()).frequency - hz) < 0.5, message);
    };
    detector.resetTracking();
    frequencyIs(left, 220.0, "Initial dominant peak appears without extra confirmation");
    for (int i = 0; i < 16; ++i)
        frequencyIs(i % 2 == 0 ? right : left, 220.0, "Near-tied alternating peaks must not flicker");
    frequencyIs(right, 220.0, "Challenger frame one keeps current peak");
    frequencyIs(right, 220.0, "Challenger frame two keeps current peak");
    frequencyIs(right, 440.0, "Persistent challenger accepted on third analysis");
    frequencyIs(pair(detector.frameSize(), rate, 220.0, 0.7, 440.0, 0.4), 220.0,
                "A clear level lead changes immediately");
    frequencyIs(tone(detector.frameSize(), rate, 880.0), 880.0,
                "Disappearing previous peak must not leave a stale note");
    frequencyIs(left, 220.0, "New two-peak signal acquired");
    frequencyIs(right, 220.0, "Near tie begins confirmation");
    const auto silence = std::vector<float>(static_cast<size_t>(detector.frameSize()), 0.0f);
    frequencyIs(silence, 0.0, "Silence clears displayed note immediately");
    frequencyIs(right, 440.0, "Silence also clears tracking history");
    detector.resetTracking();
    frequencyIs(left, 220.0, "Tracking resets explicitly");
    const auto restricted = detector.analyse(right.data(), 430.0, 450.0);
    require(std::abs(restricted.frequency - 440.0) < 0.5, "Manual range changes bypass pending tracking");
    detector.prepare(rate);
    frequencyIs(right, 440.0, "Prepare clears history for a new playback session");
}

void testResults(findnote::PitchDetector& detector, double rate) {
    detector.prepare(rate);
    findnote::PeakResults results;
    const auto signal = pair(detector.frameSize(), rate, 220.0, 0.6, 440.0, 0.3);
    const auto reading = detector.analyse(signal.data());
    results.add(reading);
    require(results.principal == 57, "Highest recorded peak is A3");
    require(std::abs(results.notes[69].frequency - 440.0) < 0.1, "Keep weaker simultaneous A4 peak too");
    require(std::abs(results.percentage(57) - 80.0) < 0.1 && std::abs(results.percentage(69) - 20.0) < 0.1,
            "Percentages use normalised peak power: .6^2 / (.6^2 + .3^2) = 80%");
    require(std::abs(results.notes[57].power - 0.36) < 0.001, "Power comparable across sample rates/window sizes");
    const auto originalScore = results.percentage(57);
    for (int i = 0; i < 20; ++i) results.add(detector.analyse(signal.data()));
    require(results.percentage(57) == originalScore, "Looping same audio does not inflate scores");
    const auto weaker = pair(detector.frameSize(), rate, 329.6276, 0.4, 440.0, 0.0);
    results.add(detector.analyse(weaker.data()));
    require(results.principal == 57 && results.notes[64].power > 0.15, "Later weaker note retained without replacing principal");
    const auto louder = pair(detector.frameSize(), rate, 329.6276, 0.8, 440.0, 0.0);
    results.add(detector.analyse(louder.data()));
    require(results.principal == 64, "New historical maximum becomes principal");
    double sum = 0.0;
    for (int midi = 0; midi < 128; ++midi) sum += results.percentage(midi);
    require(std::abs(sum - 100.0) < 1e-9, "All stored note percentages sum to 100");
    const auto peakBefore = results.notes[64];
    results.add(detector.analyse(std::vector<float>(static_cast<size_t>(detector.frameSize()), 0.0f).data()));
    require(results.principal == 64 && results.notes[64].power == peakBefore.power,
            "Silence retains captured result");
    auto invalid = findnote::Reading{};
    invalid.peaks[69] = {440.0, std::numeric_limits<double>::infinity()};
    results.add(invalid);
    require(results.principal == 64, "Invalid peaks cannot poison history");
    results = {};
    require(results.principal == -1 && results.percentage(69) == 0.0, "New capture clears notes and percentages");
    results.add(detector.analyse(signal.data(), 430.0, 450.0));
    require(results.principal == 69 && results.notes[57].power == 0.0, "Only peaks in selected range are stored");
    results = {};
    results.add(detector.analyse(signal.data(), 35.0, 2000.0, -8.0f));
    require(results.principal == 57 && results.notes[69].power == 0.0, "Gate applies to each captured peak");
    results = {};
    const auto calibrated = tone(detector.frameSize(), rate, 480.0);
    results.add(detector.analyse(calibrated.data(), 35.0, 2000.0, -60.0f, 480.0));
    require(results.principal == 69, "Stored note mapping respects La4 calibration");
    // Equal historical maxima retain the first winner rather than oscillating.
    results = {};
    findnote::Reading a, b;
    a.peaks[69] = {440.0, 0.5};
    b.peaks[57] = {220.0, 0.5};
    results.add(a);
    results.add(b);
    require(results.principal == 69, "Exact tie retains previous principal");
}
}

int main() {
    try {
        findnote::PitchDetector detector;
        double worstCents = 0.0;
        for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
            detector.prepare(rate);
            const auto windowSeconds = detector.frameSize() / rate;
            require(windowSeconds >= 0.17 && windowSeconds <= 0.186,
                    "Analysis window must match documented 171-186 ms (rounded)");
            for (const double hz : {35.0, 41.2034, 55.0, 82.4069, 110.0, 220.0, 261.6256, 440.0, 442.0, 880.0, 1760.0, 2000.0}) {
                for (int kind = 0; kind < 3; ++kind) {
                    const auto data = tone(detector.frameSize(), rate, hz, kind);
                    const auto result = detector.analyse(data.data());
                    // Select the strongest spectral peak, including a dominant
                    // second harmonic, rather than infer the fundamental.
                    const auto expected = kind != 0 && hz * 2.0 <= 2000.0 ? hz * 2.0
                        : kind == 2 ? 0.0 : hz;
                    if (expected == 0.0) {
                        require(result.frequency == 0.0, "All partials outside search range must be rejected");
                        continue;
                    }
                    const auto cents = std::abs(1200.0 * std::log2(result.frequency / expected));
                    worstCents = std::max(worstCents, cents);
                    require(result.frequency > 0.0 && cents < 5.0,
                            "Pitch error: rate=" + std::to_string(rate) + " hz=" + std::to_string(hz)
                            + " kind=" + std::to_string(kind) + " detected=" + std::to_string(result.frequency));
                }
            }
            auto silence = std::vector<float>(static_cast<size_t>(detector.frameSize()), 0.0f);
            require(detector.analyse(silence.data()).frequency == 0.0, "Silence must not produce a note");
            std::fill(silence.begin(), silence.end(), 0.5f);
            require(detector.analyse(silence.data()).frequency == 0.0, "DC must not produce a note");
            silence[10] = std::numeric_limits<float>::quiet_NaN();
            require(detector.analyse(silence.data()).frequency == 0.0, "Nonfinite input rejected");
            std::mt19937 random(42);
            std::uniform_real_distribution<float> noise(-0.4f, 0.4f);
            for (auto& value : silence) value = noise(random);
            require(detector.analyse(silence.data()).frequency == 0.0, "Noise must not produce a note");
            auto data = tone(detector.frameSize(), rate, 440.0);
            require(detector.analyse(data.data(), std::numeric_limits<double>::quiet_NaN(), 1000.0).frequency == 0.0, "Nonfinite range rejected");
            require(detector.analyse(data.data(), 600.0, 500.0).frequency == 0.0, "Inverted range rejected");
            const auto restricted = detector.analyse(data.data(), 430.0, 450.0);
            require(std::abs(restricted.frequency - 440.0) < 1.0, "Restricted range finds tone");
            require(detector.analyse(data.data(), 500.0, 600.0).frequency == 0.0, "Out-of-range tone must not create a sidelobe note");
            for (auto& value : data) value *= 0.0001f;
            require(detector.analyse(data.data()).frequency == 0.0, "Gate rejects quiet input");
            data = tone(detector.frameSize(), rate, 110.0, 3);
            require(std::abs(detector.analyse(data.data()).frequency - 110.0) < 0.5, "Square wave fundamental");
            data = tone(detector.frameSize(), rate, 329.6276);
            for (auto& value : data) value += noise(random) * 0.1f + 0.2f;
            require(std::abs(detector.analyse(data.data()).frequency - 329.6276) < 1.0, "Noisy tone with DC");

            auto mixed = tone(detector.frameSize(), rate, 220.0, 1);
            const auto dominant = detector.analyse(mixed.data());
            require(std::abs(dominant.frequency - 440.0) < 0.5, "Auto follows strongest harmonic");
            const auto manualRange = detector.analyse(mixed.data(), 200.0, 240.0);
            require(std::abs(manualRange.frequency - 220.0) < 0.5, "Optional range can select weaker peak");
            const auto peakIndex = static_cast<double>(std::max_element(dominant.spectrum.begin(), dominant.spectrum.end()) - dominant.spectrum.begin());
            const auto displayedHz = 35.0 * std::pow(2000.0 / 35.0, peakIndex / (dominant.spectrum.size() - 1));
            require(std::abs(1200.0 * std::log2(displayedHz / dominant.frequency)) < 40.0, "Display maximum agrees with automatic reference");
            for (int step = 0; step < 2; ++step) {
                auto a = tone(detector.frameSize(), rate, 220.0);
                const auto b = tone(detector.frameSize(), rate, 329.6276);
                for (size_t i = 0; i < a.size(); ++i) a[i] = a[i] * (step == 0 ? 0.3f : 1.0f) + b[i] * (step == 0 ? 1.0f : 0.3f);
                require(std::abs(detector.analyse(a.data()).frequency - (step == 0 ? 329.6276 : 220.0)) < 0.5,
                        "Automatic reference follows changing non-harmonic dominant peak");
            }
            for (int seed = 0; seed < 12; ++seed) {
                random.seed(static_cast<unsigned int>(seed));
                for (auto& value : silence) value = noise(random);
                require(detector.analyse(silence.data()).frequency == 0.0, "Broadband noise rejected across seeds");
            }
            for (const int harmonic : {3, 5}) {
                for (const double phase : {0.0, 0.43, 2.1}) {
                    const auto signal = pair(detector.frameSize(), rate, 110.0, 0.12,
                                             110.0 * harmonic, 0.6, phase);
                    const auto reading = detector.analyse(signal.data());
                    require(std::abs(reading.frequency - 110.0 * harmonic) < 0.5,
                            "Third/fifth dominant harmonic must remain the spectral reference");
                    const auto note = findnote::noteForFrequency(reading.frequency);
                    require(note.midi == (harmonic == 3 ? 64 : 73),
                            "110-Hz fundamental with dominant 330/550 Hz maps to E4/C#5, not A2");
                }
            }
            testTracking(detector, rate);
            testResults(detector, rate);
        }
        const auto a = findnote::noteForFrequency(440.0);
        require(a.midi == 69 && a.octave == 4 && a.pitchClass == 9 && std::abs(a.cents) < 0.001, "A440 note mapping");
        require(std::abs(findnote::noteForFrequency(442.0, 442.0).cents) < 0.001, "Custom reference");
        require(std::abs(findnote::noteForFrequency(440.0 * std::pow(2.0, 23.0 / 1200.0)).cents - 23.0) < 0.001, "Cents mapping");
        const auto c = findnote::noteForFrequency(261.625565);
        require(c.pitchClass == 0 && c.octave == 4, "C4 octave convention");
        std::cout << "PASS: " << checks << " checks; worst tonal error " << worstCents << " cents\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
