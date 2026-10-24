#include "UiSettings.h"

namespace nl
{
namespace
{
juce::PropertiesFile::Options fileOptions()
{
    juce::PropertiesFile::Options o;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.commonToAllUsers = false;
    o.ignoreCaseOfKeyNames = false;
    return o;
}

juce::File defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("NorthlineChannel")
        .getChildFile ("ui.settings");
}
} // namespace

UiSettings& UiSettings::get()
{
    static UiSettings settings (defaultFile());
    return settings;
}

UiSettings::UiSettings (const juce::File& f)
{
    useFile (f);
}

UiSettings::~UiSettings()
{
    store();
}

void UiSettings::useFile (const juce::File& f)
{
    store();
    file = f;
    props = std::make_unique<juce::PropertiesFile> (file, fileOptions());
}

juce::File UiSettings::getFile() const { return file; }

void UiSettings::store()
{
    if (props == nullptr) return;
    file.getParentDirectory().createDirectory();
    props->saveIfNeeded();
}

tokens::Theme UiSettings::theme() const
{
    return props != nullptr && props->getIntValue ("theme", 0) == 1 ? tokens::Theme::highContrast : tokens::Theme::dark;
}

void UiSettings::setTheme (tokens::Theme t)
{
    if (props == nullptr || theme() == t) return;
    props->setValue ("theme", t == tokens::Theme::highContrast ? 1 : 0);
    store();
    sendChangeMessage();
}

bool UiSettings::consoleLayout() const { return props != nullptr && props->getBoolValue ("consoleLayout", false); }

void UiSettings::setConsoleLayout (bool on)
{
    if (props == nullptr || consoleLayout() == on) return;
    props->setValue ("consoleLayout", on);
    store();
    sendChangeMessage();
}

bool UiSettings::showCpu() const { return props != nullptr && props->getBoolValue ("showCpu", false); }

void UiSettings::setShowCpu (bool on)
{
    if (props == nullptr || showCpu() == on) return;
    props->setValue ("showCpu", on);
    store();
    sendChangeMessage();
}

float UiSettings::defaultScale() const
{
    return props == nullptr ? kDefaultScale : clampScale ((float) props->getDoubleValue ("defaultScale", kDefaultScale));
}

void UiSettings::setDefaultScale (float s)
{
    if (props == nullptr) return;
    props->setValue ("defaultScale", (double) clampScale (s));
    store();
    sendChangeMessage();
}

void UiSettings::applyDefaultScaleToAllInstances()
{
    ++serial;
    sendChangeMessage();
}

int UiSettings::applyAllSerial() const { return serial; }

bool UiSettings::onboardingDone() const { return props != nullptr && props->getBoolValue ("onboardingDone", false); }

void UiSettings::setOnboardingDone (bool done)
{
    if (props == nullptr || onboardingDone() == done) return;
    props->setValue ("onboardingDone", done);
    store();
}

float UiSettings::clampScale (float scale)
{
    return juce::jlimit (kMinScale, kMaxScale, scale);
}
} // namespace nl
