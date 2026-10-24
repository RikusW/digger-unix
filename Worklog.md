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
