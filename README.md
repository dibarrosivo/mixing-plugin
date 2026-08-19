# Audio Plugins

A JUCE monorepo: shared, tested DSP in `libs/dsp`, one target per plugin under
`plugins/`. Adding a plugin is one command and does not touch any existing file.

Formats: **VST3** and **standalone**. Platform: Linux for now — see
[Cross-platform](#cross-platform) before promising anything to anyone.

---

## Layout

```
cmake/AudioPlugin.cmake     add_audio_plugin() — shared conventions + validation
libs/dsp/
  include/dsp/              the shared modules
    Biquad.h                allocation-free IIR section (RBJ cookbook)
    GainStage.h             smoothed gain — the template every module follows
    ToneFilter.h            one peaking EQ band, built on Biquad
  tests/                    no framework, no JUCE, no CMake required
plugins/
  mixing-plugin/            single-band EQ + gain staging
templates/plugin/           what scripts/new-plugin.sh instantiates
scripts/new-plugin.sh       scaffold a new plugin
```

Two library targets, split deliberately:

| Target | Depends on | For |
| --- | --- | --- |
| `audio::dsp_core` | nothing, not even JUCE | pure maths — testable with a bare `g++` |
| `audio::dsp` | `dsp_core` + `juce_dsp` | modules needing `SmoothedValue`, `AudioBlock` |

Keep new code in `dsp_core` unless it genuinely needs JUCE. Every module that
stays framework-free is a module testable in under a second.

## Build

One-time dependencies (Ubuntu 22.04):

```bash
sudo apt install -y cmake ninja-build build-essential pkg-config \
  libasound2-dev libjack-jackd2-dev \
  libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxcursor-dev libxcomposite-dev libgl1-mesa-dev
```

`libcurl` and `libwebkit2gtk` are **not** needed — those JUCE modules are
compiled out in `cmake/AudioPlugin.cmake`.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The first configure clones JUCE into `build/_deps` (once, shared by all plugins).
Every VST3 is copied to `~/.vst3/` after each build; standalones land in
`build/plugins/<dir>/<Target>_artefacts/Release/Standalone/`.

To build one plugin, use the **`_All`** suffix:

```bash
cmake --build build --target MixingPlugin_All
```

The bare target is JUCE's shared-code library and produces no loadable plugin
on its own.

## Tests

`libs/dsp/tests` needs no framework, no JUCE and no CMake, so it runs before the
toolchain above is even installed:

```bash
g++ -std=c++20 -O2 -Ilibs/dsp/include -o /tmp/biquad_test libs/dsp/tests/biquad_test.cpp && /tmp/biquad_test
```

Or through CTest:

```bash
ctest --test-dir build --output-on-failure
```

`biquad_test.cpp` measures the filter two independent ways — a real sine through
`processSample()`, and `|H(e^jw)|` from the coefficients — and requires them to
agree. Either can be confidently wrong alone.

**The suite is mutation-tested.** Eight deliberate breakages (sign flips, the
classic RBJ `/40`→`/20` slip, swapped coefficients, a missing `a0`
normalisation, a `reset()` that forgets `s2`) are all caught. Two escaped the
first version of the suite. **If you add a test here, break the code on purpose
and confirm it goes red** — a test that has never failed has not been shown to
work.

---

## Adding a plugin

```bash
./scripts/new-plugin.sh \
  --dir voice-chain \
  --target VoiceChain \
  --name "Voice Chain" \
  --code Vch1 \
  --description "All-in-one vocal processor"

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target VoiceChain_All
```

That is the whole process. Plugins are auto-discovered, so no existing file
changes — nothing to merge-conflict on when two plugins are added in parallel.

You get a buildable gain-in/gain-out plugin with parameters, editor, state
save/load and bypass already wired. Add DSP in `processBlock`.

### `--code` matters more than it looks

`PLUGIN_CODE` is baked into the plugin's unique ID. Two plugins sharing one
makes hosts treat them as the same plugin and silently load the wrong one —
near-impossible to diagnose from the host side. It is validated in **two**
places (the script and `add_audio_plugin()`): exactly 4 alphanumerics, at least
one uppercase, unique across the repo.

Changing a code after release makes hosts drop the plugin from saved projects.
Pick it once.

### Adding a parameter

1. Add the ID to `ParamID` in the plugin's `Parameters.h`
2. Add it to `createParameterLayout()` in `Parameters.cpp`
3. Cache its `std::atomic<float>*` in the processor constructor
4. Add an `AttachedSlider` in `PluginEditor.h`

State saving needs no changes — APVTS serialises the whole tree.

### Adding a DSP module

1. New header in `libs/dsp/include/dsp/` following the contract below
2. Add a test in `libs/dsp/tests/` and register it with `add_dsp_test(<name>)`
3. Declare it as a member in a plugin's `PluginProcessor.h`
4. `prepare` in `prepareToPlay`, `reset` in `releaseResources`, `process` in
   `processBlock`

Written once, available to every plugin.

### The module contract

| Method | Thread | Rules |
| --- | --- | --- |
| `prepare(spec)` | message | Before playback. Allocation allowed. |
| `process(block)` | **audio** | No allocation, no locks, no I/O. Ever. |
| `reset()` | either | Clear state without reallocating. |

---

## What this base already gets right

Retrofitting any of these is painful, so they are handled up front:

- **No allocation on the audio thread.** `Biquad` is a plain struct precisely
  because `juce::dsp::IIR`'s coefficient factories allocate.
- **Parameter smoothing.** Raw gain changes on a block boundary click audibly.
- **Denormal protection** via `ScopedNoDenormals`.
- **No string lookups in the audio path** — parameter pointers cached once.
- **Host-visible bypass** through `getBypassParameter()`.
- **Per-channel filter state** — sharing it bleeds channels together.
- **`using juce::AudioProcessor::processBlock`** so the float override does not
  hide the double one.
- **Unique plugin codes**, enforced at configure time.

## Roadmap

1. **Coefficient smoothing** — sweeping EQ frequency currently steps per block.
   Interpolate coefficients, or move to a TPT state-variable filter.
2. **More filter shapes** — shelves and pass filters are more RBJ formulas over
   the same `Biquad`.
3. **Metering** — needs a lock-free audio→UI path (atomics or a FIFO), never a
   mutex.
4. **Delay line** — unlocks delay, chorus, flanger.
5. **Envelope follower → compressor, gate, de-esser.**
6. **Saturation** — waveshaping plus oversampling to control aliasing.
7. **A real UI** — custom `LookAndFeel`, EQ response curve.
8. **`GainStage` / `ToneFilter` tests** — they pull in JUCE, so they sit outside
   the cheap test path. Splitting the maths from the JUCE types would fix that.

---

## Cross-platform

Everything here is verified on Linux only. Before committing to a delivery:

- **macOS** (AU + VST3) and **Windows** (VST3) cannot be built from Linux. You
  need GitHub Actions runners or real machines.
- **macOS distribution** requires Apple code signing and notarization — a paid
  Apple Developer account.
- **Pro Tools** means AAX, which requires an Avid developer account *and* a paid
  PACE/iLok signing certificate. Pro Tools refuses unsigned AAX outright.

## Licence

JUCE is fetched under its own terms — GPLv3 or a commercial/personal JUCE
licence. **If you ship this closed-source, verify the current terms at
juce.com.**

`JUCE_DISPLAY_SPLASH_SCREEN` is deliberately left at its default; disabling the
splash requires an appropriate licence.
