#include "StreamingPreview.h"
#include "AnalysisSnapshot.h"

#include <algorithm>

namespace ma
{

const std::vector<StreamingPlatform>& getStreamingPlatforms()
{
    // Valori di riferimento pubblicati dalle piattaforme (impostazioni di default lato utente)
    static const std::vector<StreamingPlatform> platforms {
        { "Spotify",       -14.0f, true,  -1.0f },
        { "Apple Music",   -16.0f, true,  -1.0f },
        { "YouTube",       -14.0f, false,  0.0f },
        { "Tidal",         -14.0f, false,  0.0f },
        { "Amazon Music",  -14.0f, false,  0.0f },
        { "Deezer",        -15.0f, false,  0.0f },
    };
    return platforms;
}

std::vector<StreamingResult> computeStreamingPreview (float integratedLufs, float truePeakDb)
{
    std::vector<StreamingResult> results;

    if (integratedLufs <= kSilenceDb + 1.0f)
        return results;

    for (const auto& p : getStreamingPlatforms())
    {
        StreamingResult r;
        r.name = p.name;
        r.targetLufs = p.targetLufs;

        float gain = p.targetLufs - integratedLufs;

        if (gain > 0.0f)
            gain = p.turnsUp ? std::max (0.0f, std::min (gain, p.upPeakLimitDb - truePeakDb)) : 0.0f;

        r.gainDb = gain;
        r.playbackLufs = integratedLufs + gain;
        r.playbackTruePeak = truePeakDb + gain;
        r.tooLoud = gain < -0.5f;
        r.tooQuiet = r.playbackLufs < p.targetLufs - 0.5f;
        results.push_back (r);
    }

    return results;
}

} // namespace ma
