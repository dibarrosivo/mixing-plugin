# Mixing Plugin

A JUCE audio plugin, built from the ground up. Currently: input gain → one
peaking EQ band → output gain, with bypass.

The point of this stage is not the effect — it is a correct, real-time-safe
skeleton with room to grow.

Formats: **VST3** and **standalone**. Platform: Linux (VST3/LV2/CLAP territory —
AU and AAX are macOS / Avid-only).

---

## Build

One-time dependency install (Ubuntu 22.04):

```bash
sudo apt install -y cmake ninja-build build-essential pkg-config \
  libasound2-dev libjack-jackd2-dev \
  libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxcursor-dev libxcomposite-dev \
  libgl1-mesa-dev
```

`libcurl` and `libwebkit2gtk` are *not* needed — `JUCE_USE_CURL=0` and
`JUCE_WEB_BROWSER=0` in [CMakeLists.txt](CMakeLists.txt) switch those modules off.

Then:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The first configure clones JUCE into `build/_deps` (a few hundred MB, once).

## Run it

The standalone app is the fastest loop while developing — no host, no rescan:

```bash
./build/MixingPlugin_artefacts/Release/Standalone/Mixing\ Plugin
```

The VST3 is copied to `~/.vst3/` automatically after every build
(`COPY_PLUGIN_AFTER_BUILD`), so a rebuild is immediately live in your host. You
only need to rescan the first time.

No DAW installed yet — `sudo apt install carla` is the lightest option for
loading and testing plugins; Reaper is the better long-term choice on Linux.

## Tests

The DSP headers avoid JUCE where they can, so the tests need **no framework, no
JUCE and no CMake** — which means they run before the toolchain above is even
installed:

```bash
g++ -std=c++20 -O2 -Isource -o /tmp/biquad_test tests/biquad_test.cpp && /tmp/biquad_test
```

Or through CMake, once you have it:

```bash
cmake --build build --target biquad_test && ctest --test-dir build --output-on-failure
```

[tests/biquad_test.cpp](tests/biquad_test.cpp) measures the filter two
independent ways — a real sine through `processSample()`, and `|H(e^jw)|`
evaluated from the coefficients — and requires them to agree. Either can be
confidently wrong on its own; both agreeing *and* matching the requested gain is
hard to fake.

It covers transparency at 0 dB, exact peak gain at `f0`, skirt behaviour,
bandwidth (`f0/Q`, verified to 0.32% — the residual is bilinear-transform
warping), unity at DC and Nyquist, stability under high-Q/low-frequency
settings, and `reset()`.

**The suite has been mutation-tested.** Eight deliberate breakages — sign flips
in the difference equation, the classic RBJ `/40`→`/20` slip, swapped
coefficients, a missing `a0` normalisation, a `reset()` that forgets `s2` —
are all caught. Two of those escaped the first version of the suite; if you
add a test here, break the code on purpose and confirm it goes red, because a
test that has never failed has not been shown to work.

---

## Layout

```
source/
  Parameters.h/.cpp    every parameter ID and range, in one place
  PluginProcessor.*    the audio thread: owns the chain, pulls parameters
  PluginEditor.*       the UI thread: sliders bound to parameters
  dsp/
    Biquad.h           allocation-free second-order IIR section (RBJ cookbook)
    GainStage.h        smoothed gain — the template every module follows
    ToneFilter.h       one peaking EQ band built on Biquad
```

### The module contract

Every DSP module exposes the same three methods:

| Method | Thread | Rules |
| --- | --- | --- |
| `prepare(spec)` | message | Called before playback. Allocation allowed. |
| `process(block)` | **audio** | No allocation, no locks, no I/O. Ever. |
| `reset()` | either | Clear state without reallocating. |

Follow that shape and a new module drops into the chain in four lines.

### Adding a parameter

1. Add the ID to `ParamID` in [Parameters.h](source/Parameters.h)
2. Add it to `createParameterLayout()` in [Parameters.cpp](source/Parameters.cpp)
3. Cache its `std::atomic<float>*` in the processor constructor
4. Add an `AttachedSlider` in [PluginEditor.h](source/PluginEditor.h)

State saving needs no changes — APVTS serialises the whole tree.

### Adding a DSP module

1. New header in `source/dsp/` following the contract above
2. Declare it as a member in [PluginProcessor.h](source/PluginProcessor.h)
3. `prepare` it in `prepareToPlay`, `reset` it in `releaseResources`
4. Call `process(block)` in `processBlock`, in chain order

---

## Things this skeleton already gets right

These are the mistakes that are painful to retrofit, so they are handled from
the start:

- **No allocation on the audio thread.** `Biquad` is a plain struct precisely
  because `juce::dsp::IIR`'s coefficient factories allocate.
- **Parameter smoothing.** Raw gain changes on a block boundary click audibly.
- **Denormal protection.** `ScopedNoDenormals` in `processBlock`.
- **No string lookups in the audio path.** Parameter pointers are cached once.
- **Host-visible bypass** via `getBypassParameter()`, so automation and the
  host's own bypass button stay in sync.
- **Per-channel filter state.** Sharing it between channels bleeds one into
  the other.

## Roadmap

Rough order, each step building on the last:

1. **Coefficient smoothing** — sweeping the EQ frequency currently steps per
   block. Interpolate the coefficients, or move to a TPT state-variable filter.
2. **More bands** — turn `ToneFilter` into an array; low/high shelf and
   high/low pass variants are more RBJ formulas on the same `Biquad`.
3. **Metering** — input/output level, gain reduction. Needs a lock-free
   audio → UI path (atomics or a FIFO), never a mutex.
4. **Delay line** — unlocks delay, chorus, flanger.
5. **Compressor** — envelope follower, threshold/ratio/attack/release/knee.
6. **Saturation** — waveshaping plus oversampling to control aliasing.
7. **A real UI** — custom `LookAndFeel`, a response curve for the EQ.
8. **More tests** — `Biquad` is covered and mutation-verified. `GainStage` and
   `ToneFilter` are not, because they pull in JUCE; splitting the smoothing
   maths out from the JUCE types would make them testable the same way.

## Licence

JUCE is fetched under its own terms — GPLv3 or a commercial/personal JUCE
licence. `JUCE_DISPLAY_SPLASH_SCREEN` is deliberately left at its default;
disabling it requires an appropriate licence.
