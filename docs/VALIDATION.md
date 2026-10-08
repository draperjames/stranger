# Validation — 2026-10-08

## Verified

- macOS optimized native build with C11 warnings treated as errors. On this
  machine the default macOS 27 SDK has a linker/SDK mismatch; selecting
  `/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk` via `-isysroot`
  resolves it without changing the system's developer configuration.
- Full native test harness passed both ordinary and AddressSanitizer +
  UndefinedBehaviorSanitizer builds. Includes 2,000 bounded malformed-state
  parser fuzz cases, clamping, rejection of non-finite values, transactional
  state restoration, 32-instance pool capacity, independence and reuse.
- 44,100 and 48,000 Hz: analytic tremolo reference differs by at most 0.5
  int16 units; chorus delay and secondary tremolo pre-delay timing, dry gain,
  channel isolation and fresh-instance silence pass. Separate checks verify
  output smoothing, mode-crossfade onset and stereo-linked limiting balance.
- All six mode combinations: full-scale DC, Nyquist, sine, noise and impulse
  input; maximum feedback/depth/secondary/output; abrupt parameter/mode
  changes; zero-input feedback decay. No sample exceeds ±16384. Silence after
  five seconds decays to at most one int16 unit.
- Manifest, runtime `chain_params`, `ui_hierarchy`, defaults and eight knob
  bindings agree. Release contains only the `stranger/` tree, required notices,
  and a 64-bit ARM64 ELF with the correct exported v2 symbol.
- Exact ARM64 shared object loads on the connected Move, and the same native
  harness passes there at both sample rates. This test operates on memory
  buffers and never opens an audio device or emits test audio.
- On-device average processing: **114.77 μs / 128 frames at 44.1 kHz**, and
  **117.93 μs at 48 kHz** (3.95% and 4.42% of each full block period).
  These are isolated averages with the existing host running, not guaranteed
  worst-case callback latency or full-chain utilization. Mean test threshold:
  500 μs. Host overhead and other effects consume additional time.
- Installed to `/data/UserData/schwung/modules/audio_fx/stranger` and read back
  the transferred binary's SHA-256:
  `0aca3a62be73f47c09e169b75cff4086674c3feb5153c915e9b38d636efe9fd9`.
- Device host: **Schwung 1.6.3**. No set/chain, track mute, firmware or host
  version was modified. Module selection and listening are left to the user.

## Not yet verified

Human listening, subjective modulation quality, physical Visitor sound
matching, selecting the effect in a live Schwung chain, and an actual 1.7.3
host session. No first-pass guarantee is claimed. The digital ceiling cannot
establish a safe acoustic level at every headphones/speaker volume setting.
