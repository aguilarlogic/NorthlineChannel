#pragma once

#include "Presets.h"
#include "dsp/ChannelStrip.h"
#include "smart/SmartStart.h"
#include <map>

namespace nl
{
class NorthlineProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorParameter::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    // Version written by getStateInformation. v1 sessions (original IDs) are migrated on load.
    static constexpr int kStateVersion = 2;

    NorthlineProcessor();
    ~NorthlineProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    // Stereo or mono in and out, plus mono-in/stereo-out (the mono input feeds both channels).
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Northline Channel"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    ChannelStrip& getStrip() noexcept { return strip; }
    juce::RangedAudioParameter* param (int index) const noexcept { return params[(size_t) index]; }

    // Module order (message thread).
    ChannelStrip::Order getOrder() const { return strip.getOrder(); }
    void setOrder (const ChannelStrip::Order& order, bool recordUndo = true);
    void moveModule (int moduleId, int delta);

    // Snapshots used by undo, A/B and presets. They never contain transient parameters
    // (monitor, listen, solo); applySnapshot leaves those untouched.
    juce::ValueTree makeSnapshot() const;
    void applySnapshot (const juce::ValueTree& snapshot);

    // Undo / redo (32 levels, includes module order).
    void pushUndoSnapshot();
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    // A/B compare.
    int getActiveSlot() const;
    void selectSlot (int slot);
    void copyActiveToOther();

    // Presets. Loading a preset (or resetting) also resets the transient parameters.
    void loadFactoryPreset (int index);
    bool saveUserPreset (const juce::File& file, const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    void resetToDefaults();

    //==============================================================================
    // Compare and Delta (roadmap 9.3). Transient: never saved, reset when a state loads.
    //  setCompare: -1 off, 0 the whole strip (loudness-matched through the Level Match trim),
    //              1 + moduleId: that module bypassed, its learned level added back.
    //  setDelta: PRE, GATE and EQ run in the strip; DE-ESS and COMP use their listen modes.
    void setCompare (int target) { strip.setCompare (target); }
    int getCompare() const { return strip.getCompare(); }
    void setDelta (int moduleId, bool on);
    bool getDelta (int moduleId) const;

    //==============================================================================
    // Smart Start (roadmap 9.1). Listen captures the input; the analysis runs in the background and its
    // result is applied here as one undo step, with Spotlight set to the changed parameters.
    bool smartListen (smart::Source source);
    void smartCancel();
    smart::SmartStart& getSmartStart() noexcept { return smartStart; }
    bool applySmartResult (const smart::Result& result);

    //==============================================================================
    // Instance Hub (roadmap 9.2). The id is the registry key, valid while this instance exists.
    int getHubId() const noexcept { return hubId; }
    juce::String getTrackName() const;
    juce::uint32 getTrackColourARGB() const;   // 0 when the host gives no colour
    float getCpuLoadPercent() const noexcept { return cpuPercent.load (std::memory_order_relaxed); }
    void updateTrackProperties (const TrackProperties& properties) override;

    // UI accessors: thread-safe, they return copies.
    juce::String getPresetName() const;
    std::vector<int> getFocus() const;

    // Spotlight (the parameters a preset highlights). Saved with presets and the session.
    void setFocus (std::vector<int> paramIndices);
    bool isInSpotlight (int paramIndex) const;
    void toggleSpotlight (int paramIndex);
    float getUiScale() const;
    void setUiScale (float s);

    // Loudness target shown in the master column (index into loudness::kTargets, custom LUFS).
    // Saved with the session, like the MeterHub integration options.
    std::pair<int, double> getLoudnessTarget() const;
    void setLoudnessTarget (int index, double customLufs);

    // Level Match (D1). Relearn restarts a Learn pass (or the Live tracking).
    void relearnLevelMatch() { strip.relearnLevelMatch(); }
    LevelMatchStatus getLevelMatchStatus() const noexcept { return strip.getLevelMatchStatus(); }

    // Message-thread housekeeping, also driven by the processor's timer (10 Hz): reports a
    // changed latency to the host and mirrors a learned Level Match trim into match_trim.
    // Offline tools without a message loop call it directly.
    void servicePendingWork();
    void updateLatency();

    // State schema migration (v1 -> v2), exposed for tests. The session variant takes the root
    // tree (PARAM children, "AB" snapshots); the snapshot variant takes one property bag
    // (preset files, A/B slots).
    static void migrateSessionV1 (juce::ValueTree& root);
    static void migrateSnapshotV1 (juce::ValueTree& snapshot);

    // Incremented whenever presets, undo or A/B change the whole state (UI refresh hint).
    std::atomic<int> stateGeneration { 0 };

private:
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void applyValues (const std::vector<std::pair<int, float>>& values);
    void resetTransientParams();
    void afterWholeStateLoad();
    void writeParameter (int index, float plain);

    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::RangedAudioParameter*, P::count> params {};
    ChannelStrip strip;
    int preparedBlock = 512;
    std::vector<float> scCopy[2]; // sidechain copy for mono-in layouts, where it shares a buffer channel with the output

    // Non-parameter state. Guarded by stateLock, which the audio thread never takes.
    mutable juce::CriticalSection stateLock;
    std::vector<juce::ValueTree> undoStack;
    int undoPos = -1;
    juce::ValueTree slots[2];
    int activeSlot = 0;
    juce::String presetName { "Init" };
    std::vector<int> focus;
    float uiScale = 1.0f;
    int loudnessTarget = 0;
    double loudnessCustomLufs = -14.0;

    // Smart Start, Instance Hub (message thread state, except cpuPercent and the capture buffer).
    smart::SmartStart smartStart;
    int hubId = 0;
    juce::String trackName;             // guarded by stateLock
    juce::uint32 trackColourARGB = 0;   // guarded by stateLock
    std::atomic<float> cpuPercent { 0.0f }; // smoothed processBlock time, percent of the block duration
    float plainValue (int index) const;
    std::array<float, P::count> currentPlainValues() const;

    std::atomic<int> applyingDepth { 0 }; // > 0 while a snapshot is being applied (no undo entries then)
    uint32_t lastLearnedSerial = 0;       // message thread only

    JUCE_DECLARE_WEAK_REFERENCEABLE (NorthlineProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NorthlineProcessor)
};
} // namespace nl
