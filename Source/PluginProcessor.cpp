#include "PluginProcessor.h"
#include "gui/PluginEditor.h"

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
           || name == "activeSlot" || name == "stateVersion" || name == "version" || name == "name";
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
    pushUndoSnapshot();
    startTimerHz (10);
}

NorthlineProcessor::~NorthlineProcessor()
{
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
    setLatencySamples (strip.getLatencySamples());
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

    // Loudness integration can pause while the host transport is stopped (MeterHub option).
    if (auto* ph = getPlayHead())
        if (auto position = ph->getPosition())
            strip.meterHub().setTransportPlaying (position->getIsPlaying());

    // Process in chunks no larger than the prepared block size.
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
        strip.process (io, numCh, len, scCh > 0 ? key : nullptr, scCh);
        pos += len;
    }
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
        undoPos = -1;
    }
    pushUndoSnapshot();
    ++stateGeneration;
    if (juce::roundToInt (params[(size_t) P::matchMode]->convertFrom0to1 (params[(size_t) P::matchMode]->getValue())) == 1)
        strip.holdLevelMatch(); // a restored Learn trim is kept, not learned again
}

//==============================================================================
void NorthlineProcessor::setOrder (const ChannelStrip::Order& order, bool recordUndo)
{
    strip.setOrder (order);
    ++stateGeneration;
    if (recordUndo) pushUndoSnapshot();
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
        setOrder (o);
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
void NorthlineProcessor::pushUndoSnapshot()
{
    auto snapshot = makeSnapshot();
    const juce::ScopedLock sl (stateLock);
    if (undoPos >= 0 && undoPos < (int) undoStack.size() && undoStack[(size_t) undoPos].isEquivalentTo (snapshot)) return;
    undoStack.resize ((size_t) (undoPos + 1));
    undoStack.push_back (snapshot);
    while (undoStack.size() > 33) undoStack.erase (undoStack.begin()); // 32 undo levels
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

void NorthlineProcessor::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (! gestureIsStarting && applyingDepth.load() == 0) triggerAsyncUpdate();
}

void NorthlineProcessor::handleAsyncUpdate() { pushUndoSnapshot(); }

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
    pushUndoSnapshot();
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
    pushUndoSnapshot();
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
    pushUndoSnapshot();
}

bool NorthlineProcessor::saveUserPreset (const juce::File& file, const juce::String& name)
{
    {
        const juce::ScopedLock sl (stateLock);
        presetName = name;
    }
    juce::ValueTree root ("NorthlinePreset");
    root.setProperty ("version", kStateVersion, nullptr);
    root.setProperty ("name", name, nullptr);
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
    snap.setProperty ("presetName", root.getProperty ("name", file.getFileNameWithoutExtension()), nullptr);
    applySnapshot (snap);
    resetTransientParams();
    pushUndoSnapshot();
    return true;
}
} // namespace nl

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new nl::NorthlineProcessor(); }
