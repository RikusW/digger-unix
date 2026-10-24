# Worklog

## Add speaker simulation filter to SDL sound output

### User

This is a 1983 game that was reverse engineered to C. It used a PWM sound system but it seems like it was wired directly to audio output and does not sound like the original, there is a lot of noise. A filter simulating a speaker should improved this, add it in sdl_snd.c, this is probably the best place for it to go ?

### Assistant

I'll look at the sound code and how the PWM output is generated.

### Assistant

Now I understand the signal path: `getsample()` in `newsnd.c` emits the raw PC-speaker position (0 = off, 255 = on) averaged per output sample, and `sdl_snd.c:fill_audio` feeds it straight to SDL. That's an un-filtered square wave with DC offset — exactly what a real speaker can't reproduce. I'll add the speaker model at the SDL output stage.

### Assistant

Builds. Now let's design and implement.

### Assistant

Build is clean. Now let me verify the actual filter behavior end-to-end by running the real `sdl_snd.c` code against a generated PC-speaker waveform using SDL's disk audio driver.

### Assistant

The filter math works, but analysis exposed a subtlety: a band-limited square wave peaks at up to ~4/π·(gain) — removing harmonics raises the peak, not lowers it — so the 0.90 gain clipped steadily. Let me set proper headroom.

### Assistant

The 150 Hz test clips because the 2nd-order high-pass sits too close to the game's lowest notes (147 Hz) and droops/rings. A real speaker's AC coupling is first-order anyway, so I'll switch the high-pass to a gentler first-order section and retune.

### Assistant

Tuning analysis shows the 2nd-order high-pass is the wrong model: it differentiates low bass notes (the game sweeps down to ~40 Hz) and forces clipping. A real speaker's AC coupling is first-order, and the game's lowest notes need it to be very low. Let me fix that.

### Assistant

Let me do one final integration test that drives the actual sound engine (`sound.c` + `newsnd.c` + `sdl_snd.c`) rather than a synthetic waveform.

### Assistant

Yes — `sdl_snd.c` is the right place. It's the only SDL-specific stage that sees the raw speaker samples, and it already knows the sample rate, so it can configure the filter per output rate.

## What was wrong

`getsample()` (`newsnd.c`) returns the PC speaker's instantaneous position as 0 (`off`) or 255 (`on`) averaged over one output sample. `fill_audio()` was feeding that straight to SDL, so the sound card played:

- a hard square wave with all its harmonics (the harsh "noise"), and
- a large DC offset — "speaker off" is `0`, which is a negative DC level for 8-bit audio.

A real PC speaker can't reproduce DC and its cone rolls off the high harmonics. That physical stage was missing entirely.

## The filter (`sdl_snd.c:17-130`)

I added a small speaker model in `fill_audio()`:

1. **1st-order high-pass @ 5 Hz** — removes the DC, like the speaker's suspension. It's deliberately very low: the game's dying/explosion sweeps go down to ~40 Hz, and any higher corner either guts them or differentiates them into clipping (a 2nd-order 100 Hz version I tried did exactly that).
2. **2nd-order Butterworth low-pass @ 5 kHz** (RBJ coefficients, clamped to 40% of the sample rate) — the cone-inertia rolloff that removes the buzzy harmonics. 12 dB/oct with no ringing.
3. **0.70 gain + clamp** — a band-limited square peaks near 4/π, not 1.0, so this headroom prevents the filter itself from clipping.

Each biquad uses transposed direct-form II in `double`; state is reset in `killsounddevice()`.

## Tuning

The three knobs are at `sdl_snd.c:36-42`:

```c
#define SPEAKER_HP_HZ 5.0
#define SPEAKER_LP_HZ 5000.0
#define SPEAKER_GAIN  0.70
```

Lower `SPEAKER_LP_HZ` for a duller/softer sound, raise it for more bite. Raising `SPEAKER_GAIN` increases loudness but will start clipping the deepest sweeps and sound onsets.

## Verification

- Clean build with no new warnings; headless run with audio enabled (`Device=1`) works.
- Captured the **real sound engine** through the real `sdl_snd.c` using SDL's disk audio driver at 22050 Hz, filtered vs unfiltered:

| band | unfiltered | filtered |
|---|---|---|
| 1–3 kHz | −0.5 dB | −0.2 dB |
| 5–8 kHz | −11.6 dB | −19.1 dB |
| 8–11 kHz | −18.2 dB | −44.9 dB |

- Steady 1 kHz and 150 Hz test tones come through at the correct pitch with **no clipping** (min/max ≈ 21/254), and silence maps to 128 instead of 0.

One incidental change: `-lm` was added to `Makefile.sdl` (Linux/FreeBSD) and `Makefile.fbsd`, since the filter uses `sin`/`cos`/`tan` at init time. I did not commit anything.

### User

commit it

### Assistant

Committed as `da079ad` on `master`:

```
Add speaker simulation filter to SDL sound output
 3 files changed, 129 insertions(+), 5 deletions(-)
```

Only `sdl_snd.c`, `Makefile.sdl`, and `Makefile.fbsd` went into the commit.

Note: the working tree now shows untracked build artifacts (`.o` files, `digger`) plus `Agents.md`/`.Agents.md.swp` that appeared after my earlier `make clean`. I left them alone and did not include them in the commit.

### User

check Agents.md and update the commit

---

*This commit was amended after the exchange above to include this worklog, as required by `Agents.md`. `da079ad` is the pre-amend hash mentioned in the reply above.*

## Fix music aliasing by oversampling the speaker emulation

### User

The game effects sound much like the original but the music and end of level tune still sounds a bit distorted

### Assistant

The music path uses extremely narrow PWM pulses (often under one output sample). That makes aliasing a prime suspect — unlike the effects' square waves, a narrow pulse has near-flat energy right up to Nyquist. Let me test the same music at 44.1 kHz to see if the inter-harmonic noise disappears.

### Assistant

The PSD comparison confirms it: at 22.05 kHz the native capture has ~5–7 dB more energy above 3 kHz than the same music emulated at 44.1 kHz — that's aliasing from the narrow PWM pulses folding back into the audible band. Let me check your actual config before deciding how to fix it.

### Assistant

The PSD difference is clear: 22.05 kHz has +5–7 dB of high-frequency hash versus the 44.1 kHz reference — aliasing from the sub-sample PWM pulses. Effects use square waves (1/n harmonics) so they're much less affected, which matches exactly what you hear. The fix is to oversample the emulation internally and decimate, so it works even if you launch with `/A:...,22050,...`.

### Assistant

The oversampled emulation now matches the 44.1 kHz reference within 0.6 dB across all bands, versus +3 to +7 dB of aliasing before. Let me smoke-test the game with both `/A` rates and rebuild cleanly.

### Assistant

The `/A` run now crashes with SIGFPE. Let me isolate whether it's the new code or the pre-existing `/A` parsing.

### Assistant

The SIGFPE exposed a pre-existing bug: `/A` parses with `%u` into 16-bit `Uint4` globals, which only worked when the linker placed them in ascending address order. On this build they're descending, so `samprate` ends up 0. Since you're likely launching with `/A`, let me fix that too.

### Assistant

The fix is verified. Let me review the complete diff and do a final clean build/test.

### Assistant

Diagnosis confirmed, and it wasn't the filter itself.

## Root cause

The music uses the PWM/pulse-width path, and its pulses are often **shorter than one output sample**. A narrow pulse has near-flat harmonic energy right up to Nyquist, unlike the effects' square waves (whose harmonics fall off as 1/n). At 22.05 kHz those harmonics fold back into the audible band as noise.

I measured the same music driven by the real engine at 22.05 kHz vs 44.1 kHz (the latter is effectively alias-free). Energy above the 44.1 kHz reference:

| band | 22.05 kHz (before) | after fix |
|---|---|---|
| 3–4 kHz | +3.4 dB | −0.25 dB |
| 4–5 kHz | +5.2 dB | −0.34 dB |
| 5–6 kHz | +6.6 dB | −0.37 dB |
| 6–8 kHz | +7.2 dB | −0.42 dB |
| 8–10 kHz | +7.1 dB | −0.63 dB |

That's the distortion you were hearing — effects were mostly below it, music wasn't.

## Fix

- `newsnd.c` / `device.h`: added `snd_os`, the number of emulator samples per output sample. The emulator now always runs at **at least 44.1 kHz** internally (`snd_os` = 2 at 22.05 kHz output, 1 at 44.1 kHz, more at lower rates), so aliasing folds from a much quieter part of the spectrum.
- `sdl_snd.c`: `fill_audio()` generates `snd_os` samples per output sample through the speaker filter, averages them (decimation), then writes one output sample.
- `main.c`: fixed a pre-existing `/A` crash. It parsed with `%u` straight into 16-bit `Uint4` globals; on this linker layout that zeroed `sound_rate` and caused a divide-by-zero. It now parses into real `unsigned` temps. So `./digger /A:1,220,7,1,22050,512` works again.
- `newsnd.c`: guards against `samprate == 0` instead of dividing by zero.

## Verification

- Clean build, no new warnings.
- Music PSD now matches the 44.1 kHz reference within 0.6 dB across 300 Hz–10 kHz.
- Effect pitch and level are unchanged at 22.05 kHz vs 44.1 kHz (tested with the emerald sound: 690 Hz peak both ways).
- No clipping in music or the end-of-level tune (`min/max` 35/254, zero samples at 0 or 255).
- Headless runs succeed at 8000, 22050, 33000, and 44100 Hz, plus the INI path.

The changes are in the working tree but **not committed**. If it sounds right now, say the word and I'll add this exchange to `Worklog.md` and commit (per `Agents.md`).

### User

commit it
