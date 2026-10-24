#include "OverlayCard.h"

namespace nl
{
namespace overlay
{
namespace
{
juce::Point<int> preferred (Size size)
{
    switch (size)
    {
        case Size::compact: return { 560, 300 };
        case Size::medium: return { 640, 520 };
        case Size::large: return { 1120, 680 };
    }
    return { 560, 360 };
}
} // namespace

juce::Rectangle<int> cardBounds (juce::Rectangle<int> area, Size size)
{
    const auto want = preferred (size);
    const int w = juce::jmax (1, juce::jmin (want.x, area.getWidth() - 2 * kMargin));
    const int h = juce::jmax (1, juce::jmin (want.y, area.getHeight() - 2 * kMargin));
    return juce::Rectangle<int> (w, h).withCentre (area.getCentre());
}

juce::Rectangle<int> componentBounds (juce::Rectangle<int> card)
{
    return card.expanded (kShadow, kShadow);
}

juce::Rectangle<int> bodyBounds (juce::Rectangle<int> cardComponent)
{
    auto r = cardComponent.reduced (kShadow, kShadow);
    r.removeFromTop (kTitleH);
    return r.reduced (kBodyPadX, kBodyPadY);
}
} // namespace overlay

//==============================================================================
// The close button: an X in the title row, lit on hover.
class OverlayCard::CloseButton : public juce::Button
{
public:
    CloseButton() : juce::Button ("Close")
    {
        setTooltip ("Close (Esc)");
        setTitle ("Close");
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        if (over || down)
        {
            g.setColour (colours::track.withAlpha (down ? 1.0f : 0.7f));
            g.fillRoundedRectangle (r, tokens::radius::control);
        }
        const float cx = r.getCentreX(), cy = r.getCentreY(), k = juce::jmin (r.getWidth(), r.getHeight()) * 0.22f;
        juce::Path x;
        x.startNewSubPath (cx - k, cy - k);
        x.lineTo (cx + k, cy + k);
        x.startNewSubPath (cx + k, cy - k);
        x.lineTo (cx - k, cy + k);
        g.setColour (over ? colours::text : colours::dim);
        g.strokePath (x, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

//==============================================================================
OverlayCard::OverlayCard (std::unique_ptr<juce::Component> content, const juce::String& t, overlay::Size s)
    : body (std::move (content)), close (std::make_unique<CloseButton>()), title (t), size (s)
{
    setOpaque (false);
    close->onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (*close);
    if (body != nullptr) addAndMakeVisible (*body);
}

OverlayCard::~OverlayCard() = default;

void OverlayCard::paint (juce::Graphics& g)
{
    const auto card = getLocalBounds().reduced (overlay::kShadow, overlay::kShadow).toFloat();
    const float radius = 10.0f;

    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 18, { 0, 6 }).drawForRectangle (g, card.toNearestInt());

    g.setColour (colours::panel);
    g.fillRoundedRectangle (card, radius);

    // Title row: a slightly lighter band with its top corners rounded, and a rule under it.
    const auto header = card.withHeight ((float) overlay::kTitleH);
    juce::Path top;
    top.addRoundedRectangle (header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius, radius, true, true, false, false);
    g.setColour (colours::panelHeader);
    g.fillPath (top);
    g.setColour (colours::edge);
    g.drawHorizontalLine ((int) (card.getY() + (float) overlay::kTitleH), card.getX(), card.getRight());
    g.drawRoundedRectangle (card.reduced (0.5f), radius, 1.0f);

    g.setColour (colours::text);
    g.setFont (NorthlineLookAndFeel::font (16.0f, true));
    g.drawText (title, header.reduced (18.0f, 0.0f).toNearestInt().withTrimmedRight (48), juce::Justification::centredLeft, true);
}

void OverlayCard::resized()
{
    const auto card = getLocalBounds().reduced (overlay::kShadow, overlay::kShadow);
    close->setBounds (juce::Rectangle<int> (30, 30).withCentre ({ card.getRight() - 22, card.getY() + overlay::kTitleH / 2 }));
    if (body != nullptr) body->setBounds (overlay::bodyBounds (getLocalBounds()));
}
} // namespace nl
