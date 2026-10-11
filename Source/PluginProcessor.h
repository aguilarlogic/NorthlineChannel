#pragma once

#include "Presets.h"
#include "dsp/ChannelStrip.h"

namespace nl
{
class NorthlineProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorParameter::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    NorthlineProcessor();
    ~NorthlineProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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

    // Snapshots used by undo, A/B and presets.
    juce::ValueTree makeSnapshot() const;
    void applySnapshot (const juce::ValueTree& snapshot);

    // Undo / redo (32 levels, includes module order).
    void pushUndoSnapshot();
    bool canUndo() const { return undoPos > 0; }
    bool canRedo() const { return undoPos + 1 < (int) undoStack.size(); }
    void undo();
    void redo();

    // A/B compare.
    int getActiveSlot() const { return activeSlot; }
    void selectSlot (int slot);
    void copyActiveToOther();

    // Presets.
    void loadFactoryPreset (int index);
    bool saveUserPreset (const juce::File& file, const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    void resetToDefaults();
    juce::String getPresetName() const { return presetName; }
    const std::vector<int>& getFocus() const { return focus; }

    float getUiScale() const { return uiScale; }
    void setUiScale (float s) { uiScale = s; }

    // Incremented whenever presets, undo or A/B change the whole state (UI refresh hint).
    std::atomic<int> stateGeneration { 0 };

private:
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void applyValues (const std::vector<std::pair<int, float>>& values);

    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::RangedAudioParameter*, P::count> params {};
    ChannelStrip strip;
    int preparedBlock = 512;

    std::vector<juce::ValueTree> undoStack;
    int undoPos = -1;
    bool applyingSnapshot = false;

    juce::ValueTree slots[2];
    int activeSlot = 0;

    juce::String presetName { "Init" };
    std::vector<int> focus;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NorthlineProcessor)
};
} // namespace nl
