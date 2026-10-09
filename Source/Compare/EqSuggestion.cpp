#include "EqSuggestion.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ma
{

namespace
{
    constexpr int kFirstFit = 1;    // 25 Hz
    constexpr int kLastFit = 29;    // 16 kHz

    double responseDb (EqMove::Type type, double f0, double gainDb, double q, double f)
    {
        const double a = std::pow (10.0, gainDb / 40.0);
        const std::complex<double> s (0.0, f / f0);
        std::complex<double> h;

        switch (type)
        {
            case EqMove::Type::bell:
                h = (s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0);
                break;
            case EqMove::Type::lowShelf:
            {
                const double sa = std::sqrt (a);
                h = a * (s * s + s * (sa / q) + a) / (a * s * s + s * (sa / q) + 1.0);
                break;
            }
            case EqMove::Type::highShelf:
            {
                const double sa = std::sqrt (a);
                h = a * (a * s * s + s * (sa / q) + 1.0) / (s * s + s * (sa / q) + a);
                break;
            }
        }
        return 20.0 * std::log10 (std::max (1e-9, std::abs (h)));
    }

    /** Media di potenza su 5 punti del terzo d'ottava: una campana stretta vale meno del suo picco, come nella misura. */
    double thirdOctaveResponseDb (EqMove::Type type, double f0, double gainDb, double q, int third)
    {
        const double fc = thirdOctaveCentre (third);
        double sum = 0.0;
        for (int k = -2; k <= 2; ++k)
            sum += std::pow (10.0, responseDb (type, f0, gainDb, q, fc * std::pow (2.0, k / 15.0)) / 10.0);
        return 10.0 * std::log10 (sum / 5.0);
    }

    struct Candidate
    {
        EqMove::Type type;
        double frequency, q;
        std::array<double, kNumThirdOctaves> unit {};   // risposta per dB di guadagno (a 3 dB, quasi lineare)
    };

    const std::vector<Candidate>& candidates()
    {
        static const std::vector<Candidate> list = []
        {
            std::vector<Candidate> c;
            auto add = [&c] (EqMove::Type type, double f, double q)
            {
                Candidate cand { type, f, q, {} };
                for (int i = kFirstFit; i <= kLastFit; ++i)
                    cand.unit[(size_t) i] = thirdOctaveResponseDb (type, f, 3.0, q, i) / 3.0;
                c.push_back (cand);
            };

            for (int k = -30; k <= 22; ++k)   // 31 Hz - 12.7 kHz, passo 1/6 d'ottava
                for (double q : { 0.5, 0.7, 1.0, 1.4, 2.0 })
                    add (EqMove::Type::bell, 1000.0 * std::pow (2.0, k / 6.0), q);
            for (double f : { 60.0, 80.0, 100.0, 150.0, 200.0, 300.0 })
                add (EqMove::Type::lowShelf, f, 0.707);
            for (double f : { 3000.0, 5000.0, 8000.0, 10000.0, 12000.0 })
                add (EqMove::Type::highShelf, f, 0.707);
            return c;
        }();
        return list;
    }

    /** Frequenza "da manopola": 2 cifre significative sotto 1 kHz, poi passi da 100 Hz. */
    float niceFrequency (float f)
    {
        if (f < 100.0f)  return std::round (f);
        if (f < 1000.0f) return std::round (f / 5.0f) * 5.0f;
        return std::round (f / 100.0f) * 100.0f;
    }
}

juce::String EqMove::typeName() const
{
    switch (type)
    {
        case Type::bell:      return "Bell";
        case Type::lowShelf:  return "Low shelf";
        case Type::highShelf: return "High shelf";
    }
    return {};
}

juce::String EqMove::describe (float gain) const
{
    const float f = niceFrequency (frequency);
    const auto hz = f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : juce::String ((int) f) + " Hz";
    juce::String text;
    text << typeName() << " " << hz << " " << (gain > 0.0f ? "+" : "") << juce::String (gain, 1) << " dB";
    if (type == Type::bell)
        text << ", Q " << juce::String (q, 1);
    return text;
}

float eqMoveResponseDb (const EqMove& move, float frequency)
{
    return (float) responseDb (move.type, move.frequency, move.gainDb, move.q, frequency);
}

float eqMoveThirdOctaveDb (const EqMove& move, int thirdOctave)
{
    return (float) thirdOctaveResponseDb (move.type, move.frequency, move.gainDb, move.q, thirdOctave);
}

std::vector<EqMove> suggestEqMoves (const std::array<float, kNumThirdOctaves>& tonalDelta,
                                    const std::array<float, kNumBands>& bandTolerance,
                                    const EqFitOptions& options)
{
    // correzione necessaria (target - master) e peso di ogni terzo d'ottava
    std::array<double, kNumThirdOctaves> residual {}, weight {};
    std::array<int, kNumThirdOctaves> bandOf {};
    for (int i = 0; i < kNumThirdOctaves; ++i)
    {
        bandOf[(size_t) i] = bandOfThirdOctave (i);
        if (i < kFirstFit || i > kLastFit || bandOf[(size_t) i] < 0)
            continue;
        const double tol = std::max (0.25f, bandTolerance[(size_t) bandOf[(size_t) i]]);
        residual[(size_t) i] = -tonalDelta[(size_t) i];
        weight[(size_t) i] = 1.0 / (tol * tol);
    }

    auto bandMean = [&] (const std::array<double, kNumThirdOctaves>& v, int band)
    {
        double sum = 0.0;
        int n = 0;
        for (int i = kFirstFit; i <= kLastFit; ++i)
            if (bandOf[(size_t) i] == band) { sum += v[(size_t) i]; ++n; }
        return n > 0 ? sum / n : 0.0;
    };

    // un po' più severo della diagnosi per banda, così una banda segnalata ha quasi sempre la sua mossa
    auto anyBandOut = [&]
    {
        for (int b = 0; b < kNumBands; ++b)
            if (std::abs (bandMean (residual, b)) > 0.8 * bandTolerance[(size_t) b])
                return true;
        return false;
    };

    const auto initial = residual;
    std::vector<EqMove> moves;

    while ((int) moves.size() < options.maxMoves && anyBandOut())
    {
        double error = 0.0;
        for (int i = kFirstFit; i <= kLastFit; ++i)
            error += weight[(size_t) i] * residual[(size_t) i] * residual[(size_t) i];

        const Candidate* best = nullptr;
        double bestGain = 0.0, bestReduction = 0.0;
        for (const auto& c : candidates())
        {
            double num = 0.0, den = 0.0;
            for (int i = kFirstFit; i <= kLastFit; ++i)
            {
                num += weight[(size_t) i] * residual[(size_t) i] * c.unit[(size_t) i];
                den += weight[(size_t) i] * c.unit[(size_t) i] * c.unit[(size_t) i];
            }
            if (den <= 0.0)
                continue;
            const double g = std::clamp (num / den, (double) -options.maxGainDb, (double) options.maxGainDb);
            const double reduction = 2.0 * g * num - g * g * den;
            if (reduction > bestReduction)
            {
                bestReduction = reduction;
                bestGain = g;
                best = &c;
            }
        }

        if (best == nullptr || bestReduction < 0.1 * error)
            break;

        // affina il guadagno sulla risposta esatta (la forma di una campana RBJ cambia un po' con il guadagno)
        std::array<double, kNumThirdOctaves> response {};
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            double num = 0.0, den = 0.0;
            for (int i = kFirstFit; i <= kLastFit; ++i)
            {
                response[(size_t) i] = thirdOctaveResponseDb (best->type, best->frequency, bestGain, best->q, i);
                num += weight[(size_t) i] * (residual[(size_t) i] - response[(size_t) i]) * best->unit[(size_t) i];
                den += weight[(size_t) i] * best->unit[(size_t) i] * best->unit[(size_t) i];
            }
            bestGain = std::clamp (bestGain + num / den, (double) -options.maxGainDb, (double) options.maxGainDb);
        }
        for (int i = kFirstFit; i <= kLastFit; ++i)
            response[(size_t) i] = thirdOctaveResponseDb (best->type, best->frequency, bestGain, best->q, i);

        if (std::abs (bestGain) < options.minGainDb)
            break;

        EqMove move;
        move.type = best->type;
        move.frequency = (float) best->frequency;
        move.gainDb = (float) bestGain;
        move.q = (float) best->q;
        move.shownGainDb = move.gainDb;

        // bande fuori tolleranza che la mossa porta verso il target (per almeno un terzo di quanto serve)
        for (int b = 0; b < kNumBands; ++b)
        {
            const double needed = bandMean (initial, b);
            const double applied = bandMean (response, b);
            if (std::abs (needed) > 0.5 * bandTolerance[(size_t) b]
                && std::abs (applied) >= std::max (0.5, 0.35 * std::abs (needed))
                && needed * applied > 0.0)
                move.bands.push_back (b);
        }

        for (int i = kFirstFit; i <= kLastFit; ++i)
            residual[(size_t) i] -= response[(size_t) i];

        moves.push_back (std::move (move));
    }

    // due mosse dello stesso tipo e segno entro 1/3 d'ottava (due passi della griglia) sono una sola regolazione;
    // due shelf, che agiscono su tutto ciò che sta oltre la frequenza, anche entro un'ottava
    for (size_t i = 0; i < moves.size(); ++i)
    {
        for (size_t j = i + 1; j < moves.size();)
        {
            auto& a = moves[i];
            const auto& b = moves[j];
            const float distance = std::abs (std::log2 (a.frequency / b.frequency));
            if (a.type == b.type && a.gainDb * b.gainDb > 0.0f && (distance <= 0.34f || (a.type != EqMove::Type::bell && distance <= 1.0f)))
            {
                const float wa = std::abs (a.gainDb), wb = std::abs (b.gainDb);
                a.frequency = std::pow (2.0f, (wa * std::log2 (a.frequency) + wb * std::log2 (b.frequency)) / (wa + wb));
                a.gainDb = std::clamp (a.gainDb + b.gainDb, -options.maxGainDb, options.maxGainDb);
                a.shownGainDb = a.gainDb;
                a.q = std::min (a.q, b.q);
                for (int band : b.bands)
                    if (std::find (a.bands.begin(), a.bands.end(), band) == a.bands.end())
                        a.bands.push_back (band);
                std::sort (a.bands.begin(), a.bands.end());
                moves.erase (moves.begin() + (long) j);
            }
            else
            {
                ++j;
            }
        }
    }

    return moves;
}

} // namespace ma
