#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Analysis/SnapshotIO.h"
#include "Common/Text.h"

namespace
{
    // il wrapper standalone imposta il tipo prima di creare il processor
    bool isStandaloneWrapper()
    {
        return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
    }
}

juce::AudioProcessor::BusesProperties MasterAgentProcessor::makeBuses (bool standaloneApp)
{
    // standalone: solo uscita (il lettore), così l'app non apre l'ingresso della scheda audio
    if (standaloneApp)
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

MasterAgentProcessor::MasterAgentProcessor()
    : AudioProcessor (makeBuses (isStandaloneWrapper())),
      standalone (isStandaloneWrapper())
{
}

MasterAgentProcessor::~MasterAgentProcessor()
{
    liveAnalysis.release();
}

bool MasterAgentProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (standalone)
        return layouts.getMainInputChannelSet().isDisabled();
    return layouts.getMainInputChannelSet() == out;
}

void MasterAgentProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    if (standalone)
    {
        player.prepare (sampleRate, samplesPerBlock);
        reference.setHostSampleRate (sampleRate);
        return;
    }

    liveAnalysis.prepare (sampleRate, getTotalNumInputChannels());
    liveAnalysis.setLiveMode (liveMode.load(), liveWindowSeconds.load());
    reference.setHostSampleRate (sampleRate);
    wasPlaying = false;
}

void MasterAgentProcessor::releaseResources()
{
    liveAnalysis.release();
    player.release();
}

void MasterAgentProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // standalone: l'uscita è il lettore, oppure il reference durante l'ascolto A/B
    if (standalone)
    {
        player.render (buffer);
        if (listenReference.load() && buffer.getNumChannels() > 0)
            reference.renderPlayback (buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr,
                                      buffer.getNumSamples(), referenceGain.load());
        return;
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels = std::min (2, getTotalNumInputChannels());
    if (numSamples == 0 || numChannels == 0)
        return;

    // stato del transport
    bool hasTransport = false, isPlaying = false, hasTime = false;
    double timeSeconds = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            hasTransport = true;
            isPlaying = pos->getIsPlaying();
            if (const auto t = pos->getTimeInSeconds())
            {
                timeSeconds = *t;
                hasTime = true;
            }
        }
    }

    // reset automatico quando la riproduzione riparte dall'inizio del brano
    if (hasTransport && isPlaying && ! wasPlaying && autoResetOnPlay.load() && timeSeconds < 0.5)
        liveAnalysis.requestReset();
    wasPlaying = isPlaying;

    // la posizione del transport serve a dire in che punto del brano si trova un problema
    const double songTime = (isPlaying && hasTime) ? timeSeconds : std::numeric_limits<double>::quiet_NaN();

    if (! hasTransport || isPlaying || ! analyseOnlyWhilePlaying.load())
        liveAnalysis.push (buffer.getReadPointer (0), numChannels > 1 ? buffer.getReadPointer (1) : nullptr, numSamples,
                           isNonRealtime(),   // export offline: attende l'analisi invece di perdere audio
                           songTime);

    // ascolto A/B del reference: sostituisce l'uscita (l'analisi resta sul master).
    // Se il reference non è disponibile il master passa inalterato.
    if (listenReference.load())
        reference.renderPlayback (buffer.getWritePointer (0), numChannels > 1 ? buffer.getWritePointer (1) : nullptr,
                                  numSamples, referenceGain.load());

    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

void MasterAgentProcessor::setLiveMode (bool shouldBeLive, double windowSeconds)
{
    liveMode.store (shouldBeLive);
    liveWindowSeconds.store (windowSeconds);
    liveAnalysis.setLiveMode (shouldBeLive, windowSeconds);
}

void MasterAgentProcessor::loadReference (const juce::File& file)
{
    reference.load (file, currentSampleRate);
}

juce::String MasterAgentProcessor::getSelectedTargetId() const
{
    const juce::ScopedLock lock (settingsLock);
    return selectedTargetId;
}

void MasterAgentProcessor::setSelectedTargetId (const juce::String& id)
{
    const juce::ScopedLock lock (settingsLock);
    selectedTargetId = id;
}

juce::StringArray MasterAgentProcessor::getIgnoredFindings() const
{
    const juce::ScopedLock lock (settingsLock);
    return ignoredFindings;
}

void MasterAgentProcessor::setFindingIgnored (const juce::String& findingId, bool ignored)
{
    const juce::ScopedLock lock (settingsLock);
    if (ignored)
        ignoredFindings.addIfNotAlreadyThere (findingId);
    else
        ignoredFindings.removeString (findingId);
}

void MasterAgentProcessor::clearIgnoredFindings()
{
    const juce::ScopedLock lock (settingsLock);
    ignoredFindings.clear();
}

//==============================================================================
void MasterAgentProcessor::analyseMasterFile (const juce::File& file)
{
    {
        const juce::ScopedLock lock (settingsLock);
        masterFile = file;
        masterFileState = MasterFileState::analysing;
        masterFileError.clear();
        masterFileTimeline.reset();
    }
    masterFileJob.start ({ file }, "Analisi"_t, true);
    if (standalone)
        player.load (file);
}

void MasterAgentProcessor::clearMasterFile()
{
    masterFileJob.cancel();
    const juce::ScopedLock lock (settingsLock);
    masterFile = juce::File();
    masterFileState = MasterFileState::none;
    masterFileError.clear();
    masterFileSnapshot = {};
    masterFileTimeline.reset();
    player.unload();
}

void MasterAgentProcessor::updateMasterFile()
{
    if (masterFileJob.getState() != ma::BatchAnalysisJob::State::done)
        return;

    auto results = masterFileJob.takeResults();
    const juce::ScopedLock lock (settingsLock);
    if (masterFileState != MasterFileState::analysing)
        return;

    if (! results.snapshots.empty())
    {
        masterFileSnapshot = std::move (results.snapshots.front());
        masterFileTimeline = results.timelines.empty() ? nullptr : results.timelines.front();
        masterFileState = MasterFileState::ready;
    }
    else
    {
        masterFileState = MasterFileState::failed;
        masterFileError = results.skipped.isEmpty() ? juce::String() : results.skipped[0];
    }
}

MasterAgentProcessor::MasterFileState MasterAgentProcessor::getMasterFileState() const
{
    const juce::ScopedLock lock (settingsLock);
    return masterFileState;
}

juce::File MasterAgentProcessor::getMasterFile() const
{
    const juce::ScopedLock lock (settingsLock);
    return masterFile;
}

juce::String MasterAgentProcessor::getMasterFileError() const
{
    const juce::ScopedLock lock (settingsLock);
    return masterFileError;
}

bool MasterAgentProcessor::getMasterFileSnapshot (ma::AnalysisSnapshot& dest) const
{
    const juce::ScopedLock lock (settingsLock);
    if (masterFileState != MasterFileState::ready)
        return false;
    dest = masterFileSnapshot;
    return true;
}

std::shared_ptr<const ma::TrackTimeline> MasterAgentProcessor::getMasterFileTimeline() const
{
    const juce::ScopedLock lock (settingsLock);
    return masterFileState == MasterFileState::ready ? masterFileTimeline : nullptr;
}

//==============================================================================
int MasterAgentProcessor::addVersion (const ma::AnalysisSnapshot& snapshot, const juce::String& source)
{
    const juce::ScopedLock lock (settingsLock);

    // numero progressivo: il più alto usato finora + 1
    int next = 1;
    for (const auto& v : versions)
        if (v.name.startsWithChar ('v'))
            next = std::max (next, v.name.substring (1).getIntValue() + 1);

    SavedVersion v;
    v.time = juce::Time::getCurrentTime();
    v.name = "v" + juce::String (next) + "  " + v.time.formatted ("%H:%M") + (source != "live" ? "  " + source : juce::String());
    v.source = source;
    v.snapshot = snapshot;
    versions.push_back (std::move (v));

    if ((int) versions.size() > maxVersions)
    {
        versions.erase (versions.begin());
        compareVersion = std::max (-1, compareVersion - 1);
    }

    ++versionsRevision;
    return (int) versions.size() - 1;
}

void MasterAgentProcessor::renameVersion (int index, const juce::String& name)
{
    const juce::ScopedLock lock (settingsLock);
    if (juce::isPositiveAndBelow (index, (int) versions.size()) && name.trim().isNotEmpty())
    {
        versions[(size_t) index].name = name.trim();
        ++versionsRevision;
    }
}

void MasterAgentProcessor::removeVersion (int index)
{
    const juce::ScopedLock lock (settingsLock);
    if (! juce::isPositiveAndBelow (index, (int) versions.size()))
        return;

    versions.erase (versions.begin() + index);
    if (compareVersion == index)
        compareVersion = -1;
    else if (compareVersion > index)
        --compareVersion;
    ++versionsRevision;
}

void MasterAgentProcessor::clearVersions()
{
    const juce::ScopedLock lock (settingsLock);
    versions.clear();
    compareVersion = -1;
    ++versionsRevision;
}

int MasterAgentProcessor::getNumVersions() const
{
    const juce::ScopedLock lock (settingsLock);
    return (int) versions.size();
}

juce::StringArray MasterAgentProcessor::getVersionNames() const
{
    const juce::ScopedLock lock (settingsLock);
    juce::StringArray names;
    for (const auto& v : versions)
        names.add (v.name);
    return names;
}

bool MasterAgentProcessor::getVersion (int index, SavedVersion& dest) const
{
    const juce::ScopedLock lock (settingsLock);
    if (! juce::isPositiveAndBelow (index, (int) versions.size()))
        return false;
    dest = versions[(size_t) index];
    return true;
}

int MasterAgentProcessor::getCompareVersion() const
{
    const juce::ScopedLock lock (settingsLock);
    return compareVersion;
}

void MasterAgentProcessor::setCompareVersion (int index)
{
    const juce::ScopedLock lock (settingsLock);
    compareVersion = juce::isPositiveAndBelow (index, (int) versions.size()) ? index : -1;
    ++versionsRevision;
}

//==============================================================================
void MasterAgentProcessor::startAlbumCheck (const juce::Array<juce::File>& files)
{
    auto list = files;
    list.removeRange (ma::album::maxTracks, list.size());
    {
        const juce::ScopedLock lock (settingsLock);
        album = {};
        albumState = AlbumState::analysing;
    }
    albumJob.start (list, "Album"_t, standalone);
    ++albumRevision;
}

void MasterAgentProcessor::clearAlbum()
{
    albumJob.cancel();
    {
        const juce::ScopedLock lock (settingsLock);
        album = {};
        albumState = AlbumState::none;
    }
    ++albumRevision;
}

void MasterAgentProcessor::updateAlbum()
{
    if (albumJob.getState() != ma::BatchAnalysisJob::State::done)
        return;

    auto results = albumJob.takeResults();
    {
        const juce::ScopedLock lock (settingsLock);
        if (albumState != AlbumState::analysing)
            return;
        album.snapshots = std::move (results.snapshots);
        album.files = results.files;
        album.names = results.names;
        album.skipped = results.skipped;
        album.timelines = std::move (results.timelines);
        albumState = AlbumState::ready;
    }
    ++albumRevision;
}

MasterAgentProcessor::AlbumState MasterAgentProcessor::getAlbumState() const
{
    const juce::ScopedLock lock (settingsLock);
    return albumState;
}

MasterAgentProcessor::AlbumData MasterAgentProcessor::getAlbum() const
{
    const juce::ScopedLock lock (settingsLock);
    return album;
}

void MasterAgentProcessor::useAlbumTrackAsMaster (int index)
{
    masterFileJob.cancel();
    juce::File file;
    {
        const juce::ScopedLock lock (settingsLock);
        if (! juce::isPositiveAndBelow (index, (int) album.snapshots.size()))
            return;

        file = album.files[index];
        masterFile = file;
        masterFileSnapshot = album.snapshots[(size_t) index];
        masterFileTimeline = (size_t) index < album.timelines.size() ? album.timelines[(size_t) index] : nullptr;
        masterFileState = MasterFileState::ready;
        masterFileError.clear();
    }
    if (standalone)
        player.load (file);
}

juce::AudioProcessorEditor* MasterAgentProcessor::createEditor()
{
    return new MasterAgentEditor (*this);
}

void MasterAgentProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MasterAgentState");
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("target", getSelectedTargetId(), nullptr);
    state.setProperty ("ignoredFindings", getIgnoredFindings().joinIntoString ("\n"), nullptr);
    {
        const juce::ScopedLock lock (settingsLock);
        juce::ValueTree list ("Versions");
        list.setProperty ("compare", compareVersion, nullptr);
        for (const auto& v : versions)
        {
            juce::ValueTree node ("Version");
            node.setProperty ("name", v.name, nullptr);
            node.setProperty ("time", v.time.toMilliseconds(), nullptr);
            node.setProperty ("source", v.source, nullptr);
            node.setProperty ("data", ma::saveSnapshot (v.snapshot), nullptr);
            list.appendChild (node, nullptr);
        }
        state.appendChild (list, nullptr);
    }
    state.setProperty ("referencePath", reference.getFile().getFullPathName(), nullptr);
    state.setProperty ("autoReset", autoResetOnPlay.load(), nullptr);
    state.setProperty ("onlyWhilePlaying", analyseOnlyWhilePlaying.load(), nullptr);
    state.setProperty ("liveMode", liveMode.load(), nullptr);
    state.setProperty ("abMatchLoudest", abMatchLoudestSection.load(), nullptr);
    state.setProperty ("liveWindow", liveWindowSeconds.load(), nullptr);
    state.setProperty ("mixPhase", mixPhase.load(), nullptr);
    state.setProperty ("tonalLoudest", tonalLoudestSection.load(), nullptr);
    state.setProperty ("editorWidth", editorWidth, nullptr);
    state.setProperty ("editorHeight", editorHeight, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void MasterAgentProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.hasType ("MasterAgentState"))
        return;

    setSelectedTargetId (state.getProperty ("target", "streaming_generic").toString());
    {
        auto ignored = juce::StringArray::fromLines (state.getProperty ("ignoredFindings", {}).toString());
        ignored.removeEmptyStrings();
        const juce::ScopedLock lock (settingsLock);
        ignoredFindings = ignored;
    }
    autoResetOnPlay.store ((bool) state.getProperty ("autoReset", true));
    analyseOnlyWhilePlaying.store ((bool) state.getProperty ("onlyWhilePlaying", true));
    setLiveMode ((bool) state.getProperty ("liveMode", false), (double) state.getProperty ("liveWindow", 20.0));
    abMatchLoudestSection.store ((bool) state.getProperty ("abMatchLoudest", false));
    mixPhase.store ((bool) state.getProperty ("mixPhase", false));
    tonalLoudestSection.store ((bool) state.getProperty ("tonalLoudest", false));
    editorWidth = (int) state.getProperty ("editorWidth", 1440);
    editorHeight = (int) state.getProperty ("editorHeight", 900);

    {
        std::vector<SavedVersion> loaded;
        const auto list = state.getChildWithName ("Versions");
        for (const auto& node : list)
        {
            SavedVersion v;
            const auto* blob = node.getProperty ("data").getBinaryData();
            if (blob == nullptr || ! ma::loadSnapshot (blob->getData(), blob->getSize(), v.snapshot))
                continue;
            v.name = node.getProperty ("name").toString();
            v.time = juce::Time ((juce::int64) node.getProperty ("time", 0));
            v.source = node.getProperty ("source").toString();
            loaded.push_back (std::move (v));
        }

        const juce::ScopedLock lock (settingsLock);
        versions = std::move (loaded);
        const int wanted = (int) list.getProperty ("compare", -1);
        compareVersion = juce::isPositiveAndBelow (wanted, (int) versions.size()) ? wanted : -1;
        ++versionsRevision;
    }

    const juce::File refFile (state.getProperty ("referencePath", {}).toString());
    if (refFile.existsAsFile() && refFile != reference.getFile())
        loadReference (refFile);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterAgentProcessor();
}
