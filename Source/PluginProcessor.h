#pragma once

#include "Presets.h"
#include "dsp/ChannelStrip.h"
#include <map>

namespace nl
{
// "Comp Threshold -18 -> -22 dB" in the typeset form (U+2212 minus, U+2192 arrow); the unit is
// written once when both values share it. Choice and bool parameters read "HP On: Off -> On".
juce::String undoLabelForParam (int paramIndex, float oldPlain, float newPlain);

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
    void setOrder (const ChannelStrip::Order& order, bool recordUndo = true, const juce::String& undoLabel = {});
    void moveModule (int moduleId, int delta);

    // Snapshots used by undo, A/B and presets. They never contain transient parameters
    // (monitor, listen, solo); applySnapshot leaves those untouched.
    juce::ValueTree makeSnapshot() const;
    void applySnapshot (const juce::ValueTree& snapshot);

    // Undo / redo (32 levels, includes module order). Every entry has a label such as
    // "Comp Threshold -18 -> -22 dB" (see undoLabelForParam), taken from the gesture, preset,
    // A/B, reorder or reset that made it.
    void pushUndoSnapshot (const juce::String& label = {});
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    // The timeline runs from the oldest state to the newest. getUndoHistory() ends at the current
    // state (its last label is the change that produced it); getRedoHistory() lists the states after
    // it, next first. undoTo() takes an index into the two lists joined (undo part first).
    std::vector<juce::String> getUndoHistory() const;
    std::vector<juce::String> getRedoHistory() const;
    void undoTo (int timelineIndex);

    // Module presets: one module's parameters, never transient ones. Module ids are ModuleId.
    // Module presets travel as XML: files (*.nlmodule) and the clipboard.
    static int moduleOfParam (int paramIndex); // ModuleId, or -1 for master parameters
    juce::String getModuleXml (int moduleId, const juce::String& name = {}) const;
    bool saveModulePreset (const juce::File& file, int moduleId, const juce::String& name);
    bool loadModulePreset (const juce::File& file);  // the module comes from the file
    bool pasteModuleXml (const juce::String& xml);   // false when the text is not a module preset

    // A/B compare.
    int getActiveSlot() const;
    void selectSlot (int slot);
    void copyActiveToOther();

    // Presets. Loading a preset (or resetting) also resets the transient parameters.
    void loadFactoryPreset (int index);
    bool saveUserPreset (const juce::File& file, const juce::String& name, const PresetMeta& meta = {});
    bool loadUserPreset (const juce::File& file);
    void resetToDefaults();

    // UI accessors: thread-safe, they return copies.
    juce::String getPresetName() const;
    std::vector<int> getFocus() const;

    // Spotlight (the parameters a preset highlights). Saved with presets and the session.
    void setFocus (std::vector<int> paramIndices);
    bool isInSpotlight (int paramIndex) const;
    void toggleSpotlight (int paramIndex);
    float getUiScale() const;
    void setUiScale (float s);

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
    void applyParamsAsOneStep (const std::vector<std::pair<int, float>>& values, const juce::String& label);
    void noteGesture (int juceParamIndex, bool starting);
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
    std::vector<juce::String> undoLabels; // parallel to undoStack
    juce::String pendingUndoLabel;        // label for the async push of a finished gesture
    int gestureParam = -1;                // parameter of the gesture in progress (message thread)
    float gestureStartPlain = 0.0f;
    int undoPos = -1;
    juce::ValueTree slots[2];
    int activeSlot = 0;
    juce::String presetName { "Init" };
    std::vector<int> focus;
    float uiScale = 1.0f;

    std::atomic<int> applyingDepth { 0 }; // > 0 while a snapshot is being applied (no undo entries then)
    uint32_t lastLearnedSerial = 0;       // message thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NorthlineProcessor)
};
} // namespace nl
