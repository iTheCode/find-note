#pragma once

#include <array>
#include <complex>
#include <vector>

namespace findnote {
struct Reading {
    double frequency = 0.0;
    float confidence = 0.0f;
    float rmsDb = -100.0f;
    std::array<float, 192> spectrum {};
};

struct Note {
    int midi = 0;
    int octave = 0;
    int pitchClass = 0;
    double cents = 0.0;
};
Note noteForFrequency(double frequency, double concertA = 440.0);

// Used exclusively by the analysis worker; no allocation during analyse().
class PitchDetector {
public:
    void prepare(double sampleRate);
    void resetTracking();
    int frameSize() const { return size; }
    Reading analyse(const float* samples, double lowHz = 35.0,
                    double highHz = 2000.0, float gateDb = -60.0f);
private:
    static void fft(std::vector<std::complex<double>>& values);
    double rate = 48000.0;
    int size = 4096;
    std::vector<std::complex<double>> transform;
    std::vector<double> window, power, noiseFloor;
    double windowSum = 0.0;
    double trackedHz = 0.0, pendingHz = 0.0;
    int pendingFrames = 0;
    double previousLowHz = 35.0, previousHighHz = 2000.0;
    float previousGateDb = -60.0f;
};
}
