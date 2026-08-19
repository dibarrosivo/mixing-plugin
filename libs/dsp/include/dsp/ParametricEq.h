#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

#include <dsp/ToneFilter.h>

/*
    An N-band parametric EQ: a stack of independent peaking bands in series.

    Series rather than parallel because that is what "an EQ" means — each band
    filters the output of the one before it, so their responses multiply
    (add in dB). Summing bands in parallel gives comb filtering and a response
    that looks nothing like the curve you drew.

    Bands are always processed while enabled, even at 0 dB where a peaking
    filter is mathematically unity. Skipping them would save a handful of
    multiplies and leave stale state in the filter, so the first sample after
    the gain moved off zero would carry a transient from whenever it was last
    active. Six biquads is not worth a discontinuity.

    This is the base the client's dynamic EQ sits on: a dynamic band is one of
    these whose gain is driven by a detector rather than by a knob.
*/
namespace dsp
{

template <size_t NumBands>
class ParametricEq
{
public:
    static constexpr size_t numBands = NumBands;

    struct Band
    {
        float frequencyHz { 1000.0f };
        float gainDb      { 0.0f };
        float q           { 0.707f };
        bool  enabled     { false };
    };

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        for (auto& filter : filters)
            filter.prepare (spec);
    }

    void reset()
    {
        for (auto& filter : filters)
            filter.reset();
    }

    void setBand (size_t index, const Band& band) noexcept
    {
        if (index >= NumBands)
            return;

        // A band switched off then on again must not resume mid-ring with
        // whatever was in its state from before.
        if (band.enabled && ! bands[index].enabled)
            filters[index].reset();

        bands[index] = band;
        filters[index].setParameters (band.frequencyHz, band.gainDb, band.q);
    }

    const Band& getBand (size_t index) const noexcept { return bands[index]; }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        for (size_t i = 0; i < NumBands; ++i)
            if (bands[i].enabled)
                filters[i].process (block);
    }

private:
    std::array<Band, NumBands>       bands {};
    std::array<ToneFilter, NumBands> filters {};
};

} // namespace dsp
