#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ma
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kInstantAlpha = 0.3;
}

void SpectrumAnalyzer::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = std::clamp (newNumChannels, 1, 2);

    // risoluzione costante indipendentemente dal sample rate:
    // FFT lunga ~1.5 Hz per bin (misure), FFT corta ~6 Hz per bin (display istantaneo)
    fftOrder = sampleRate <= 50000.0 ? 15 : (sampleRate <= 100000.0 ? 16 : 17);
    fftSize = 1 << fftOrder;
    hopSize = fftSize / 4;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    shortFftSize = fftSize / 4;
    shortHopSize = shortFftSize / 4;
    shortFft = std::make_unique<juce::dsp::FFT> (fftOrder - 2);

    auto makeHann = [] (std::vector<float>& w, int size)
    {
        w.resize ((size_t) size);
        for (int n = 0; n < size; ++n)
            w[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * n / size));
    };
    makeHann (window, fftSize);
    makeHann (shortWindow, shortFftSize);

    fftL.assign ((size_t) fftSize * 2, 0.0f);
    fftR.assign ((size_t) fftSize * 2, 0.0f);
    shortFftL.assign ((size_t) shortFftSize * 2, 0.0f);
    shortFftR.assign ((size_t) shortFftSize * 2, 0.0f);
    inputL.assign ((size_t) fftSize, 0.0f);
    inputR.assign ((size_t) fftSize, 0.0f);

    const size_t numBins = (size_t) fftSize / 2 + 1;
    sumPL.assign (numBins, 0.0);
    sumPR.assign (numBins, 0.0);
    sumCross.assign (numBins, 0.0);
    instantP.assign ((size_t) shortFftSize / 2 + 1, 0.0);
    shortRawP.assign ((size_t) shortFftSize / 2 + 1, 0.0);
    framePower.assign (numBins, 0.0);
    timeline.reserve (600);

    // Parseval con finestra di Hann (somma w^2 = 3N/8): lo spettro unilatero di una sinusoide di ampiezza A
    // somma a 3 A^2 N^2 / 16 -> normalizziamo perché la potenza di banda sia l'RMS^2 (sinusoide 0 dBFS = -3 dB)
    powerNorm = 8.0 / (3.0 * (double) fftSize * (double) fftSize);
    shortPowerNorm = 8.0 / (3.0 * (double) shortFftSize * (double) shortFftSize);

    emaCoeff = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));

    reset();
}

void SpectrumAnalyzer::reset()
{
    std::fill (inputL.begin(), inputL.end(), 0.0f);
    std::fill (inputR.begin(), inputR.end(), 0.0f);
    inputPos = 0;
    samplesSinceFrame = 0;
    samplesSinceShortFrame = 0;
    samplesBuffered = 0;

    std::fill (sumPL.begin(), sumPL.end(), 0.0);
    std::fill (sumPR.begin(), sumPR.end(), 0.0);
    std::fill (sumCross.begin(), sumCross.end(), 0.0);
    std::fill (instantP.begin(), instantP.end(), 0.0);
    framesAccumulated = 0;

    for (auto& bin : loudnessThirds) bin.fill (0.0);
    loudnessFrames.fill (0.0);
    currentShortTerm = kSilenceDb;
    for (auto& band : burstHistogram) band.fill ({});
    timeline.clear();

    emaLR = emaLL = emaRR = 0.0;
    gonio.fill (0.0f);
    gonioPos = 0;
    gonioDecimation = 0;
}

void SpectrumAnalyzer::process (const float* left, const float* right, int numSamples, float shortTermLufs, double songTime)
{
    const bool stereo = numChannels == 2 && right != nullptr;
    currentShortTerm = shortTermLufs;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i];
        const float r = stereo ? right[i] : l;

        inputL[(size_t) inputPos] = l;
        inputR[(size_t) inputPos] = r;
        inputPos = (inputPos + 1) % fftSize;
        samplesBuffered = std::min (samplesBuffered + 1, fftSize);

        emaLR += emaCoeff * ((double) l * r - emaLR);
        emaLL += emaCoeff * ((double) l * l - emaLL);
        emaRR += emaCoeff * ((double) r * r - emaRR);

        if (++gonioDecimation >= 2)
        {
            gonioDecimation = 0;
            gonio[(size_t) gonioPos * 2] = l;
            gonio[(size_t) gonioPos * 2 + 1] = r;
            gonioPos = (gonioPos + 1) % kGoniometerPoints;
        }

        if (++samplesSinceShortFrame >= shortHopSize && samplesBuffered >= shortFftSize)
        {
            samplesSinceShortFrame = 0;
            performShortFrame (songTime + (double) (i + 1) / sampleRate);
        }

        if (++samplesSinceFrame >= hopSize && samplesBuffered >= fftSize)
        {
            samplesSinceFrame = 0;
            performLongFrame();
        }
    }
}

void SpectrumAnalyzer::performShortFrame (double frameEndTime)
{
    // ultimi shortFftSize campioni del buffer circolare
    for (int n = 0; n < shortFftSize; ++n)
    {
        const auto idx = (size_t) ((inputPos - shortFftSize + n + fftSize) % fftSize);
        shortFftL[(size_t) n] = inputL[idx] * shortWindow[(size_t) n];
        shortFftR[(size_t) n] = inputR[idx] * shortWindow[(size_t) n];
    }
    std::fill (shortFftL.begin() + shortFftSize, shortFftL.end(), 0.0f);
    std::fill (shortFftR.begin() + shortFftSize, shortFftR.end(), 0.0f);

    shortFft->performRealOnlyForwardTransform (shortFftL.data(), true);
    shortFft->performRealOnlyForwardTransform (shortFftR.data(), true);

    const size_t numBins = instantP.size();
    for (size_t k = 0; k < numBins; ++k)
    {
        const double lr = shortFftL[k * 2], li = shortFftL[k * 2 + 1];
        const double rr = shortFftR[k * 2], ri = shortFftR[k * 2 + 1];
        const double scale = (k == 0 || k == numBins - 1) ? 1.0 : 2.0;
        const double p = 0.5 * (lr * lr + li * li + rr * rr + ri * ri) * scale;
        shortRawP[k] = p;
        instantP[k] += kInstantAlpha * (p - instantP[k]);
    }

    // comportamento nel tempo delle bande: livello di ogni banda rispetto al totale in questo frame
    const double df = shortBinWidth();
    const double total = bandPower (shortRawP, 20.0, 20000.0, df) * shortPowerNorm;
    if (total <= 1e-7)   // sotto -70 dB: silenzio, code, fade
        return;

    SecondBin* second = nullptr;
    const double centreTime = frameEndTime - 0.5 * shortFftSize / sampleRate;
    if (centreTime >= 0.0 && centreTime < (double) kMaxTimelineSeconds)
    {
        const auto index = (size_t) centreTime;
        if (index >= timeline.size())
            timeline.resize (index + 1);
        second = &timeline[index];
        second->totalP += total;
        ++second->frames;
    }

    for (int b = 0; b < kNumBands; ++b)
    {
        const double p = bandPower (shortRawP, kBandEdges[(size_t) b], kBandEdges[(size_t) b + 1], df) * shortPowerNorm;
        const double relDb = p > 0.0 ? 10.0 * std::log10 (p / total) : -200.0;
        const int bin = std::clamp ((int) std::floor ((relDb + 80.0) * 2.0), 0, kBurstBins - 1);

        auto& h = burstHistogram[(size_t) b][(size_t) bin];
        h.frames += 1.0;
        h.bandP += p;
        h.totalP += total;

        if (second != nullptr)
            second->bandP[(size_t) b] += p;
    }
}

void SpectrumAnalyzer::performLongFrame()
{
    for (int n = 0; n < fftSize; ++n)
    {
        const auto idx = (size_t) ((inputPos + n) % fftSize);   // dal più vecchio al più recente
        fftL[(size_t) n] = inputL[idx] * window[(size_t) n];
        fftR[(size_t) n] = inputR[idx] * window[(size_t) n];
    }
    std::fill (fftL.begin() + fftSize, fftL.end(), 0.0f);
    std::fill (fftR.begin() + fftSize, fftR.end(), 0.0f);

    fft->performRealOnlyForwardTransform (fftL.data(), true);
    fft->performRealOnlyForwardTransform (fftR.data(), true);

    const size_t numBins = sumPL.size();
    for (size_t k = 0; k < numBins; ++k)
    {
        const double lr = fftL[k * 2], li = fftL[k * 2 + 1];
        const double rr = fftR[k * 2], ri = fftR[k * 2 + 1];

        // le frequenze positive contano doppio (spettro unilatero), tranne DC e Nyquist
        const double scale = (k == 0 || k == numBins - 1) ? 1.0 : 2.0;
        const double pl = (lr * lr + li * li) * scale;
        const double pr = (rr * rr + ri * ri) * scale;
        const double cross = (lr * rr + li * ri) * scale;

        sumPL[k] += pl;
        sumPR[k] += pr;
        sumCross[k] += cross;
        framePower[k] = 0.5 * (pl + pr);
    }

    ++framesAccumulated;

    // terzi d'ottava del frame, raggruppati per short-term loudness (spettro della sezione più forte)
    if (currentShortTerm > -70.0f)
    {
        const auto bin = (size_t) std::clamp ((int) std::floor ((currentShortTerm + 70.0f) * 2.0f), 0, kLoudnessBins - 1);
        for (int i = 0; i < kNumThirdOctaves; ++i)
        {
            const double fc = thirdOctaveCentre (i);
            loudnessThirds[bin][(size_t) i] += bandPower (framePower, fc * std::pow (2.0, -1.0 / 6.0), fc * std::pow (2.0, 1.0 / 6.0), longBinWidth());
        }
        loudnessFrames[bin] += 1.0;
    }
}

double SpectrumAnalyzer::bandPower (const std::vector<double>& bins, double lo, double hi, double df) const
{
    // somma con sovrapposizione frazionaria tra il bin [f - df/2, f + df/2] e la banda [lo, hi]
    hi = std::min (hi, sampleRate * 0.5);
    if (hi <= lo)
        return 0.0;

    const int kStart = std::max (0, (int) std::floor (lo / df - 0.5));
    const int kEnd = std::min ((int) bins.size() - 1, (int) std::ceil (hi / df + 0.5));

    double sum = 0.0;
    for (int k = kStart; k <= kEnd; ++k)
    {
        const double binLo = (k - 0.5) * df, binHi = (k + 0.5) * df;
        const double overlap = std::min (hi, binHi) - std::max (lo, binLo);
        if (overlap > 0.0)
            sum += bins[(size_t) k] * (overlap / df);
    }
    return sum;
}

double SpectrumAnalyzer::bandPowerLongTerm (double lo, double hi) const
{
    if (framesAccumulated == 0)
        return 0.0;

    const double norm = powerNorm / (double) framesAccumulated;
    return 0.5 * (bandPower (sumPL, lo, hi, longBinWidth()) + bandPower (sumPR, lo, hi, longBinWidth())) * norm;
}

void SpectrumAnalyzer::fillSnapshot (AnalysisSnapshot& s) const
{
    // correlazione istantanea
    const double denomInst = std::sqrt (emaLL * emaRR);
    s.correlationInstant = denomInst > 1e-12 ? (float) std::clamp (emaLR / denomInst, -1.0, 1.0) : 1.0f;
    s.goniometer = gonio;
    s.goniometerWritePos = gonioPos;

    // spettro istantaneo (display)
    for (int i = 0; i < kNumDisplayPoints; ++i)
    {
        const double f = displayFrequency (i);
        const double p = bandPower (instantP, f * std::pow (2.0, -1.0 / 12.0), f * std::pow (2.0, 1.0 / 12.0), shortBinWidth()) * shortPowerNorm;
        s.spectrumInstantDb[(size_t) i] = powerToDb (p, -120.0f);
    }

    fillBandDynamics (s);

    if (framesAccumulated == 0)
        return;

    const double norm = powerNorm / (double) framesAccumulated;

    std::vector<double> sumP (sumPL.size());
    for (size_t k = 0; k < sumP.size(); ++k)
        sumP[k] = 0.5 * (sumPL[k] + sumPR[k]);

    auto lt = [&] (double lo, double hi) { return bandPower (sumP, lo, hi, longBinWidth()) * norm; };

    // spettro long-term (display), potenza per 1/6 d'ottava
    for (int i = 0; i < kNumDisplayPoints; ++i)
    {
        const double f = displayFrequency (i);
        s.spectrumLongTermDb[(size_t) i] = powerToDb (lt (f * std::pow (2.0, -1.0 / 12.0), f * std::pow (2.0, 1.0 / 12.0)), -120.0f);
    }

    // Mid e Side long-term dagli stessi accumulatori: |(L+R)/2|^2 = (PL+PR+2C)/4, |(L-R)/2|^2 = (PL+PR-2C)/4
    {
        std::vector<double> midP (sumPL.size()), sideP (sumPL.size());
        for (size_t k = 0; k < midP.size(); ++k)
        {
            midP[k] = std::max (0.0, 0.25 * (sumPL[k] + sumPR[k] + 2.0 * sumCross[k]));
            sideP[k] = std::max (0.0, 0.25 * (sumPL[k] + sumPR[k] - 2.0 * sumCross[k]));
        }
        for (int i = 0; i < kNumDisplayPoints; ++i)
        {
            const double f = displayFrequency (i);
            const double lo = f * std::pow (2.0, -1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
            s.spectrumMidDb[(size_t) i] = powerToDb (bandPower (midP, lo, hi, longBinWidth()) * norm, -120.0f);
            s.spectrumSideDb[(size_t) i] = powerToDb (bandPower (sideP, lo, hi, longBinWidth()) * norm, -120.0f);
        }
    }

    const double total = lt (20.0, 20000.0);

    // terzi d'ottava normalizzati (media dB 63 Hz - 12.5 kHz = 0)
    std::array<double, kNumThirdOctaves> thirdDb {};
    for (int i = 0; i < kNumThirdOctaves; ++i)
    {
        const double fc = thirdOctaveCentre (i);
        thirdDb[(size_t) i] = powerToDb (lt (fc * std::pow (2.0, -1.0 / 6.0), fc * std::pow (2.0, 1.0 / 6.0)), -120.0f);
    }
    double mean = 0.0;
    for (int i = 5; i <= 28; ++i) mean += thirdDb[(size_t) i];
    mean /= 24.0;
    for (int i = 0; i < kNumThirdOctaves; ++i)
        s.thirdOctaveDb[(size_t) i] = (float) (thirdDb[(size_t) i] - mean);

    // tilt: regressione lineare dB vs log2(f) tra 100 Hz e 10 kHz
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        for (int i = 7; i <= 27; ++i)
        {
            const double x = std::log2 (thirdOctaveCentre (i) / 1000.0);
            const double y = thirdDb[(size_t) i];
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
        }
        const double d = n * sxx - sx * sx;
        s.spectralTiltDbPerOct = d != 0.0 ? (float) ((n * sxy - sx * sy) / d) : 0.0f;
    }

    // centroide spettrale
    {
        const double df = sampleRate / fftSize;
        double num = 0.0, den = 0.0;
        for (size_t k = 1; k < sumP.size(); ++k)
        {
            const double f = k * df;
            if (f < 20.0 || f > 20000.0) continue;
            num += f * sumP[k];
            den += sumP[k];
        }
        s.spectralCentroidHz = den > 0.0 ? (float) (num / den) : 0.0f;
    }

    if (total > 0.0)
    {
        for (int b = 0; b < kNumBands; ++b)
            s.bandEnergyDb[(size_t) b] = powerToDb (lt (kBandEdges[(size_t) b], kBandEdges[(size_t) b + 1]) / total, -120.0f);

        s.subRumbleDb = powerToDb (lt (5.0, 30.0) / total, -120.0f);
        s.ultraHighDb = powerToDb (lt (16000.0, 20000.0) / total, -120.0f);
    }

    // stereo per banda e globale
    auto stereoStats = [&] (double lo, double hi, float& corr, float& width, float& monoLoss)
    {
        const double pl = bandPower (sumPL, lo, hi, longBinWidth());
        const double pr = bandPower (sumPR, lo, hi, longBinWidth());
        const double c = bandPower (sumCross, lo, hi, longBinWidth());
        const double sum = pl + pr;

        if (sum <= 1e-20)
        {
            corr = 1.0f; width = 0.0f; monoLoss = 0.0f;
            return;
        }

        const double denom = std::sqrt (pl * pr);
        corr = denom > 1e-20 ? (float) std::clamp (c / denom, -1.0, 1.0) : 0.0f;
        width = (float) std::clamp (100.0 * (sum - 2.0 * c) / (2.0 * sum), 0.0, 100.0);
        monoLoss = powerToDb (std::max (0.0, (sum + 2.0 * c) / (2.0 * sum)), -60.0f);
    };

    for (int b = 0; b < kNumBands; ++b)
        stereoStats (kBandEdges[(size_t) b], kBandEdges[(size_t) b + 1],
                     s.bandCorrelation[(size_t) b], s.bandWidthPercent[(size_t) b], s.bandMonoLossDb[(size_t) b]);

    stereoStats (20.0, 20000.0, s.correlation, s.widthPercent, s.monoLossDb);

    {
        float unusedLoss = 0.0f;
        stereoStats (20.0, 100.0, s.lowEndCorrelation, s.lowEndWidthPercent, unusedLoss);
    }

    {
        const double pl = bandPower (sumPL, 20.0, 20000.0, longBinWidth());
        const double pr = bandPower (sumPR, 20.0, 20000.0, longBinWidth());
        s.balanceDb = (pl > 0.0 && pr > 0.0) ? (float) (10.0 * std::log10 (pl / pr)) : 0.0f;
    }

    fillLoudestSection (s);
    fillResonances (s, sumP);
}

void SpectrumAnalyzer::fillBandDynamics (AnalysisSnapshot& s) const
{
    // quanto del livello di ogni banda dipende dal 10% dei frame in cui la banda è più prominente
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& out = s.bandDynamics[(size_t) b];
        out = {};
        const auto& histogram = burstHistogram[(size_t) b];

        double frames = 0.0, bandP = 0.0, totalP = 0.0;
        for (const auto& h : histogram)
        {
            frames += h.frames;
            bandP += h.bandP;
            totalP += h.totalP;
        }
        if (frames < 20.0 || bandP <= 0.0 || totalP <= 0.0)
            continue;

        double remaining = 0.2 * frames, topBand = 0.0, topTotal = 0.0;
        for (int k = kBurstBins - 1; k >= 0 && remaining > 0.0; --k)
        {
            const auto& h = histogram[(size_t) k];
            if (h.frames <= 0.0)
                continue;
            const double take = std::min (h.frames, remaining);
            topBand += h.bandP * take / h.frames;
            topTotal += h.totalP * take / h.frames;
            remaining -= take;
        }

        const double restBand = bandP - topBand, restTotal = totalP - topTotal;
        if (restBand > 0.0 && restTotal > 0.0)
            out.burstDb = (float) std::max (0.0, 10.0 * std::log10 (bandP / totalP) - 10.0 * std::log10 (restBand / restTotal));
    }

    // dove: la finestra di qualche secondo in cui la banda è più sopra la sua mediana sul brano
    const int numSeconds = (int) timeline.size();
    std::vector<int> validSeconds;
    for (int i = 0; i < numSeconds; ++i)
        if (timeline[(size_t) i].frames > 0 && timeline[(size_t) i].totalP / timeline[(size_t) i].frames > 1e-7)
            validSeconds.push_back (i);

    if (validSeconds.size() < 8)
        return;

    std::vector<double> excess ((size_t) numSeconds);
    std::vector<double> sorted;
    sorted.reserve (validSeconds.size());

    for (int b = 0; b < kNumBands; ++b)
    {
        std::fill (excess.begin(), excess.end(), std::numeric_limits<double>::quiet_NaN());
        sorted.clear();
        for (int i : validSeconds)
        {
            const auto& sec = timeline[(size_t) i];
            const double rel = 10.0 * std::log10 (std::max (sec.bandP[(size_t) b], 1e-30) / sec.totalP);
            excess[(size_t) i] = rel;
            sorted.push_back (rel);
        }
        std::nth_element (sorted.begin(), sorted.begin() + (long) (sorted.size() / 2), sorted.end());
        const double median = sorted[sorted.size() / 2];
        for (int i : validSeconds)
            excess[(size_t) i] -= median;

        constexpr int windowSeconds = 4;
        double best = -1.0e9;
        int bestStart = -1;
        for (int start = 0; start + windowSeconds <= numSeconds; ++start)
        {
            double sum = 0.0;
            int n = 0;
            for (int k = start; k < start + windowSeconds; ++k)
                if (! std::isnan (excess[(size_t) k])) { sum += excess[(size_t) k]; ++n; }
            if (n >= 3 && sum / n > best)
            {
                best = sum / n;
                bestStart = start;
            }
        }
        if (bestStart < 0 || best <= 0.0)
            continue;

        // allarga finché i secondi vicini restano chiaramente in eccesso (massimo 40 s)
        int lo = bestStart, hi = bestStart + windowSeconds - 1;
        const double threshold = std::max (1.0, 0.5 * best);
        auto above = [&] (int k) { return k >= 0 && k < numSeconds && ! std::isnan (excess[(size_t) k]) && excess[(size_t) k] >= threshold; };
        if (best >= threshold)
        {
            while (hi - lo > 1 && ! above (lo)) ++lo;
            while (hi - lo > 1 && ! above (hi)) --hi;
        }
        while (hi - lo < 40 && above (lo - 1)) --lo;
        while (hi - lo < 40 && above (hi + 1)) ++hi;

        double sum = 0.0;
        int n = 0;
        for (int k = lo; k <= hi; ++k)
            if (! std::isnan (excess[(size_t) k])) { sum += excess[(size_t) k]; ++n; }

        auto& out = s.bandDynamics[(size_t) b];
        out.worstStartSec = (float) lo;
        out.worstEndSec = (float) (hi + 1);
        out.worstExcessDb = n > 0 ? (float) (sum / n) : 0.0f;
    }
}

void SpectrumAnalyzer::fillLoudestSection (AnalysisSnapshot& s) const
{
    s.loudestSectionSeconds = 0.0f;

    double total = 0.0;
    for (auto f : loudnessFrames)
        total += f;

    const double hopSeconds = hopSize / sampleRate;
    if (total * hopSeconds < 4.0)
        return;

    // dal livello più forte in giù fino al 25% dei frame (almeno 8 s)
    const double wanted = std::min (total, std::max (0.25 * total, 8.0 / hopSeconds));
    std::array<double, kNumThirdOctaves> sum {};
    double taken = 0.0;
    for (int bin = kLoudnessBins - 1; bin >= 0 && taken < wanted; --bin)
    {
        const double frames = loudnessFrames[(size_t) bin];
        if (frames <= 0.0)
            continue;
        const double fraction = std::min (1.0, (wanted - taken) / frames);   // l'ultimo bin solo in parte
        for (int i = 0; i < kNumThirdOctaves; ++i)
            sum[(size_t) i] += loudnessThirds[(size_t) bin][(size_t) i] * fraction;
        taken += frames * fraction;
    }

    std::array<double, kNumThirdOctaves> db {};
    for (int i = 0; i < kNumThirdOctaves; ++i)
        db[(size_t) i] = powerToDb (sum[(size_t) i] * powerNorm / taken, -120.0f);

    double mean = 0.0;
    for (int i = 5; i <= 28; ++i) mean += db[(size_t) i];
    mean /= 24.0;
    for (int i = 0; i < kNumThirdOctaves; ++i)
        s.thirdOctaveLoudestDb[(size_t) i] = (float) (db[(size_t) i] - mean);

    s.loudestSectionSeconds = (float) (taken * hopSeconds);
}

void SpectrumAnalyzer::fillResonances (AnalysisSnapshot& s, const std::vector<double>& sumP) const
{
    s.resonances.clear();
    s.keyPitchClass = -1;
    s.tuningCents = 0.0f;

    const double norm = powerNorm / (double) framesAccumulated;
    const double df = longBinWidth();
    auto lt = [&] (double lo, double hi) { return bandPower (sumP, lo, hi, df) * norm; };

    // picchi: spettro fine (1/24 ott.) contro baseline larga (1/2 ott.), 80 Hz - 8 kHz
    const double fineBw = std::pow (2.0, 1.0 / 48.0);
    const double broadBw = std::pow (2.0, 1.0 / 4.0);
    auto fineDb  = [&] (double f) { return powerToDb (lt (f / fineBw, f * fineBw) / (2.0 * std::log2 (fineBw)), -150.0f); };
    auto broadDb = [&] (double f) { return powerToDb (lt (f / broadBw, f * broadBw) / (2.0 * std::log2 (broadBw)), -150.0f); };
    auto excessAt = [&] (double f) { return (f > 25.0 && f < 0.45 * sampleRate) ? (double) (fineDb (f) - broadDb (f)) : 0.0; };

    std::vector<Resonance> candidates;
    {
        constexpr int pointsPerOct = 24;
        const int numPoints = (int) std::floor (std::log2 (8000.0 / 80.0) * pointsPerOct) + 1;
        std::vector<double> fine ((size_t) numPoints), broad ((size_t) numPoints), freqs ((size_t) numPoints);

        for (int i = 0; i < numPoints; ++i)
        {
            const double f = 80.0 * std::pow (2.0, (double) i / pointsPerOct);
            freqs[(size_t) i] = f;
            fine[(size_t) i] = fineDb (f);
            broad[(size_t) i] = broadDb (f);
        }

        for (int i = 1; i < numPoints - 1; ++i)
        {
            const double excess = fine[(size_t) i] - broad[(size_t) i];
            if (excess > 4.0 && fine[(size_t) i] > fine[(size_t) i - 1] && fine[(size_t) i] >= fine[(size_t) i + 1])
                candidates.push_back ({ (float) freqs[(size_t) i], (float) excess });
        }

        std::sort (candidates.begin(), candidates.end(), [] (const Resonance& a, const Resonance& b) { return a.excessDb > b.excessDb; });
        if (candidates.size() > 12)
            candidates.resize (12);
    }

    if (candidates.empty())
        return;

    // La griglia di ricerca ha passo 1/24 d'ottava (50 cent): la frequenza va affinata sui bin FFT
    // (interpolazione parabolica sul massimo locale), altrimenti lo scarto dalla nota sarebbe solo il passo della griglia.
    for (auto& c : candidates)
    {
        const int lo = std::max (1, (int) std::floor (c.frequency / std::pow (2.0, 1.0 / 48.0) / df));
        const int hi = std::min ((int) sumP.size() - 2, (int) std::ceil (c.frequency * std::pow (2.0, 1.0 / 48.0) / df));
        int best = -1;
        for (int k = lo; k <= hi; ++k)
            if (best < 0 || sumP[(size_t) k] > sumP[(size_t) best])
                best = k;
        if (best < 1 || sumP[(size_t) best] <= 0.0 || sumP[(size_t) best - 1] <= 0.0 || sumP[(size_t) best + 1] <= 0.0)
            continue;

        const double a = 10.0 * std::log10 (sumP[(size_t) best - 1]);
        const double b = 10.0 * std::log10 (sumP[(size_t) best]);
        const double g = 10.0 * std::log10 (sumP[(size_t) best + 1]);
        const double denom = a - 2.0 * b + g;
        const double offset = denom < 0.0 ? std::clamp (0.5 * (a - g) / denom, -0.5, 0.5) : 0.0;
        c.frequency = (float) ((best + offset) * df);
    }

    // --- intonazione: media circolare (pesata sull'eccesso) degli scarti dei picchi dalla griglia temperata ------
    constexpr double twoPi = 6.283185307179586;
    auto midiOf = [] (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); };
    auto centsOf = [&] (double f) { const double m = midiOf (f); return 100.0 * (m - std::round (m)); };

    // escludendo il picco in esame, così un picco isolato non "sposta" la griglia su se stesso
    auto tuningExcluding = [&] (int skip, int minPeaks)
    {
        double sx = 0.0, sy = 0.0, w = 0.0;
        int used = 0;
        for (int j = 0; j < (int) candidates.size(); ++j)
        {
            if (j == skip) continue;
            const double a = twoPi * centsOf (candidates[(size_t) j].frequency) / 100.0;
            const double wj = candidates[(size_t) j].excessDb;
            sx += wj * std::cos (a);
            sy += wj * std::sin (a);
            w += wj;
            ++used;
        }
        if (used < minPeaks || w <= 0.0 || std::sqrt (sx * sx + sy * sy) / w < 0.5)
            return 0.0;   // pochi picchi o scarti sparsi: nessuna intonazione coerente
        return 100.0 * std::atan2 (sy, sx) / twoPi;
    };

    const double globalTuning = tuningExcluding (-1, 3);
    s.tuningCents = (float) globalTuning;

    // --- croma 50 Hz - 2 kHz, ogni ottava con lo stesso peso: la classe più presente è la tonica probabile -----
    auto pitchClassOf = [&] (double f, double tuning) { return ((int) std::lround (midiOf (f) - tuning / 100.0) % 12 + 12) % 12; };
    auto octaveOf = [] (double f) { return std::clamp ((int) std::floor (std::log2 (f / 50.0)), 0, 5); };

    std::array<double, 6> octaveTotal {};
    const int kLo = (int) std::ceil (50.0 / df), kHi = std::min ((int) sumP.size() - 1, (int) std::floor (2000.0 / df));
    for (int k = kLo; k <= kHi; ++k)
        octaveTotal[(size_t) octaveOf (k * df)] += sumP[(size_t) k];

    auto chromaWeight = [&] (int k) { const double t = octaveTotal[(size_t) octaveOf (k * df)]; return t > 0.0 ? sumP[(size_t) k] / t : 0.0; };

    std::array<double, 12> chroma {};
    for (int k = kLo; k <= kHi; ++k)
        chroma[(size_t) pitchClassOf (k * df, globalTuning)] += chromaWeight (k);

    auto keyOf = [] (const std::array<double, 12>& c)
    {
        const auto it = std::max_element (c.begin(), c.end());
        double mean = 0.0;
        for (auto v : c) mean += v;
        mean /= 12.0;
        return (mean > 0.0 && *it > 1.5 * mean) ? (int) (it - c.begin()) : -1;
    };
    s.keyPitchClass = keyOf (chroma);

    // --- classificazione: nota del brano o risonanza -----------------------------------------------------------
    for (int i = 0; i < (int) candidates.size(); ++i)
    {
        auto& c = candidates[(size_t) i];
        const double tuning = tuningExcluding (i, 2);
        const double m = midiOf (c.frequency) - tuning / 100.0;
        c.midiNote = (int) std::lround (m);
        c.cents = (float) (100.0 * (m - c.midiNote));
        const int pitchClass = ((c.midiNote % 12) + 12) % 12;

        // tonica stimata senza il contributo del picco stesso
        auto chromaWithout = chroma;
        const int lo = (int) std::ceil (c.frequency / std::pow (2.0, 1.0 / 24.0) / df);
        const int hi = (int) std::floor (c.frequency * std::pow (2.0, 1.0 / 24.0) / df);
        for (int k = std::max (lo, kLo); k <= std::min (hi, kHi); ++k)
            chromaWithout[(size_t) pitchClassOf (k * df, globalTuning)] -= chromaWeight (k);
        for (auto& v : chromaWithout) v = std::max (0.0, v);

        // le tre classi di nota più presenti nel brano (senza il picco stesso): tonica e note dell'accordo principale
        bool mainPitchClass = false;
        if (keyOf (chromaWithout) >= 0)
        {
            std::array<int, 12> order {};
            for (int k = 0; k < 12; ++k) order[(size_t) k] = k;
            std::partial_sort (order.begin(), order.begin() + 3, order.end(),
                               [&] (int x, int y) { return chromaWithout[(size_t) x] > chromaWithout[(size_t) y]; });
            mainPitchClass = std::find (order.begin(), order.begin() + 3, pitchClass) != order.begin() + 3;
        }

        // Una nota cade sulla griglia temperata; un picco quasi esatto (entro ±12 cent) basta, uno meno preciso
        // deve avere conferme: un'altra ottava nello spettro o essere tra le note principali del brano.
        const bool octavePartner = excessAt (c.frequency * 0.5) > 3.0 || excessAt (c.frequency * 2.0) > 3.0;
        const float offGrid = std::abs (c.cents);
        c.musical = offGrid <= 12.0f || (offGrid <= 25.0f && (octavePartner || mainPitchClass));
    }

    if (candidates.size() > 5)
        candidates.resize (5);
    s.resonances = std::move (candidates);
}

} // namespace ma
