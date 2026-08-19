#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/*
    One palette for every plugin in the repo.

    The reference is FabFilter's visual language: near-black neutral ground, a
    quiet grid that reads as structure rather than decoration, one bright curve,
    and saturated per-band accents that stay legible against the dark.

    Two rules that matter more than the exact hex values:

      1. The grid must sit far enough below the curve in contrast that the eye
         reads the curve first. Grid lines that compete are the single most
         common way a serious analyser ends up looking amateur.

      2. Band accents are distinguishable by hue AND by lightness, so bands
         remain tellable apart on a bad monitor or by a colourblind user.
*/
namespace ui::theme
{

// ── Ground ──────────────────────────────────────────────────────────────
inline const juce::Colour background      { 0xff16181d };
inline const juce::Colour displayBackground { 0xff101216 };
inline const juce::Colour panel           { 0xff1c1f26 };
inline const juce::Colour panelRaised     { 0xff232730 };
inline const juce::Colour border          { 0xff2c313b };

// ── Grid ────────────────────────────────────────────────────────────────
// Deliberately low contrast. Decades get the stronger line; everything else
// recedes.
inline const juce::Colour gridLine        { 0x14ffffff };
inline const juce::Colour gridLineStrong  { 0x26ffffff };
inline const juce::Colour gridZeroLine    { 0x3affffff };

// ── Text ────────────────────────────────────────────────────────────────
inline const juce::Colour text            { 0xffe4e8f0 };
inline const juce::Colour textDim         { 0xff8b93a3 };
inline const juce::Colour textFaint       { 0xff5a6273 };

// ── Curve and analyser ──────────────────────────────────────────────────
inline const juce::Colour curve           { 0xfff2f4f8 };
inline const juce::Colour curveFill       { 0x14f2f4f8 };
inline const juce::Colour spectrum        { 0x33aab4c8 };
inline const juce::Colour spectrumPeak    { 0x66c8d2e6 };

// ── Band accents ────────────────────────────────────────────────────────
// Ordered so adjacent bands never share a neighbouring hue.
inline const juce::Colour bandColours[] = {
    juce::Colour { 0xff4db6e8 },   // cyan
    juce::Colour { 0xffe8a33d },   // amber
    juce::Colour { 0xff7ed957 },   // green
    juce::Colour { 0xffe86a8f },   // pink
    juce::Colour { 0xffa87ce8 },   // violet
    juce::Colour { 0xffe8d44d },   // yellow
};

inline juce::Colour bandColour (int index) noexcept
{
    constexpr int count = (int) (sizeof (bandColours) / sizeof (bandColours[0]));
    return bandColours[((index % count) + count) % count];
}

// ── Type ────────────────────────────────────────────────────────────────
// Small, tight, and never bold except for the product name. Precision reads as
// competence in this category; heavy type reads as consumer software.
inline juce::Font labelFont (float height = 11.0f)
{
    return juce::Font { juce::FontOptions { height } };
}

inline juce::Font valueFont (float height = 12.0f)
{
    return juce::Font { juce::FontOptions { height } };
}

inline juce::Font titleFont (float height = 13.0f)
{
    return juce::Font { juce::FontOptions { height }.withStyle ("Bold") };
}

} // namespace ui::theme
