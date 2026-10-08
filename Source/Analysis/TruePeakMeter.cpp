#include "TruePeakMeter.h"

#include <algorithm>
#include <cmath>

namespace ma
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum)
                break;
        }
        return sum;
    }

    double clampUnit (double r) { return std::clamp (r, -1.0, 1.0); }

    const double kOver1Threshold = std::pow (10.0, -1.0 / 20.0);
    constexpr double kOver0Threshold = 1.0;
}

void TruePeakMeter::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = std::clamp (newNumChannels, 1, 2);
    factor = sampleRate < 88000.0 ? 4 : (sampleRate < 176000.0 ? 2 : 1);

    // Filtro passa-basso sinc con taglio alla Nyquist originale, finestra di Kaiser (beta 8 ~ 80 dB)
    // centro su un indice intero multiplo del fattore: la fase 0 coincide con i campioni originali
    const int totalTaps = factor * tapsPerPhase;
    const double centre = (double) (factor * (tapsPerPhase / 2));
    const double beta = 8.0;
    const double i0Beta = besselI0 (beta);

    std::vector<double> h ((size_t) totalTaps);
    for (int n = 0; n < totalTaps; ++n)
    {
        const double t = (n - centre) / (double) factor;
        const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
        const double r = clampUnit ((n - centre) / centre);
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
        h[(size_t) n] = sinc * w;
    }

    phases.assign ((size_t) factor, std::vector<double> ((size_t) tapsPerPhase));
    for (int p = 0; p < factor; ++p)
    {
        double sum = 0.0;
        for (int k = 0; k < tapsPerPhase; ++k)
        {
            phases[(size_t) p][(size_t) k] = h[(size_t) (p + k * factor)];
            sum += h[(size_t) (p + k * factor)];
        }
        // guadagno unitario in continua per ogni fase
        for (auto& c : phases[(size_t) p])
            c /= sum;
    }

    recentBlockLength = std::max (1, (int) std::lround (sampleRate * 0.1));
    reset();
}

void TruePeakMeter::reset()
{
    for (auto& ch : channels)
    {
        ch.history.assign ((size_t) tapsPerPhase * 2, 0.0);
        ch.writePos = 0;
        ch.above1 = ch.above0 = false;
    }

    truePeak = { 0.0, 0.0 };
    samplePeak = { 0.0, 0.0 };
    recentBlockMax.fill (0.0);
    recentBlockIndex = 0;
    recentBlockCounter = 0;
    currentBlockMax = 0.0;
    overs1Events = overs0Events = 0;
}

float TruePeakMeter::processSample (ChannelState& state, double x) noexcept
{
    // scrive il campione in due posizioni per avere sempre una finestra contigua
    state.history[(size_t) state.writePos] = x;
    state.history[(size_t) (state.writePos + tapsPerPhase)] = x;

    // finestra: history[writePos + 1 .. writePos + tapsPerPhase], dal più vecchio al più recente
    const double* window = state.history.data() + state.writePos + 1;
    state.writePos = (state.writePos + 1) % tapsPerPhase;

    double peak = 0.0;

    if (factor == 1)
    {
        peak = std::abs (x);
    }
    else
    {
        for (int p = 0; p < factor; ++p)
        {
            const double* coeffs = phases[(size_t) p].data();
            double acc = 0.0;
            // coeffs[k] moltiplica x[n - k] -> il più recente è window[tapsPerPhase - 1]
            for (int k = 0; k < tapsPerPhase; ++k)
                acc += coeffs[k] * window[tapsPerPhase - 1 - k];
            peak = std::max (peak, std::abs (acc));
        }
    }

    if (peak > kOver1Threshold)  { if (! state.above1) ++overs1Events; state.above1 = true; }
    else if (peak < kOver1Threshold * 0.94) state.above1 = false;   // isteresi ~0.5 dB

    if (peak > kOver0Threshold)  { if (! state.above0) ++overs0Events; state.above0 = true; }
    else if (peak < kOver0Threshold * 0.94) state.above0 = false;

    return (float) peak;
}

void TruePeakMeter::process (const float* left, const float* right, int numSamples)
{
    const bool stereo = numChannels == 2 && right != nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i];
        samplePeak[0] = std::max (samplePeak[0], std::abs (l));
        const double tpL = processSample (channels[0], l);
        truePeak[0] = std::max (truePeak[0], tpL);
        double framePeak = tpL;

        if (stereo)
        {
            const double r = right[i];
            samplePeak[1] = std::max (samplePeak[1], std::abs (r));
            const double tpR = processSample (channels[1], r);
            truePeak[1] = std::max (truePeak[1], tpR);
            framePeak = std::max (framePeak, tpR);
        }
        else
        {
            samplePeak[1] = samplePeak[0];
            truePeak[1] = truePeak[0];
        }

        currentBlockMax = std::max (currentBlockMax, framePeak);

        if (++recentBlockCounter >= recentBlockLength)
        {
            recentBlockMax[(size_t) recentBlockIndex] = currentBlockMax;
            recentBlockIndex = (recentBlockIndex + 1) % (int) recentBlockMax.size();
            recentBlockCounter = 0;
            currentBlockMax = 0.0;
        }
    }
}

float TruePeakMeter::getRecentTruePeakDb() const noexcept
{
    double m = currentBlockMax;
    for (auto v : recentBlockMax)
        m = std::max (m, v);
    return gainToDb (m);
}

} // namespace ma
