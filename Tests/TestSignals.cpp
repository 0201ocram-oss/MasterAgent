#include "TestSignals.h"
#include "Analysis/ReferenceAnalyzer.h"

namespace test
{

juce::AudioBuffer<float> sine (double freq, double amplitudeDb, double seconds, int channels, double sampleRate, double phase)
{
    const int n = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> b (channels, n);
    const double amp = juce::Decibels::decibelsToGain (amplitudeDb, -400.0);
    for (int i = 0; i < n; ++i)
    {
        const auto v = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sampleRate + phase));
        for (int ch = 0; ch < channels; ++ch)
            b.setSample (ch, i, v);
    }
    return b;
}

juce::AudioBuffer<float> sineSegments (double freq, const std::vector<std::pair<double, double>>& segments, double sampleRate)
{
    int total = 0;
    for (const auto& [db, secs] : segments)
        total += (int) (secs * sampleRate);

    juce::AudioBuffer<float> b (2, total);
    int pos = 0;
    for (const auto& [db, secs] : segments)
    {
        const int n = (int) (secs * sampleRate);
        const double amp = juce::Decibels::decibelsToGain (db, -400.0);
        for (int i = 0; i < n; ++i, ++pos)
        {
            const auto v = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * pos / sampleRate));
            b.setSample (0, pos, v);
            b.setSample (1, pos, v);
        }
    }
    return b;
}

juce::AudioBuffer<float> whiteNoise (double seconds, float amplitude, bool correlated, int seed)
{
    juce::Random rng (seed);
    const int n = (int) (seconds * kSampleRate);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float l = (rng.nextFloat() * 2.0f - 1.0f) * amplitude;
        const float r = correlated ? l : (rng.nextFloat() * 2.0f - 1.0f) * amplitude;
        b.setSample (0, i, l);
        b.setSample (1, i, r);
    }
    return b;
}

juce::AudioBuffer<float> pinkNoise (double seconds, float amplitude, int seed)
{
    // filtro di Paul Kellet (accurato entro ±0.05 dB sopra ~10 Hz a 44.1/48 kHz)
    juce::Random rng (seed);
    const int n = (int) (seconds * kSampleRate);
    juce::AudioBuffer<float> b (2, n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = rng.nextDouble() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        const auto v = (float) (pink * 0.11 * amplitude);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

ma::AnalysisSnapshot analyse (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    ma::AnalysisSnapshot s;
    ma::analyseBuffer (buffer, sampleRate, s);
    return s;
}

} // namespace test
