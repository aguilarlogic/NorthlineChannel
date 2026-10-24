#pragma once

#include "LookAndFeel.h"
#include <functional>
#include <memory>

namespace nl
{
// Overlay host (roadmap 8.4 and 8.6). An overlay (preset browser, Smart Start, Instance Hub, settings,
// first-run tour) is shown as a centred card over the editor, which is dimmed behind it. The card has a
// radius, a shadow, a title row and a close button. The overlay itself is only the card's content.
namespace overlay
{
enum class Size { compact, medium, large };

constexpr float kDimAlpha = 0.6f; // black over the editor while an overlay is open
constexpr int kTitleH = 44;       // title row: title and close button
constexpr int kShadow = 22;       // room around the card for its shadow (the card component includes it)
constexpr int kMargin = 40;       // least gap between the card and the editor canvas, per side
constexpr int kBodyPadX = 16, kBodyPadY = 12;

// The card rectangle for `size`, centred in `area` (editor canvas pixels). Never larger than `area`
// less kMargin on each side, so a small window still shows the whole card.
juce::Rectangle<int> cardBounds (juce::Rectangle<int> area, Size size);

// Bounds of the card component for a card rectangle: the card plus the shadow margin.
juce::Rectangle<int> componentBounds (juce::Rectangle<int> card);

// Where the overlay content sits in a card component: below the title row, inside the padding.
juce::Rectangle<int> bodyBounds (juce::Rectangle<int> cardComponent);
} // namespace overlay

// Dims the editor while an overlay is open. Clicks land here and go no further, so the editor below
// does not react to them. Overlays close with the close button or Esc, not by clicking the dim.
class OverlayDim : public juce::Component
{
public:
    OverlayDim() { setInterceptsMouseClicks (true, true); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (overlay::kDimAlpha)); }
};

// The card: panel, radius, shadow, a title row with the close button, and the overlay content below.
class OverlayCard : public juce::Component
{
public:
    OverlayCard (std::unique_ptr<juce::Component> content, const juce::String& title, overlay::Size size);
    ~OverlayCard() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    overlay::Size getSize() const noexcept { return size; }
    juce::Component* getContent() const noexcept { return body.get(); }

    // Called by the close button (the editor closes the overlay from here).
    std::function<void()> onClose;

private:
    class CloseButton;

    std::unique_ptr<juce::Component> body;
    std::unique_ptr<CloseButton> close;
    juce::String title;
    overlay::Size size;
};
} // namespace nl
