#pragma once

#include "Analysis/AnalysisSnapshot.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace test
{

constexpr double kSampleRate = 48000.0;

juce::AudioBuffer<float> sine (double freq, double amplitudeDb, double seconds, int channels = 2,
                               double sampleRate = kSampleRate, double phase = 0.0);

/** Concatena segmenti di sinusoide a livelli diversi (test EBU Tech 3341/3342). */
juce::AudioBuffer<float> sineSegments (double freq, const std::vector<std::pair<double, double>>& levelDbAndSeconds,
                                       double sampleRate = kSampleRate);

juce::AudioBuffer<float> whiteNoise (double seconds, float amplitude, bool correlated, int seed = 1);
juce::AudioBuffer<float> pinkNoise (double seconds, float amplitude, int seed = 1);

ma::AnalysisSnapshot analyse (const juce::AudioBuffer<float>& buffer, double sampleRate = kSampleRate);

} // namespace test
