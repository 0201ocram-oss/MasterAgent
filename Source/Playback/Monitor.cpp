#include "Monitor.h"
#include "../Common/Text.h"

namespace ma
{

void Monitor::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    step = (float) (1.0 / std::max (1.0, fadeSeconds * sampleRate));

    const juce::dsp::ProcessSpec spec { sampleRate, 4096, 2 };
    highPass.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lowPass.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    highPass.prepare (spec);
    lowPass.prepare (spec);

    current = 0;
    mix = 0.0f;
    applyChoice (requested.load());
}

void Monitor::setChoice (MonitorChoice choice) noexcept
{
    if (choice.mode == MonitorMode::band && ! juce::isPositiveAndBelow (choice.band, kNumBands))
        choice = {};
    if (choice.mode != MonitorMode::band)
        choice.band = -1;
    requested.store (pack (choice));
}

MonitorChoice Monitor::getChoice() const noexcept
{
    return unpack (requested.load());
}

void Monitor::applyChoice (int packed) noexcept
{
    current = packed;
    const auto c = unpack (packed);
    if (c.mode != MonitorMode::band)
        return;

    const double nyquistLimit = 0.45 * sampleRate;
    const double lo = kBandEdges[(size_t) c.band], hi = kBandEdges[(size_t) c.band + 1];
    highPass.setCutoffFrequency ((float) std::min (lo, nyquistLimit));
    useLowPass = hi < nyquistLimit;
    if (useLowPass)
        lowPass.setCutoffFrequency ((float) hi);
    highPass.reset();
    lowPass.reset();
}

void Monitor::process (float* left, float* right, int numSamples) noexcept
{
    const int wanted = requested.load();

    // a riposo e spento: l'uscita non si tocca
    if (wanted == 0 && current == 0 && mix <= 0.0f)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        // cambio di scelta: prima si torna al segnale normale, poi si passa alla nuova
        if (wanted != current && mix <= 0.0f)
            applyChoice (wanted);

        const float target = (wanted == current && current != 0) ? 1.0f : 0.0f;
        if (mix < target)      mix = std::min (target, mix + step);
        else if (mix > target) mix = std::max (target, mix - step);

        const float l = left[i];
        const float r = right != nullptr ? right[i] : l;
        float wl = l, wr = r;

        switch ((MonitorMode) (current / 16))
        {
            case MonitorMode::mono:
                wl = wr = 0.5f * (l + r);
                break;
            case MonitorMode::side:
                wl = wr = 0.5f * (l - r);
                break;
            case MonitorMode::band:
                wl = highPass.processSample (0, l);
                wr = highPass.processSample (1, r);
                if (useLowPass)
                {
                    wl = lowPass.processSample (0, wl);
                    wr = lowPass.processSample (1, wr);
                }
                break;
            case MonitorMode::off:
                break;
        }

        left[i] = l + (wl - l) * mix;
        if (right != nullptr)
            right[i] = r + (wr - r) * mix;
    }
}

namespace
{
    int bandOfFrequency (float hz)
    {
        for (int b = 0; b < kNumBands; ++b)
            if (hz < kBandEdges[(size_t) b + 1])
                return b;
        return kNumBands - 1;
    }
}

MonitorChoice monitorForFinding (const Finding& f, const ComparisonResult& result)
{
    const auto prefix = f.key.upToFirstOccurrenceOf (":", false, false);
    const int index = f.key.fromFirstOccurrenceOf (":", false, false).getIntValue();
    auto band = [] (int b) { return juce::isPositiveAndBelow (b, kNumBands) ? MonitorChoice { MonitorMode::band, b } : MonitorChoice(); };

    if (prefix == "band" || prefix == "bandWidth" || prefix == "bandCorr")
        return band (index);

    // mossa EQ: la banda che sposta di più
    if (prefix == "eq" && juce::isPositiveAndBelow (index, (int) result.eqMoves.size()))
    {
        const auto& bands = result.eqMoves[(size_t) index].bands;
        int main = -1;
        for (int b : bands)
            if (main < 0 || std::abs (result.bandTonalDelta[(size_t) b]) > std::abs (result.bandTonalDelta[(size_t) main]))
                main = b;
        return band (main);
    }

    if (prefix == "resonance")
        return band (bandOfFrequency ((float) index));

    if (f.key == highlight::rumble)
        return band (0);

    if (f.key == highlight::width || f.key == highlight::correlation || f.key == highlight::lowEndWidth)
        return { MonitorMode::side, -1 };

    if (f.key == highlight::monoLoss)
        return { MonitorMode::mono, -1 };

    return {};
}

juce::String describeMonitor (MonitorChoice c)
{
    switch (c.mode)
    {
        case MonitorMode::mono: return "Mono";
        case MonitorMode::side: return "Side";
        case MonitorMode::band:
        {
            if (! juce::isPositiveAndBelow (c.band, kNumBands))
                return {};
            auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String (juce::roundToInt (f)); };
            return "solo"_t + " " + kBandNames[(size_t) c.band] + " (" + hz (kBandEdges[(size_t) c.band]) + "-"
                   + hz (kBandEdges[(size_t) c.band + 1]) + " Hz)";
        }
        case MonitorMode::off:  break;
    }
    return {};
}

} // namespace ma
