#include "PitchDetector.h"
#include <algorithm>
#include <cmath>

namespace findnote {
namespace { constexpr double pi = 3.14159265358979323846; }

void PeakResults::add(const Reading& reading) {
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& peak = reading.peaks[i];
        if (!std::isfinite(peak.frequency) || !std::isfinite(peak.power)
            || peak.frequency <= 0.0 || peak.power <= notes[i].power) continue;
        notes[i] = peak;
        if (principal < 0 || peak.power > notes[static_cast<size_t>(principal)].power)
            principal = static_cast<int>(i);
    }
    for (size_t i = 0; i < spectrum.size(); ++i)
        spectrum[i] = std::max(spectrum[i], reading.spectrum[i]);
}

double PeakResults::percentage(int midi) const {
    if (midi < 0 || midi >= static_cast<int>(notes.size())) return 0.0;
    double total = 0.0;
    for (const auto& peak : notes) total += peak.power;
    return total > 0.0 ? 100.0 * notes[static_cast<size_t>(midi)].power / total : 0.0;
}

Note noteForFrequency(double frequency, double concertA) {
    if (!std::isfinite(frequency) || !std::isfinite(concertA)
        || frequency <= 0.0 || concertA <= 0.0) return {};
    const auto midiFloat = 69.0 + 12.0 * std::log2(frequency / concertA);
    const auto midi = static_cast<int>(std::round(midiFloat));
    return { midi, static_cast<int>(std::floor(midi / 12.0)) - 1,
             (midi % 12 + 12) % 12, (midiFloat - midi) * 100.0 };
}

void PitchDetector::prepare(double sampleRate) {
    resetTracking();
    rate = std::clamp(sampleRate, 8000.0, 384000.0);
    size = 1;
    while (size < rate * 0.17) size <<= 1;
    // A Blackman-Harris window suppresses leakage. Fourfold padding and log-power
    // interpolation refine the strongest peak without quantising it to FFT bins.
    transform.resize(static_cast<size_t>(size * 4));
    power.resize(transform.size() / 2 + 1);
    noiseFloor.resize(power.size());
    window.resize(static_cast<size_t>(size));
    windowSum = 0.0;
    for (int i = 0; i < size; ++i) {
        const auto phase = 2.0 * pi * i / (size - 1);
        window[static_cast<size_t>(i)] = 0.35875 - 0.48829 * std::cos(phase)
            + 0.14128 * std::cos(2.0 * phase) - 0.01168 * std::cos(3.0 * phase);
        windowSum += window[static_cast<size_t>(i)];
    }
}

void PitchDetector::resetTracking() {
    trackedHz = pendingHz = 0.0;
    pendingFrames = 0;
}

void PitchDetector::fft(std::vector<std::complex<double>>& values) {
    const auto n = values.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (size_t length = 2; length <= n; length <<= 1) {
        const auto step = std::polar(1.0, -2.0 * pi / static_cast<double>(length));
        for (size_t i = 0; i < n; i += length) {
            std::complex<double> phase = 1.0;
            for (size_t j = 0; j < length / 2; ++j) {
                const auto even = values[i + j];
                const auto odd = values[i + j + length / 2] * phase;
                values[i + j] = even + odd;
                values[i + j + length / 2] = even - odd;
                phase *= step;
            }
        }
    }
}

Reading PitchDetector::analyse(const float* samples, double lowHz, double highHz, float gateDb, double concertA) {
    Reading result;
    if (transform.empty() || !std::isfinite(lowHz) || !std::isfinite(highHz)
        || !std::isfinite(gateDb) || !std::isfinite(concertA) || concertA < 400.0 || concertA > 480.0
        || lowHz < 35.0 || highHz > 2000.0 || lowHz >= highHz) {
        resetTracking();
        return result;
    }
    if (lowHz != previousLowHz || highHz != previousHighHz || gateDb != previousGateDb) resetTracking();
    previousLowHz = lowHz;
    previousHighHz = highHz;
    previousGateDb = gateDb;
    double mean = 0.0;
    for (int i = 0; i < size; ++i) {
        if (!std::isfinite(samples[i])) {
            resetTracking();
            return result;
        }
        mean += samples[i];
    }
    mean /= size;
    double energy = 0.0;
    std::fill(transform.begin(), transform.end(), std::complex<double>{});
    for (int i = 0; i < size; ++i) {
        const auto value = samples[i] - mean;
        energy += value * value;
        transform[static_cast<size_t>(i)] = value * window[static_cast<size_t>(i)];
    }
    result.rmsDb = static_cast<float>(10.0 * std::log10(std::max(1e-10, energy / size)));
    if (result.rmsDb < gateDb) {
        resetTracking();
        return result;
    }

    fft(transform);
    for (size_t i = 0; i < power.size(); ++i) power[i] = std::norm(transform[i]);
    const auto binHz = rate / static_cast<double>(transform.size());
    // Display the maximum of each log-frequency interval so narrow peaks are
    // not lost between display points. Analysis and display use the same FFT.
    for (size_t i = 0; i < result.spectrum.size(); ++i) {
        const auto position = static_cast<double>(i);
        const auto count = static_cast<double>(result.spectrum.size() - 1);
        const auto fromHz = 35.0 * std::pow(2000.0 / 35.0, std::max(0.0, position - 0.5) / count);
        const auto toHz = 35.0 * std::pow(2000.0 / 35.0, std::min(count, position + 0.5) / count);
        const auto first = static_cast<size_t>(std::floor(fromHz / binHz));
        const auto last = static_cast<size_t>(std::ceil(toHz / binHz));
        const auto peak = *std::max_element(power.begin() + static_cast<ptrdiff_t>(first),
                                           power.begin() + static_cast<ptrdiff_t>(last + 1));
        const auto amplitude = 2.0 * std::sqrt(peak) / windowSum;
        result.spectrum[i] = static_cast<float>(std::clamp((20.0 * std::log10(std::max(amplitude, 1e-5)) + 80.0) / 80.0, 0.0, 1.0));
    }

    const auto first = std::max(1, static_cast<int>(std::floor(35.0 / binHz)) - 1);
    const auto last = std::min(static_cast<int>(power.size()) - 2, static_cast<int>(std::ceil(2000.0 / binHz)) + 1);
    const auto floorCount = last - first + 1;
    std::copy_n(power.begin() + first, floorCount, noiseFloor.begin());
    const auto middle = noiseFloor.begin() + floorCount / 2;
    std::nth_element(noiseFloor.begin(), middle, noiseFloor.begin() + floorCount);
    const auto floorPower = std::max(*middle, 1e-30);
    const auto isEligible = [&](double peakPower) {
        const auto amplitude = 2.0 * std::sqrt(peakPower) / windowSum;
        return peakPower / floorPower >= 25.0
            && 20.0 * std::log10(std::max(amplitude, 1e-15)) >= gateDb;
    };
    double strongestPower = 0.0;
    double strongestHz = 0.0;
    double trackedPower = 0.0, currentTrackedHz = 0.0;
    const auto samePeak = [](double a, double b) {
        return a > 0.0 && b > 0.0 && std::abs(1200.0 * std::log2(a / b)) <= 35.0;
    };
    for (int bin = first; bin <= last; ++bin) {
        const auto index = static_cast<size_t>(bin);
        if (power[index] <= power[index - 1] || power[index] < power[index + 1]) continue;
        const auto left = std::log(std::max(power[index - 1], 1e-30));
        const auto centre = std::log(std::max(power[index], 1e-30));
        const auto right = std::log(std::max(power[index + 1], 1e-30));
        const auto denominator = left - 2.0 * centre + right;
        const auto offset = std::abs(denominator) > 1e-15
            ? std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5) : 0.0;
        const auto hz = (bin + offset) * binHz;
        const auto peakPower = std::exp(centre - 0.25 * (left - right) * offset);
        // Allow only a small interpolation tolerance at range boundaries.
        if (hz >= lowHz * 0.999 && hz <= highHz * 1.001) {
            if (isEligible(peakPower)) {
                const auto midi = noteForFrequency(hz, concertA).midi;
                if (midi >= 0 && midi < static_cast<int>(result.peaks.size())) {
                    auto& peak = result.peaks[static_cast<size_t>(midi)];
                    const auto normalisedPower = 4.0 * peakPower / (windowSum * windowSum);
                    if (normalisedPower > peak.power) peak = { hz, normalisedPower };
                }
            }
            if (peakPower > strongestPower) {
                strongestPower = peakPower;
                strongestHz = hz;
            }
            if (samePeak(hz, trackedHz) && peakPower > trackedPower) {
                trackedPower = peakPower;
                currentTrackedHz = hz;
            }
        }
    }
    // Reject broad noise and peaks below the gate instead of inventing a note.
    if (strongestHz > 0.0 && isEligible(strongestPower)) {
        // Confirm only near-tied competitors (< 1 dB apart). Clear changes or
        // disappearance of the tracked peak take effect in this analysis.
        // Keep the current frequency/power of a real peak, never average notes.
        constexpr double oneDbPowerRatio = 1.2589254117941673;
        if (currentTrackedHz > 0.0 && !samePeak(strongestHz, currentTrackedHz)
            && isEligible(trackedPower) && strongestPower < trackedPower * oneDbPowerRatio) {
            pendingFrames = samePeak(strongestHz, pendingHz) ? pendingFrames + 1 : 1;
            pendingHz = strongestHz;
            if (pendingFrames < 3) {
                strongestHz = currentTrackedHz;
                strongestPower = trackedPower;
            } else {
                pendingHz = 0.0;
                pendingFrames = 0;
            }
        } else {
            pendingHz = 0.0;
            pendingFrames = 0;
        }
        trackedHz = strongestHz;
        result.frequency = strongestHz;
        result.confidence = static_cast<float>(1.0 - floorPower / strongestPower);
    } else {
        resetTracking();
    }
    return result;
}
}
