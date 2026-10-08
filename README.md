# Stranger

A native parallel modulation effect for Schwung on Ableton Move.
Primary tremolo, chorus or phaser runs alongside a secondary tremolo or chorus.
One Secondary control changes the second voice's level, rate, depth and delay;
Regen feeds both voices back into both inputs.

Module ID, C implementation, binary, package and Beads issue prefix all use
**stranger**. Independent project; no OBNE/Ableton affiliation or claim of an
exact pedal emulation.

## First test on Move

1. Turn the Move's physical volume down before listening.
2. Open Schwung's chain editor and add **Stranger** in an **Audio FX** position.
   If it is missing from the list, restart Move to refresh the module scan.
3. Use an audible, unmuted track/chain. Saved Move track mutes can also mute
   Schwung slots on 1.6.3.
4. Start with the defaults: Chorus, Rate 1.5 Hz, Depth 50%, Secondary 0%,
   Regen 0%, Mix 35%, Output 50%. Raise Secondary slowly to hear the second
   modulation. Then try the modes and a small amount of Regen.
5. To return to dry audio, set Mix to zero. Output trim and the sample ceiling
   remain active; remove/bypass the effect for the host's full dry level.

The digital output has a fixed **−6.02 dBFS sample ceiling**, smoothed changes
and protected feedback. Physical listening volume still determines loudness.
At high levels the limiter deliberately reduces peaks; this also changes the
sound compared with the pedal.

## Controls

| Knob | Parameter | Range / behavior |
|---|---|---|
| 1 | Rate | 0.1–10 Hz, primary LFO |
| 2 | Depth | Primary modulation depth |
| 3 | Secondary | Coupled second-voice level, rate, depth, delay |
| 4 | Regen | Cross-feedback, internally bounded and saturated |
| 5 | Mix | Linear dry/wet blend |
| 6 | Primary | Tremolo / Chorus / Phaser |
| 7 | Secondary Mode | Tremolo / Chorus |
| 8 | Output | Trim, default 50%; peak ceiling always applies |

The stereo channels remain separate; no input is collapsed to mono. Save/recall
uses Schwung's normal instance state. LFO phase and delay contents restart on
load. Metadata and on-device help are included; no custom UI is needed.

## Build and validate

```sh
make test           # Desktop DSP, state, safety and metadata checks
make sanitize       # Address/undefined-behavior checks, separate build directory
./scripts/build.sh  # Linux ARM64 build/package via Docker
./scripts/test_move.sh  # Run offline loader/DSP tests ON Move; no audio output
./scripts/install.sh   # Install tested dist/stranger without editing chains
```

Use `MOVE_HOST=your-move-host` to override `move.local`. Requires Python 3,
a C11 compiler, Docker for cross-compilation, and configured Move SSH access.
On macOS a compiler-compatible SDK is needed; see the validation report for
the SDK selection used on this machine. Run `make clean` when changing ordinary
build compiler flags; sanitizer builds have their own folder.

Artifact: `dist/stranger-module.tar.gz`, containing `stranger/stranger.so`,
manifest, help, license and attribution. Install destination:
`/data/UserData/schwung/modules/audio_fx/stranger`. Install verifies SHA-256
and stages files before replacement; previous versions are retained under
`/data/UserData/schwung/tmp/stranger-backup-<timestamp>`. It does not restart
Move or select the effect in a running chain.

Current upstream requirements were inspected against Schwung 1.7.3. Native
load/safety/timing checks were performed on the connected Move running 1.6.3.
See [validation](docs/VALIDATION.md) for the exact boundary: listening and
physical-pedal comparison remain unverified.

## Agent tracking

[Gas Town Beads](https://github.com/gastownhall/beads) tracks work with the
`stranger` prefix and Dolt history under the same GitHub origin's
`refs/dolt/data`, separate from source commits. After cloning, run
`bd bootstrap`, then `bd prime`. Commit/push code with Git; push task history
with `bd dolt push`. No separate Gas Town orchestrator is required.

MIT. See [third-party notices](THIRD_PARTY.md) and [AI assistance](AI_DISCLOSURE.md).
