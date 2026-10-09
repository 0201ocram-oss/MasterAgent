#include "SnapshotIO.h"

namespace ma
{

namespace
{
    constexpr int kMagic = 0x4e53414d;   // "MASN"
    constexpr int kFormatVersion = 2;

    /**
        Un solo elenco di campi per scrittura e lettura, così l'ordine non può divergere.
        io(...) è chiamata con riferimenti ai campi: Writer li scrive, Reader li riempie.
    */
    template <typename IO, typename Snapshot>
    void visitFields (IO& io, Snapshot& s)
    {
        io (s.valid);
        io (s.sampleRate);
        io (s.numChannels);
        io (s.secondsAnalyzed);
        io (s.liveMode);
        io (s.liveWindowSeconds);

        io (s.integratedLufs);
        io (s.loudnessRange);
        io (s.maxMomentaryLufs);
        io (s.maxShortTermLufs);
        io (s.rmsDb);
        io (s.shortTermHistory);
        io (s.historyStartSongSec);
        io (s.historyAligned);

        io (s.samplePeakDb);
        io (s.truePeakDb);
        io (s.truePeakMaxDb);
        io (s.recentTruePeakDb);
        io (s.overs1dB);
        io (s.overs0dB);
        io (s.clipEvents);
        io (s.clipEventsFullScale);
        io (s.longCeilingClips);
        io (s.ceilingClipDb);

        io (s.crestDb);
        io (s.plr);
        io (s.psr);
        io (s.minPsr);
        io (s.drValue);
        io (s.drValid);
        io (s.bandCrestDb);
        io (s.stHistogram);

        io (s.spectrumLongTermDb);
        io (s.spectrumMidDb);
        io (s.spectrumSideDb);
        io (s.thirdOctaveDb);
        io (s.bandEnergyDb);
        io (s.spectralTiltDbPerOct);
        io (s.spectralCentroidHz);
        io (s.subRumbleDb);
        io (s.ultraHighDb);
        io (s.resonances);
        io (s.keyPitchClass);
        io (s.tuningCents);
        io (s.thirdOctaveLoudestDb);
        io (s.loudestSectionSeconds);
        io (s.bandDynamics);

        io (s.correlation);
        io (s.bandCorrelation);
        io (s.widthPercent);
        io (s.bandWidthPercent);
        io (s.balanceDb);
        io (s.monoLossDb);
        io (s.bandMonoLossDb);
        io (s.lowEndWidthPercent);
        io (s.lowEndCorrelation);

        io (s.dcOffsetDb);
        io (s.noiseFloorDb);
        io (s.hasDigitalSilence);
        io (s.channelSilent);
    }

    struct Writer
    {
        juce::OutputStream& out;

        void operator() (bool v)         { out.writeBool (v); }
        void operator() (int v)          { out.writeInt (v); }
        void operator() (float v)        { out.writeFloat (v); }
        void operator() (double v)       { out.writeDouble (v); }

        void operator() (const Resonance& r)
        {
            out.writeFloat (r.frequency); out.writeFloat (r.excessDb); out.writeBool (r.musical);
            out.writeInt (r.midiNote); out.writeFloat (r.cents);
        }

        void operator() (const BandDynamics& d)
        {
            out.writeFloat (d.burstDb); out.writeFloat (d.worstStartSec); out.writeFloat (d.worstEndSec); out.writeFloat (d.worstExcessDb);
        }

        template <typename T, size_t N>
        void operator() (const std::array<T, N>& a)
        {
            out.writeInt ((int) N);
            for (const auto& v : a)
                (*this) (v);
        }

        template <typename T>
        void operator() (const std::vector<T>& v)
        {
            out.writeInt ((int) v.size());
            for (const auto& x : v)
                (*this) (x);
        }
    };

    struct Reader
    {
        juce::InputStream& in;
        bool ok = true;

        bool more() { ok = ok && ! in.isExhausted(); return ok; }

        void operator() (bool& v)        { if (more()) v = in.readBool(); }
        void operator() (int& v)         { if (more()) v = in.readInt(); }
        void operator() (float& v)       { if (more()) v = in.readFloat(); }
        void operator() (double& v)      { if (more()) v = in.readDouble(); }

        void operator() (Resonance& r)
        {
            (*this) (r.frequency); (*this) (r.excessDb); (*this) (r.musical); (*this) (r.midiNote); (*this) (r.cents);
        }

        void operator() (BandDynamics& d)
        {
            (*this) (d.burstDb); (*this) (d.worstStartSec); (*this) (d.worstEndSec); (*this) (d.worstExcessDb);
        }

        template <typename T, size_t N>
        void operator() (std::array<T, N>& a)
        {
            int n = 0;
            (*this) (n);
            if (n != (int) N)   // dimensioni cambiate: dati non compatibili
            {
                ok = false;
                return;
            }
            for (auto& v : a)
                (*this) (v);
        }

        template <typename T>
        void operator() (std::vector<T>& v)
        {
            int n = 0;
            (*this) (n);
            if (n < 0 || n > 1000000)
            {
                ok = false;
                return;
            }
            v.assign ((size_t) n, T {});
            for (auto& x : v)
                (*this) (x);
        }
    };
}

juce::MemoryBlock saveSnapshot (const AnalysisSnapshot& s)
{
    juce::MemoryBlock block;
    {
        juce::MemoryOutputStream raw (block, false);
        raw.writeInt (kMagic);
        raw.writeInt (kFormatVersion);

        juce::GZIPCompressorOutputStream zip (raw, 9);
        Writer writer { zip };
        visitFields (writer, s);
        zip.flush();
    }
    return block;
}

bool loadSnapshot (const void* data, size_t size, AnalysisSnapshot& out)
{
    juce::MemoryInputStream raw (data, size, false);
    if (size < 8 || raw.readInt() != kMagic || raw.readInt() != kFormatVersion)
        return false;

    juce::GZIPDecompressorInputStream zip (&raw, false);
    AnalysisSnapshot s;
    Reader reader { zip };
    visitFields (reader, s);
    if (! reader.ok)
        return false;

    out = std::move (s);
    return true;
}

} // namespace ma
