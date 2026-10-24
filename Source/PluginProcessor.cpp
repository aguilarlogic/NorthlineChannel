#include "PluginProcessor.h"
#include "gui/PluginEditor.h"
#include "hub/InstanceRegistry.h"

namespace nl
{
namespace
{
juce::String orderToString (const ChannelStrip::Order& o)
{
    juce::StringArray s;
    for (int id : o) s.add (juce::String (id));
    return s.joinIntoString (",");
}

bool orderFromString (const juce::String& text, ChannelStrip::Order& out)
{
    auto tokens = juce::StringArray::fromTokens (text, ",", "");
    if (tokens.size() != numModules) return false;
    ChannelStrip::Order o {};
    for (int i = 0; i < numModules; ++i) o[(size_t) i] = tokens[i].getIntValue();
    if (! ChannelStrip::isValidOrder (o)) return false;
    out = o;
    return true;
}

juce::String focusToString (const std::vector<int>& f)
{
    juce::StringArray s;
    for (int i : f) s.add (paramID (i));
    return s.joinIntoString (",");
}

std::vector<int> focusFromString (const juce::String& text)
{
    std::vector<int> out;
    for (auto& t : juce::StringArray::fromTokens (text, ",", ""))
        for (int i = 0; i < P::count; ++i)
            if (paramID (i) == t) out.push_back (i);
    return out;
}

// Index of a parameter ID, or -1.
int indexForId (const juce::String& id)
{
    static const std::map<juce::String, int> table = [] { std::map<juce::String, int> m; for (int i = 0; i < P::count; ++i) m[paramID (i)] = i; return m; }();
    const auto it = table.find (id);
    return it == table.end() ? -1 : it->second;
}

bool isTransientId (const juce::String& id)
{
    const int i = indexForId (id);
    return i >= 0 && isTransient (i);
}

bool isMetaProperty (const juce::String& name)
{
    return name == "order" || name == "focus" || name == "presetName" || name == "slot" || name == "uiScale"
           || name == "activeSlot" || name == "stateVersion" || name == "version" || name == "name"
           || name == "loudTarget" || name == "loudCustom" || name == "loudPause" || name == "loudResetOnPlay";
}

// v1 -> v2: renamed or re-meant parameters, by their v1 IDs.
void migrateValuesV1 (std::map<juce::String, float>& v)
{
    const auto take = [&v] (const char* id, float& out)
    {
        const auto it = v.find (id);
        if (it == v.end()) return false;
        out = it->second;
        v.erase (it);
        return true;
    };
    float x = 0.0f;
    if (take ("in_phase", x))      v["in_polarity"] = x >= 0.5f ? 3.0f : 0.0f;        // Invert L+R
    if (take ("level_match", x))   v["match_mode"] = x >= 0.5f ? 2.0f : 0.0f;         // Live
    if (take ("lim_lookahead", x)) v["lim_mode"] = x >= 0.5f ? 1.0f : 0.0f;           // Brickwall / Safety Clip
    if (take ("vu_ref", x))        v["op_level"] = std::clamp (x, -24.0f, -10.0f);
    if (take ("sat_hq", x))        v["quality"] = x >= 0.5f ? 1.0f : 0.0f;
    if (take ("thump_on", x))      v["weight_on"] = x;
    if (take ("thump_amt", x))     v["weight_amt"] = x;
    // v1 monitor: Stereo, Mono, Left, Right, Mid, Side. v2: ..., Side, Swap (and it is transient).
    if (const auto it = v.find ("monitor"); it != v.end())
    {
        const int m = (int) std::lround (it->second);
        it->second = m == 4 ? 1.0f : (m == 5 ? 4.0f : (float) m);
    }
    // ds_listen / comp_listen / gate_listen keep their IDs and index meanings (they are transient in v2).
}
} // namespace

//==============================================================================
namespace
{
juce::String typesetNumber (const juce::String& text)
{
    // U+2212 minus before digits, so labels read as typeset ("-18" becomes a true minus).
    juce::String out;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (c == '-' && i + 1 < text.length() && juce::CharacterFunctions::isDigit (text[i + 1]))
            out += juce::String::fromUTF8 ("\xe2\x88\x92");
        else
            out += juce::String::charToString (c);
    }
    return out;
}
} // namespace

juce::String undoLabelForParam (int index, float oldPlain, float newPlain)
{
    const auto& d = paramDef (index);
    juce::String name = d.name;
    if (const int mod = NorthlineProcessor::moduleOfParam (index); mod >= 0)
    {
        const juce::String prefix = moduleName (mod);
        if (! name.startsWithIgnoreCase (prefix)) name = prefix + " " + name;
    }

    const juce::String arrow = juce::String::fromUTF8 ("\xe2\x86\x92");
    const auto a = formatValue (index, oldPlain), b = formatValue (index, newPlain);
    if (d.kind != ParamDef::Float)
        return name + ": " + typesetNumber (a) + " " + arrow + " " + typesetNumber (b);

    // Write the unit once when both values share it: "Comp Threshold -18 -> -22 dB".
    juce::String oldText = a;
    for (auto unit : { " dB", " kHz", " Hz", " ms", " s", " %", " : 1" })
        if (a.endsWith (unit) && b.endsWith (unit))
        {
            oldText = a.dropLastCharacters ((int) std::strlen (unit));
            break;
        }
    return name + " " + typesetNumber (oldText) + " " + arrow + " " + typesetNumber (b);
}

//==============================================================================
void NorthlineProcessor::migrateSnapshotV1 (juce::ValueTree& snapshot)
{
    std::map<juce::String, float> before;
    for (int i = 0; i < snapshot.getNumProperties(); ++i)
    {
        const auto name = snapshot.getPropertyName (i).toString();
        if (! isMetaProperty (name)) before[name] = (float) (double) snapshot.getProperty (snapshot.getPropertyName (i));
    }
    auto after = before;
    migrateValuesV1 (after);
    for (auto& [name, value] : before)
        if (after.find (name) == after.end()) snapshot.removeProperty (name, nullptr);
    for (auto& [name, value] : after) snapshot.setProperty (name, (double) value, nullptr);
}

void NorthlineProcessor::migrateSessionV1 (juce::ValueTree& root)
{
    std::map<juce::String, float> values;
    juce::Identifier paramType ("PARAM");
    for (int i = root.getNumChildren(); --i >= 0;)
    {
        auto child = root.getChild (i);
        if (! (child.hasProperty ("id") && child.hasProperty ("value"))) continue;
        paramType = child.getType();
        values[child.getProperty ("id").toString()] = (float) (double) child.getProperty ("value");
        root.removeChild (i, nullptr);
    }
    migrateValuesV1 (values);
    for (auto& [id, value] : values)
    {
        juce::ValueTree child (paramType);
        child.setProperty ("id", id, nullptr);
        child.setProperty ("value", (double) value, nullptr);
        root.appendChild (child, nullptr);
    }
    if (auto ab = root.getChildWithName ("AB"); ab.isValid())
        for (int i = 0; i < ab.getNumChildren(); ++i)
        {
            auto s = ab.getChild (i);
            migrateSnapshotV1 (s);
        }
    root.setProperty ("stateVersion", 2, nullptr);
}

//==============================================================================
NorthlineProcessor::NorthlineProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "NorthlineState", createParameterLayout()),
      strip (apvts)
{
    for (int i = 0; i < P::count; ++i)
    {
        params[(size_t) i] = apvts.getParameter (paramID (i));
        jassert (params[(size_t) i] != nullptr);
        params[(size_t) i]->addListener (this);
    }
    slots[0] = makeSnapshot();
    slots[1] = slots[0].createCopy();
    pushUndoSnapshot ("Start");
    startTimerHz (10);

    // Instance Hub (9.2): the registry holds a weak reference; the destructor removes the entry.
    hubId = hub::InstanceRegistry::instance().add (*this);
}

NorthlineProcessor::~NorthlineProcessor()
{
    hub::InstanceRegistry::instance().remove (hubId);
    stopTimer();
    cancelPendingUpdate();
    for (auto* p : params)
        if (p != nullptr) p->removeListener (this);
}

//==============================================================================
bool NorthlineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    if (mainOut != mono && mainOut != stereo) return false;
    if (mainIn != mainOut && ! (mainIn == mono && mainOut == stereo)) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != mono && sc != stereo) return false;
    }
    return true;
}

void NorthlineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = std::max (1, getMainBusNumOutputChannels());
    preparedBlock = std::max (16, samplesPerBlock);
    for (auto& v : scCopy) v.assign ((size_t) preparedBlock, 0.0f);
    strip.prepare (sampleRate, preparedBlock, numCh);
    smartStart.prepare (sampleRate); // Smart Start capture buffer (allocated here, never on the audio thread)
    bypassLine.prepare (2, ChannelStrip::maxModuleLatency (sampleRate) * (numModules + 1));
    setLatencySamples (strip.getLatencySamples());
}

// Host bypass (a host without a bypass parameter calls this): the input delayed by the reported
// latency, so the bypassed track stays aligned with the others.
void NorthlineProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto mainOut = getBusBuffer (buffer, false, 0);
    const int numCh = juce::jmin (2, mainOut.getNumChannels());
    const int n = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;

    if (numCh == 2 && getMainBusNumInputChannels() == 1) mainOut.copyFrom (1, 0, mainOut, 0, 0, n);
    for (int c = numCh; c < mainOut.getNumChannels(); ++c) mainOut.clear (c, 0, n);
    bypassLine.setDelay (getLatencySamples());
    bypassLine.process (mainOut.getArrayOfWritePointers(), mainOut.getArrayOfWritePointers(), numCh, n);
}

void NorthlineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainOut = getBusBuffer (buffer, false, 0);
    const int numCh = juce::jmin (2, mainOut.getNumChannels());
    const int n = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;

    // Mono input, stereo output: the input is on channel 0 only and feeds both channels. In that
    // layout the sidechain starts at channel 1, the same buffer channel as the right output.
    const bool monoToStereo = numCh == 2 && getMainBusNumInputChannels() == 1;

    // Keep the host-bypass line current, so a switch to bypass is aligned from its first block.
    {
        const float* in[2] { mainOut.getReadPointer (0), mainOut.getReadPointer (monoToStereo ? 0 : numCh - 1) };
        bypassLine.write (in, numCh, n);
    }

    int scCh = 0;
    const float* sc[2] { nullptr, nullptr };
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto scBuf = getBusBuffer (buffer, true, 1);
            scCh = juce::jmin (2, scBuf.getNumChannels());
            for (int c = 0; c < scCh; ++c) sc[c] = scBuf.getReadPointer (c);
        }

    for (int c = numCh; c < mainOut.getNumChannels(); ++c) mainOut.clear (c, 0, n);

    // Loudness integration can pause while the host transport is stopped (MeterHub option). The host
    // position is read only while one of the two options that use it is on.
    if (strip.meterHub().pauseIntegrationWhenStopped() || strip.meterHub().resetIntegrationOnPlay())
        if (auto* ph = getPlayHead())
            if (auto position = ph->getPosition())
                strip.meterHub().setTransportPlaying (position->getIsPlaying());

    // Process in chunks no larger than the prepared block size.
    const auto cpuStart = juce::Time::getHighResolutionTicks();
    int pos = 0;
    while (pos < n)
    {
        const int len = juce::jmin (n - pos, preparedBlock);
        float* io[2] { mainOut.getWritePointer (0) + pos, numCh > 1 ? mainOut.getWritePointer (1) + pos : nullptr };
        const float* key[2] { nullptr, nullptr };
        for (int c = 0; c < scCh; ++c)
        {
            if (monoToStereo)
            {
                std::copy (sc[c] + pos, sc[c] + pos + len, scCopy[c].data());
                key[c] = scCopy[c].data();
            }
            else
            {
                key[c] = sc[c] + pos;
            }
        }
        if (monoToStereo) std::copy (io[0], io[0] + len, io[1]);
        // Smart Start capture: the raw input, before the strip touches it (writes samples and atomics only).
        smartStart.getCapture().write (io, numCh, len);
        strip.process (io, numCh, len, scCh > 0 ? key : nullptr, scCh);
        pos += len;
    }

    // Instance Hub: smoothed share of the block duration spent in the plug-in.
    const double busyUs = (double) (juce::Time::getHighResolutionTicks() - cpuStart) * 1.0e6 / juce::Time::getHighResolutionTicksPerSecond();
    const double blockUs = n * 1.0e6 / juce::jmax (1.0, getSampleRate());
    cpuPercent.store (0.9f * cpuPercent.load (std::memory_order_relaxed) + 0.1f * (float) (100.0 * busyUs / blockUs), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* NorthlineProcessor::createEditor() { return new NorthlineEditor (*this); }

void NorthlineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state;
    {
        const juce::ScopedLock sl (stateLock);
        state = apvts.copyState();
        for (int i = state.getNumChildren(); --i >= 0;)
            if (isTransientId (state.getChild (i).getProperty ("id").toString())) state.removeChild (i, nullptr);

        state.setProperty ("order", orderToString (getOrder()), nullptr);
        state.setProperty ("focus", focusToString (focus), nullptr);
        state.setProperty ("presetName", presetName, nullptr);
        state.setProperty ("uiScale", uiScale, nullptr);
        state.setProperty ("loudTarget", loudnessTarget, nullptr);
        state.setProperty ("loudCustom", loudnessCustomLufs, nullptr);
        state.setProperty ("loudPause", strip.meterHub().pauseIntegrationWhenStopped(), nullptr);
        state.setProperty ("loudResetOnPlay", strip.meterHub().resetIntegrationOnPlay(), nullptr);
        state.setProperty ("activeSlot", activeSlot, nullptr);
        state.setProperty ("stateVersion", kStateVersion, nullptr);

        for (auto old = state.getChildWithName ("AB"); old.isValid(); old = state.getChildWithName ("AB"))
            state.removeChild (old, nullptr);
        juce::ValueTree ab ("AB");
        auto current = makeSnapshot();
        ab.appendChild ((activeSlot == 0 ? current : slots[0]).createCopy().setProperty ("slot", 0, nullptr), nullptr);
        ab.appendChild ((activeSlot == 1 ? current : slots[1]).createCopy().setProperty ("slot", 1, nullptr), nullptr);
        state.appendChild (ab, nullptr);
    }

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void NorthlineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if ((int) tree.getProperty ("stateVersion", 1) < 2) migrateSessionV1 (tree);

    // Transient parameters are never restored.
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (isTransientId (tree.getChild (i).getProperty ("id").toString())) tree.removeChild (i, nullptr);

    ChannelStrip::Order o = ChannelStrip::defaultOrder;
    orderFromString (tree.getProperty ("order").toString(), o);
    strip.setOrder (o);

    {
        const juce::ScopedLock sl (stateLock);
        focus = focusFromString (tree.getProperty ("focus").toString());
        presetName = tree.getProperty ("presetName", "Init").toString();
        uiScale = (float) tree.getProperty ("uiScale", 1.0f);
        loudnessTarget = (int) tree.getProperty ("loudTarget", 0);
        loudnessCustomLufs = (double) tree.getProperty ("loudCustom", -14.0);
        strip.meterHub().setPauseIntegrationWhenStopped ((bool) tree.getProperty ("loudPause", false));
        strip.meterHub().setResetIntegrationOnPlay ((bool) tree.getProperty ("loudResetOnPlay", false));
        activeSlot = juce::jlimit (0, 1, (int) tree.getProperty ("activeSlot", 0));

        if (auto ab = tree.getChildWithName ("AB"); ab.isValid())
        {
            for (int i = 0; i < ab.getNumChildren(); ++i)
            {
                auto s = ab.getChild (i).createCopy();
                const int slot = juce::jlimit (0, 1, (int) s.getProperty ("slot", i));
                for (int k = s.getNumProperties(); --k >= 0;)
                    if (isTransientId (s.getPropertyName (k).toString())) s.removeProperty (s.getPropertyName (k), nullptr);
                slots[slot] = s;
            }
            tree.removeChild (ab, nullptr);
        }
    }

    apvts.replaceState (tree);
    resetTransientParams();

    {
        const juce::ScopedLock sl (stateLock);
        undoStack.clear();
        undoLabels.clear();
        undoPos = -1;
    }
    pushUndoSnapshot ("Load session");
    ++stateGeneration;
    if (juce::roundToInt (params[(size_t) P::matchMode]->convertFrom0to1 (params[(size_t) P::matchMode]->getValue())) == 1)
        strip.holdLevelMatch(); // a restored Learn trim is kept, not learned again
}

//==============================================================================
void NorthlineProcessor::setOrder (const ChannelStrip::Order& order, bool recordUndo, const juce::String& undoLabel)
{
    strip.setOrder (order);
    ++stateGeneration;
    if (recordUndo) pushUndoSnapshot (undoLabel.isEmpty() ? juce::String ("Reorder modules") : undoLabel);
}

void NorthlineProcessor::moveModule (int moduleId, int delta)
{
    auto o = getOrder();
    for (int i = 0; i < numModules; ++i)
    {
        if (o[(size_t) i] != moduleId) continue;
        const int j = i + delta;
        if (j < 0 || j >= numModules) return;
        std::swap (o[(size_t) i], o[(size_t) j]);
        setOrder (o, true, juce::String ("Move ") + moduleName (moduleId) + (delta < 0 ? " earlier" : " later"));
        return;
    }
}

juce::ValueTree NorthlineProcessor::makeSnapshot() const
{
    juce::ValueTree s ("Snapshot");
    for (int i = 0; i < P::count; ++i)
    {
        if (isTransient (i)) continue;
        auto* p = params[(size_t) i];
        s.setProperty (paramID (i), p->convertFrom0to1 (p->getValue()), nullptr);
    }
    s.setProperty ("order", orderToString (getOrder()), nullptr);
    {
        const juce::ScopedLock sl (stateLock);
        s.setProperty ("focus", focusToString (focus), nullptr);
        s.setProperty ("presetName", presetName, nullptr);
    }
    return s;
}

void NorthlineProcessor::applySnapshot (const juce::ValueTree& s)
{
    ChannelStrip::Order o = ChannelStrip::defaultOrder;
    orderFromString (s.getProperty ("order").toString(), o);
    strip.setOrder (o);
    {
        const juce::ScopedLock sl (stateLock);
        focus = focusFromString (s.getProperty ("focus").toString());
        presetName = s.getProperty ("presetName", presetName).toString();
    }

    // Parameters are written outside the lock: host callbacks must never run under it.
    ++applyingDepth;
    for (int i = 0; i < P::count; ++i)
    {
        if (isTransient (i)) continue;
        auto* p = params[(size_t) i];
        const auto id = paramID (i);
        const float plain = s.hasProperty (id) ? (float) s.getProperty (id) : paramDef (i).defV;
        const float norm = p->convertTo0to1 (plain);
        if (std::abs (norm - p->getValue()) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
    }
    --applyingDepth;

    // A stored Learn trim is kept, not learned again, whichever way the state arrived.
    if (juce::roundToInt (params[(size_t) P::matchMode]->convertFrom0to1 (params[(size_t) P::matchMode]->getValue())) == 1)
        strip.holdLevelMatch();
    ++stateGeneration;
}

void NorthlineProcessor::resetTransientParams()
{
    strip.setCompare (-1); // Compare and Delta are transient too
    for (int m = 0; m < numModules; ++m) strip.setDelta (m, false);
    for (int i = 0; i < P::count; ++i)
    {
        if (! isTransient (i)) continue;
        auto* p = params[(size_t) i];
        const float norm = p->convertTo0to1 (paramDef (i).defV);
        if (std::abs (norm - p->getValue()) > 1.0e-6f) p->setValueNotifyingHost (norm);
    }
}

void NorthlineProcessor::writeParameter (int index, float plain)
{
    auto* p = params[(size_t) index];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}

//==============================================================================
void NorthlineProcessor::pushUndoSnapshot (const juce::String& label)
{
    auto snapshot = makeSnapshot();
    const juce::ScopedLock sl (stateLock);
    if (undoPos >= 0 && undoPos < (int) undoStack.size() && undoStack[(size_t) undoPos].isEquivalentTo (snapshot)) return;
    undoStack.resize ((size_t) (undoPos + 1));
    undoLabels.resize ((size_t) (undoPos + 1));
    undoStack.push_back (snapshot);
    undoLabels.push_back (label.isEmpty() ? juce::String ("Change") : label);
    while (undoStack.size() > 33) // 32 undo levels
    {
        undoStack.erase (undoStack.begin());
        undoLabels.erase (undoLabels.begin());
    }
    undoPos = (int) undoStack.size() - 1;
}

bool NorthlineProcessor::canUndo() const
{
    const juce::ScopedLock sl (stateLock);
    return undoPos > 0;
}

bool NorthlineProcessor::canRedo() const
{
    const juce::ScopedLock sl (stateLock);
    return undoPos + 1 < (int) undoStack.size();
}

void NorthlineProcessor::undo()
{
    juce::ValueTree target;
    {
        const juce::ScopedLock sl (stateLock);
        if (undoPos <= 0) return;
        --undoPos;
        target = undoStack[(size_t) undoPos].createCopy();
    }
    applySnapshot (target);
}

void NorthlineProcessor::redo()
{
    juce::ValueTree target;
    {
        const juce::ScopedLock sl (stateLock);
        if (undoPos + 1 >= (int) undoStack.size()) return;
        ++undoPos;
        target = undoStack[(size_t) undoPos].createCopy();
    }
    applySnapshot (target);
}

std::vector<juce::String> NorthlineProcessor::getUndoHistory() const
{
    const juce::ScopedLock sl (stateLock);
    return std::vector<juce::String> (undoLabels.begin(), undoLabels.begin() + juce::jmax (0, undoPos + 1));
}

std::vector<juce::String> NorthlineProcessor::getRedoHistory() const
{
    const juce::ScopedLock sl (stateLock);
    return std::vector<juce::String> (undoLabels.begin() + juce::jmax (0, undoPos + 1), undoLabels.end());
}

void NorthlineProcessor::undoTo (int timelineIndex)
{
    juce::ValueTree target;
    {
        const juce::ScopedLock sl (stateLock);
        if (timelineIndex < 0 || timelineIndex >= (int) undoStack.size() || timelineIndex == undoPos) return;
        undoPos = timelineIndex;
        target = undoStack[(size_t) undoPos].createCopy();
    }
    applySnapshot (target);
}

void NorthlineProcessor::parameterGestureChanged (int juceIndex, bool gestureIsStarting)
{
    if (applyingDepth.load() > 0) return; // a snapshot is being applied: not a user gesture
    int index = -1;
    for (int i = 0; i < P::count && index < 0; ++i)
        if (params[(size_t) i] != nullptr && params[(size_t) i]->getParameterIndex() == juceIndex) index = i;
    if (index < 0) return;

    if (gestureIsStarting)
    {
        gestureParam = index;
        gestureStartPlain = params[(size_t) index]->convertFrom0to1 (params[(size_t) index]->getValue());
        return;
    }

    juce::String label;
    if (gestureParam == index)
        label = undoLabelForParam (index, gestureStartPlain, params[(size_t) index]->convertFrom0to1 (params[(size_t) index]->getValue()));
    gestureParam = -1;
    {
        const juce::ScopedLock sl (stateLock);
        pendingUndoLabel = label;
    }
    triggerAsyncUpdate();
}

void NorthlineProcessor::handleAsyncUpdate()
{
    juce::String label;
    {
        const juce::ScopedLock sl (stateLock);
        label = pendingUndoLabel;
        pendingUndoLabel.clear();
    }
    pushUndoSnapshot (label);
}

void NorthlineProcessor::timerCallback() { servicePendingWork(); }

void NorthlineProcessor::updateLatency()
{
    const int lat = strip.getLatencySamples();
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void NorthlineProcessor::servicePendingWork()
{
    updateLatency();

    // A finished Learn pass hands its trim to the message thread, which stores it in match_trim.
    const auto status = strip.getLevelMatchStatus();
    if (status.learnedSerial != lastLearnedSerial)
    {
        lastLearnedSerial = status.learnedSerial;
        writeParameter (P::matchTrim, status.learnedTrimDb);
    }

    // Smart Start: a finished analysis is applied here, on the message thread, as one undo step.
    if (smartStart.service (currentPlainValues())) applySmartResult (smartStart.lastResult());
}

//==============================================================================
int NorthlineProcessor::getActiveSlot() const
{
    const juce::ScopedLock sl (stateLock);
    return activeSlot;
}

void NorthlineProcessor::selectSlot (int slot)
{
    juce::ValueTree target;
    {
        const juce::ScopedLock sl (stateLock);
        slot = juce::jlimit (0, 1, slot);
        if (slot == activeSlot) return;
        slots[activeSlot] = makeSnapshot();
        activeSlot = slot;
        target = slots[slot].createCopy();
    }
    applySnapshot (target);
    pushUndoSnapshot (juce::String ("Show setting ") + (slot == 0 ? "A" : "B"));
}

void NorthlineProcessor::copyActiveToOther()
{
    const juce::ScopedLock sl (stateLock);
    slots[1 - activeSlot] = makeSnapshot();
}

juce::String NorthlineProcessor::getPresetName() const
{
    const juce::ScopedLock sl (stateLock);
    return presetName;
}

std::vector<int> NorthlineProcessor::getFocus() const
{
    const juce::ScopedLock sl (stateLock);
    return focus;
}

void NorthlineProcessor::setFocus (std::vector<int> paramIndices)
{
    {
        const juce::ScopedLock sl (stateLock);
        focus = std::move (paramIndices);
    }
    ++stateGeneration;
}

bool NorthlineProcessor::isInSpotlight (int paramIndex) const
{
    const juce::ScopedLock sl (stateLock);
    return std::find (focus.begin(), focus.end(), paramIndex) != focus.end();
}

void NorthlineProcessor::toggleSpotlight (int paramIndex)
{
    {
        const juce::ScopedLock sl (stateLock);
        if (auto it = std::find (focus.begin(), focus.end(), paramIndex); it != focus.end()) focus.erase (it);
        else focus.push_back (paramIndex);
    }
    ++stateGeneration;
}

float NorthlineProcessor::getUiScale() const
{
    const juce::ScopedLock sl (stateLock);
    return uiScale;
}

void NorthlineProcessor::setUiScale (float s)
{
    const juce::ScopedLock sl (stateLock);
    uiScale = s;
}

std::pair<int, double> NorthlineProcessor::getLoudnessTarget() const
{
    const juce::ScopedLock sl (stateLock);
    return { loudnessTarget, loudnessCustomLufs };
}

void NorthlineProcessor::setLoudnessTarget (int index, double customLufs)
{
    const juce::ScopedLock sl (stateLock);
    loudnessTarget = index;
    loudnessCustomLufs = customLufs;
}

void NorthlineProcessor::applyValues (const std::vector<std::pair<int, float>>& values)
{
    juce::ValueTree s ("Snapshot");
    for (int i = 0; i < P::count; ++i)
        if (! isTransient (i)) s.setProperty (paramID (i), paramDef (i).defV, nullptr);
    for (auto& [index, v] : values)
        if (! isTransient (index)) s.setProperty (paramID (index), v, nullptr);
    s.setProperty ("order", orderToString (ChannelStrip::defaultOrder), nullptr);
    applySnapshot (s);
}

void NorthlineProcessor::resetToDefaults()
{
    {
        const juce::ScopedLock sl (stateLock);
        focus.clear();
        presetName = "Init";
    }
    applyValues ({});
    resetTransientParams();
    pushUndoSnapshot ("Reset all settings");
}

void NorthlineProcessor::loadFactoryPreset (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& fp = list[(size_t) index];
    juce::ValueTree s ("Snapshot");
    for (int i = 0; i < P::count; ++i)
        if (! isTransient (i)) s.setProperty (paramID (i), paramDef (i).defV, nullptr);
    for (auto& [pi, v] : fp.values)
        if (! isTransient (pi)) s.setProperty (paramID (pi), v, nullptr);
    s.setProperty ("order", orderToString (fp.order), nullptr);
    s.setProperty ("focus", focusToString (fp.focus), nullptr);
    s.setProperty ("presetName", fp.name, nullptr);
    applySnapshot (s);
    resetTransientParams();
    pushUndoSnapshot (juce::String ("Load preset: ") + fp.name);
}

bool NorthlineProcessor::saveUserPreset (const juce::File& file, const juce::String& name, const PresetMeta& meta)
{
    {
        const juce::ScopedLock sl (stateLock);
        presetName = name;
    }
    juce::ValueTree root ("NorthlinePreset");
    root.setProperty ("version", kStateVersion, nullptr);
    root.setProperty ("name", name, nullptr);
    root.setProperty ("category", meta.category, nullptr);
    root.setProperty ("tags", meta.tags.joinIntoString (", "), nullptr);
    root.setProperty ("note", meta.note, nullptr);
    root.appendChild (makeSnapshot(), nullptr);
    file.getParentDirectory().createDirectory();
    if (auto xml = root.createXml()) return xml->writeTo (file);
    return false;
}

bool NorthlineProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("NorthlinePreset")) return false;
    auto root = juce::ValueTree::fromXml (*xml);
    auto snap = root.getChildWithName ("Snapshot");
    if (! snap.isValid()) return false;
    snap = snap.createCopy();
    if ((int) root.getProperty ("version", 1) < 2) migrateSnapshotV1 (snap);
    const juce::String name = root.getProperty ("name", file.getFileNameWithoutExtension()).toString();
    snap.setProperty ("presetName", name, nullptr);
    applySnapshot (snap);
    resetTransientParams();
    pushUndoSnapshot ("Load preset: " + name);
    return true;
}
//==============================================================================
// Module presets.
int NorthlineProcessor::moduleOfParam (int i)
{
    if (i >= P::preOn && i <= P::weightTrack) return modPre;
    if (i >= P::gateOn && i <= P::gateLookahead) return modGate;
    if (i >= P::dsOn && i <= P::ds2Range) return modDs;
    if (i >= P::eqOn && i <= P::eqHighDynExt) return modEq;
    if (i >= P::compOn && i <= P::compLookahead) return modComp;
    return -1;
}

juce::String NorthlineProcessor::getModuleXml (int moduleId, const juce::String& name) const
{
    juce::ValueTree m ("NorthlineModule");
    m.setProperty ("version", kStateVersion, nullptr);
    m.setProperty ("module", moduleId, nullptr);
    m.setProperty ("name", name, nullptr);
    for (int i = 0; i < P::count; ++i)
    {
        if (moduleOfParam (i) != moduleId || isTransient (i)) continue;
        auto* p = params[(size_t) i];
        m.setProperty (paramID (i), (double) p->convertFrom0to1 (p->getValue()), nullptr);
    }
    auto xml = m.createXml();
    return xml != nullptr ? xml->toString() : juce::String();
}

bool NorthlineProcessor::saveModulePreset (const juce::File& file, int moduleId, const juce::String& name)
{
    if (moduleId < 0 || moduleId >= numModules) return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (getModuleXml (moduleId, name));
}

namespace
{
// Reads a module preset. Only parameters of that module that are not transient are taken.
bool readModuleXml (const juce::XmlElement& xml, int& moduleId, juce::String& name, std::vector<std::pair<int, float>>& values)
{
    if (! xml.hasTagName ("NorthlineModule")) return false;
    moduleId = xml.getIntAttribute ("module", -1);
    if (moduleId < 0 || moduleId >= numModules) return false;
    name = xml.getStringAttribute ("name");
    for (int i = 0; i < P::count; ++i)
    {
        if (NorthlineProcessor::moduleOfParam (i) != moduleId || isTransient (i)) continue;
        const auto id = paramID (i);
        if (xml.hasAttribute (id)) values.emplace_back (i, (float) xml.getDoubleAttribute (id));
    }
    return true;
}
} // namespace

void NorthlineProcessor::applyParamsAsOneStep (const std::vector<std::pair<int, float>>& values, const juce::String& label)
{
    ++applyingDepth;
    for (auto& [index, plain] : values)
    {
        auto* p = params[(size_t) index];
        const float norm = p->convertTo0to1 (plain);
        if (std::abs (norm - p->getValue()) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
    }
    --applyingDepth;
    pushUndoSnapshot (label);
}

bool NorthlineProcessor::loadModulePreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    int moduleId = -1;
    juce::String name;
    std::vector<std::pair<int, float>> values;
    if (xml == nullptr || ! readModuleXml (*xml, moduleId, name, values)) return false;
    applyParamsAsOneStep (values, juce::String (moduleName (moduleId)) + " preset: " + (name.isEmpty() ? file.getFileNameWithoutExtension() : name));
    return true;
}

bool NorthlineProcessor::pasteModuleXml (const juce::String& text)
{
    auto xml = juce::XmlDocument::parse (text);
    int moduleId = -1;
    juce::String name;
    std::vector<std::pair<int, float>> values;
    if (xml == nullptr || ! readModuleXml (*xml, moduleId, name, values)) return false;
    applyParamsAsOneStep (values, juce::String ("Paste ") + moduleName (moduleId) + " module");
    return true;
}
//==============================================================================
// Compare and Delta (9.3)

void NorthlineProcessor::setDelta (int moduleId, bool on)
{
    if (moduleId == modDs) writeParameter (P::dsListen, on ? 3.0f : 0.0f);         // the de-esser's own Delta listen
    else if (moduleId == modComp) writeParameter (P::compListen, on ? 2.0f : 0.0f); // the compressor's own Delta listen
    else strip.setDelta (moduleId, on);
}

bool NorthlineProcessor::getDelta (int moduleId) const
{
    if (moduleId == modDs) return juce::roundToInt (plainValue (P::dsListen)) == 3;
    if (moduleId == modComp) return juce::roundToInt (plainValue (P::compListen)) == 2;
    return strip.getDelta (moduleId);
}

//==============================================================================
// Smart Start (9.1)

bool NorthlineProcessor::smartListen (smart::Source source) { return smartStart.listen (source); }
void NorthlineProcessor::smartCancel() { smartStart.cancel(); }

bool NorthlineProcessor::applySmartResult (const smart::Result& result)
{
    if (! result.valid || result.changes.empty()) return false;
    auto snapshot = makeSnapshot();
    std::vector<int> changed;
    for (const auto& c : result.changes)
    {
        snapshot.setProperty (paramID (c.param), (double) c.to, nullptr);
        changed.push_back (c.param);
    }
    applySnapshot (snapshot); // parameters are written without undo entries
    pushUndoSnapshot();       // one entry for the whole analysis
    setFocus (changed);       // Spotlight shows exactly what changed
    return true;
}

//==============================================================================
// Instance Hub (9.2)

void NorthlineProcessor::updateTrackProperties (const TrackProperties& properties)
{
    const juce::ScopedLock sl (stateLock);
    trackName = properties.name.value_or (juce::String());
    trackColourARGB = properties.colourARGB.value_or ((juce::uint32) 0);
}

juce::String NorthlineProcessor::getTrackName() const
{
    const juce::ScopedLock sl (stateLock);
    return trackName;
}

juce::uint32 NorthlineProcessor::getTrackColourARGB() const
{
    const juce::ScopedLock sl (stateLock);
    return trackColourARGB;
}

float NorthlineProcessor::plainValue (int index) const
{
    auto* p = params[(size_t) index];
    return p->convertFrom0to1 (p->getValue());
}

std::array<float, P::count> NorthlineProcessor::currentPlainValues() const
{
    std::array<float, P::count> values {};
    for (int i = 0; i < P::count; ++i) values[(size_t) i] = plainValue (i);
    return values;
}

} // namespace nl

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new nl::NorthlineProcessor(); }
