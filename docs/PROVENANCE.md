# Stranger design and provenance

Research and device inspection: 2026-10-08.

## Integration references

- Current Schwung main inspected at `443466abe3aa87aae7b1e67f22439345102ec42c`; `release.json` advertises 1.7.3. https://github.com/charlesvestal/schwung
- Module requirements: https://github.com/charlesvestal/schwung/blob/443466abe3aa87aae7b1e67f22439345102ec42c/docs/MODULES.md
- Audio effect v2 ABI: instance-based, stereo interleaved signed 16-bit input/output, in-place. Required symbol `move_audio_fx_init_v2`, library `stranger/stranger.so`. Copied headers and license are included.
- TapeDelay integration studied at `aae4c5ef64e0a48150d8ddc76bfc3b07ef358ec9`: https://github.com/charlesvestal/schwung-space-delay . Its DSP is not used.
- Connected Move actually reports Schwung **1.6.3** in `host/version.txt`. No host upgrade was performed. The module only reads the stable `host_api_v1_t` prefix (API version/sample rate), so it does not rely on newer callbacks. Native loading/tests passed on this installation. Current 1.7.3 compatibility follows the unchanged v2 ABI; a live 1.7.3 host session has not been tested.

## Pedal and local code references

- OBNE product description: https://oldbloodnoise.com/pedals/p/visitor-parallel-multi-modulator
- Official manual linked there: https://oldbloodnoise.com/s/Visitor.png
- Supplied local JUCE project is actually `~/Developer/Vistor`, not `~/Developer/Visitor`. Originals are untouched. The README explicitly grants free use. The port uses the LFO, modulation, Hermite interpolation, parallel sum and secondary-knob curves from its processor; no JUCE dependency remains.

The documented pedal has primary tremolo/chorus/phaser and secondary tremolo/chorus in parallel. Secondary controls level, rate, depth and delay together. The primary LFO varies the secondary rate; the combined wet output feeds back into both inputs. The manufacturer does not publish exact oscillator rates, DSP/filter coefficients or control curves. Stranger's 0.1–10 Hz range, 7 ms chorus base, ±4 ms swing, four phaser stages, ±40% secondary rate coupling and ~19.5 ms maximum secondary offset are inherited **approximations**, not measured hardware specifications.

## Deliberate port changes

- Secondary tremolo is delayed as described in the manual (missing from the local processor). True sample delay indexing corrects the original one-sample indexing discrepancy.
- Original `0.5 * (primary + secondary_level * secondary)` summing is retained. With Secondary=0, the wet primary has half gain; wet/dry loudness varies by mode. Do not compensate by increasing the physical volume abruptly.
- Per-instance double precision LFO phase avoids long-term rate drift. Two identical stereo LFOs become shared phases, preserving stereo separation without independent phase drift.
- 15 ms exponential parameter smoothing and three-way mode crossfading. All voice states run continuously so mode changes do not revive old filter state.
- Regen tops out at a 0.75 loop coefficient (local project: 0.85), with a 20 Hz DC blocker and tanh feedback saturation. Driver input is bounded to ±2. Feedback changes the sound without permitting sustained zero-input oscillation in the tested sweeps.
- Output defaults to 50%; Mix to 35%; Regen and Secondary to zero. A 20 ms startup fade, stereo-linked immediate-attack/50 ms-release limiter, and final saturating conversion cap each output sample at ±0.5 (±16384 int16, −6.02 dBFS). This is a digital sample ceiling, not an acoustic hearing-level guarantee or an oversampled true-peak limiter. Keep physical playback volume low for first use.
- Fixed 32-instance pool and validity-tracked 2048-sample delay rings. No heap allocation/free, mutex, logging, disk I/O or full delay-buffer clear in plugin callbacks. Reusing a pool cell cannot replay old delay data. Instance capacity fails cleanly with NULL.
- Strict finite/clamped parameters; bounded transactional flat-JSON state restore; instance-local state, runtime metadata generated from the manifest, and all eight controls exposed to standard Schwung pages.
- 44.1/48 kHz supported; other sample rates fail initialization rather than silently running mis-sized delay lines. Free-running modulation is preserved; this version does not add tempo sync or a physical expression jack.

## Input fingerprints

SHA-256 of the supplied local files used in this port:

- `README.md`: `9c26c3ce04f6d8aa43b3f7fbe7b342248b86a54b5900b4b6e0056ed722ad37fd`

- `Source/PluginProcessor.h`: `f4eeecb395aea4e59720871448a80a2d3d37d1dfe9cd03bf77345eac4047c8bd`

- `Source/PluginProcessor.cpp`: `1e9f1beb90c06b0585c1c6aea367963b5dadcdc7af7b1d61b3a6fea2bd8822ed`
