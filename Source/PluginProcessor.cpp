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
} // namespace

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
    slots[0] = slots[1] = makeSnapshot();
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
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != mainOut) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void NorthlineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = std::max (1, getMainBusNumOutputChannels());
    preparedBlock = std::max (16, samplesPerBlock);
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

    // Process in chunks no larger than the prepared block size.
    int pos = 0;
    while (pos < n)
    {
        const int len = juce::jmin (n - pos, preparedBlock);
        float* io[2] { mainOut.getWritePointer (0) + pos, numCh > 1 ? mainOut.getWritePointer (1) + pos : nullptr };
        const float* key[2] { sc[0] != nullptr ? sc[0] + pos : nullptr, sc[1] != nullptr ? sc[1] + pos : nullptr };
        strip.process (io, numCh, len, scCh > 0 ? key : nullptr, scCh);
        pos += len;
    }
}

//==============================================================================
juce::AudioProcessorEditor* NorthlineProcessor::createEditor() { return new NorthlineEditor (*this); }

void NorthlineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("order", orderToString (getOrder()), nullptr);
    state.setProperty ("focus", focusToString (focus), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    state.setProperty ("activeSlot", activeSlot, nullptr);
    state.setProperty ("stateVersion", 1, nullptr);

    for (auto old = state.getChildWithName ("AB"); old.isValid(); old = state.getChildWithName ("AB"))
        state.removeChild (old, nullptr);
    juce::ValueTree ab ("AB");
    auto current = makeSnapshot();
    ab.appendChild ((activeSlot == 0 ? current : slots[0]).createCopy().setProperty ("slot", 0, nullptr), nullptr);
    ab.appendChild ((activeSlot == 1 ? current : slots[1]).createCopy().setProperty ("slot", 1, nullptr), nullptr);
    state.appendChild (ab, nullptr);

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void NorthlineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = juce::ValueTree::fromXml (*xml);
    ChannelStrip::Order o = ChannelStrip::defaultOrder;
    orderFromString (tree.getProperty ("order").toString(), o);
    strip.setOrder (o);
    focus = focusFromString (tree.getProperty ("focus").toString());
    presetName = tree.getProperty ("presetName", "Init").toString();
    uiScale = (float) tree.getProperty ("uiScale", 1.0f);
    activeSlot = juce::jlimit (0, 1, (int) tree.getProperty ("activeSlot", 0));

    if (auto ab = tree.getChildWithName ("AB"); ab.isValid())
    {
        for (int i = 0; i < ab.getNumChildren(); ++i)
        {
            auto s = ab.getChild (i);
            const int slot = juce::jlimit (0, 1, (int) s.getProperty ("slot", i));
            slots[slot] = s.createCopy();
        }
        tree.removeChild (ab, nullptr);
    }

    apvts.replaceState (tree);
    undoStack.clear();
    undoPos = -1;
    pushUndoSnapshot();
    ++stateGeneration;
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
        auto* p = params[(size_t) i];
        s.setProperty (paramID (i), p->convertFrom0to1 (p->getValue()), nullptr);
    }
    s.setProperty ("order", orderToString (getOrder()), nullptr);
    s.setProperty ("focus", focusToString (focus), nullptr);
    s.setProperty ("presetName", presetName, nullptr);
    return s;
}

void NorthlineProcessor::applySnapshot (const juce::ValueTree& s)
{
    const juce::ScopedValueSetter<bool> svs (applyingSnapshot, true);
    for (int i = 0; i < P::count; ++i)
    {
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
    ChannelStrip::Order o = ChannelStrip::defaultOrder;
    orderFromString (s.getProperty ("order").toString(), o);
    strip.setOrder (o);
    focus = focusFromString (s.getProperty ("focus").toString());
    presetName = s.getProperty ("presetName", presetName).toString();
    ++stateGeneration;
}

//==============================================================================
void NorthlineProcessor::pushUndoSnapshot()
{
    auto snap = makeSnapshot();
    if (undoPos >= 0 && undoPos < (int) undoStack.size() && undoStack[(size_t) undoPos].isEquivalentTo (snap)) return;
    undoStack.resize ((size_t) (undoPos + 1));
    undoStack.push_back (snap);
    while (undoStack.size() > 33) undoStack.erase (undoStack.begin()); // 32 undo levels
    undoPos = (int) undoStack.size() - 1;
}

void NorthlineProcessor::undo()
{
    if (! canUndo()) return;
    --undoPos;
    applySnapshot (undoStack[(size_t) undoPos]);
}

void NorthlineProcessor::redo()
{
    if (! canRedo()) return;
    ++undoPos;
    applySnapshot (undoStack[(size_t) undoPos]);
}

void NorthlineProcessor::parameterGestureChanged (int, bool gestureIsStarting)
{
    if (! gestureIsStarting && ! applyingSnapshot) triggerAsyncUpdate();
}

void NorthlineProcessor::handleAsyncUpdate() { pushUndoSnapshot(); }

void NorthlineProcessor::timerCallback()
{
    const int lat = strip.getLatencySamples();
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

//==============================================================================
void NorthlineProcessor::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == activeSlot) return;
    slots[activeSlot] = makeSnapshot();
    activeSlot = slot;
    applySnapshot (slots[slot]);
    pushUndoSnapshot();
}

void NorthlineProcessor::copyActiveToOther() { slots[1 - activeSlot] = makeSnapshot(); }

void NorthlineProcessor::applyValues (const std::vector<std::pair<int, float>>& values)
{
    juce::ValueTree s ("Snapshot");
    for (int i = 0; i < P::count; ++i) s.setProperty (paramID (i), paramDef (i).defV, nullptr);
    for (auto& [index, v] : values) s.setProperty (paramID (index), v, nullptr);
    s.setProperty ("order", orderToString (ChannelStrip::defaultOrder), nullptr);
    applySnapshot (s);
}

void NorthlineProcessor::resetToDefaults()
{
    focus.clear();
    presetName = "Init";
    applyValues ({});
    pushUndoSnapshot();
}

void NorthlineProcessor::loadFactoryPreset (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& fp = list[(size_t) index];
    presetName = fp.name;
    focus = fp.focus;
    juce::ValueTree s ("Snapshot");
    for (int i = 0; i < P::count; ++i) s.setProperty (paramID (i), paramDef (i).defV, nullptr);
    for (auto& [pi, v] : fp.values) s.setProperty (paramID (pi), v, nullptr);
    s.setProperty ("order", orderToString (fp.order), nullptr);
    s.setProperty ("focus", focusToString (fp.focus), nullptr);
    s.setProperty ("presetName", fp.name, nullptr);
    applySnapshot (s);
    pushUndoSnapshot();
}

bool NorthlineProcessor::saveUserPreset (const juce::File& file, const juce::String& name)
{
    presetName = name;
    juce::ValueTree root ("NorthlinePreset");
    root.setProperty ("version", 1, nullptr);
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
    snap.setProperty ("presetName", root.getProperty ("name", file.getFileNameWithoutExtension()), nullptr);
    applySnapshot (snap);
    pushUndoSnapshot();
    return true;
}
} // namespace nl

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new nl::NorthlineProcessor(); }
