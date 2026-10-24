#include <stdlib.h>
#include <math.h>
#include <SDL.h>

#include "def.h"
#include "device.h"
#include "hardware.h"

void fill_audio(void *udata, Uint8 *stream, int len);
samp getsample(void);

samp *buf;
Uint4 bsize;

bool wave_device_available = FALSE;

/* Simulated speaker.

   getsample() (in newsnd.c) emulates the PC speaker at the register level and
   hands back the speaker position averaged over one output sample: 0 means
   the speaker is off, 255 means it is on. Feeding that straight to a sound
   card plays a hard square wave with a large DC offset, which is why it sounds
   much harsher and noisier than the original machine did.

   A real PC speaker cannot reproduce DC (the cone cannot move to a permanent
   offset) and its inertia rolls off the high harmonics. Model that with a DC
   blocking high pass followed by a low pass. The high pass is first order,
   like the speaker's own suspension/AC coupling; the low pass is a second
   order Butterworth section (RBJ cookbook coefficients), giving a 12dB/octave
   slope without the ringing of a sharper filter. */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The lowest notes in the game are below 50Hz (the dying/explosion sweeps go
   down to about 40Hz), so keep the DC blocker well below them. */
#define SPEAKER_HP_HZ 5.0
#define SPEAKER_LP_HZ 5000.0    /* cone inertia kills the buzzy harmonics */
/* A square wave that has had its harmonics removed peaks near 4/pi, not 1, so
   leave enough headroom for that (plus the filter's own overshoot). */
#define SPEAKER_GAIN  0.70

typedef struct
{
	double b0,b1,b2;        /* feed forward coefficients */
	double a1,a2;           /* feedback coefficients (a0 is normalised to 1) */
	double z1,z2;           /* filter state */
} biquad;

static biquad hp_filter,lp_filter;
static bool speaker_filter_ready = FALSE;

static void biquad_reset(biquad *f)
{
	f->z1 = f->z2 = 0.0;
}

/* Transposed direct form II. */
static double biquad_run(biquad *f,double x)
{
	double y;

	y = f->b0 * x + f->z1;
	f->z1 = f->b1 * x - f->a1 * y + f->z2;
	f->z2 = f->b2 * x - f->a2 * y;
	return(y);
}

static void biquad_lowpass(biquad *f,double freq,double rate)
{
	double w0,cosw,alpha,a0;

	w0 = 2.0 * M_PI * freq / rate;
	cosw = cos(w0);
	alpha = sin(w0) / (2.0 * 0.7071067811865476);
	a0 = 1.0 + alpha;
	f->b0 = (1.0 - cosw) * 0.5 / a0;
	f->b1 = (1.0 - cosw) / a0;
	f->b2 = f->b0;
	f->a1 = -2.0 * cosw / a0;
	f->a2 = (1.0 - alpha) / a0;
	biquad_reset(f);
}

/* First order high pass, bilinear transform. */
static void biquad_highpass1(biquad *f,double freq,double rate)
{
	double k,a0;

	k = tan(M_PI * freq / rate);
	a0 = 1.0 + k;
	f->b0 = 1.0 / a0;
	f->b1 = -1.0 / a0;
	f->b2 = 0.0;
	f->a1 = (k - 1.0) / a0;
	f->a2 = 0.0;
	biquad_reset(f);
}

static void speaker_filter_init(Uint4 samprate)
{
	double rate=(double)samprate;
	double lp=SPEAKER_LP_HZ;

	if (rate<1.0)
		return;
	if (lp>rate*0.4)          /* Keep the cutoff safely below Nyquist */
		lp=rate*0.4;
	biquad_highpass1(&hp_filter,SPEAKER_HP_HZ,rate);
	biquad_lowpass(&lp_filter,lp,rate);
	speaker_filter_ready=TRUE;
}

static samp speaker_filter(samp s)
{
	double x,y;

	/* Turn the 0/255 speaker levels into a bipolar signal. The high pass
	   then removes the DC that an AC coupled speaker would not reproduce,
	   so "speaker off" ends up as silence instead of a negative offset. */
	x=((double)s - 127.5) / 127.5;
	x=biquad_run(&hp_filter,x);
	x=biquad_run(&lp_filter,x) * SPEAKER_GAIN;
	y=127.5 + x * 127.5;
	if (y<0.0)
		y=0.0;
	if (y>255.0)
		y=255.0;
	return((samp)y);
}

bool initsounddevice(void)
{
	return(TRUE);
}

bool setsounddevice(int base, int irq, int dma, Uint4 samprate, Uint4 bufsize)
{
	SDL_AudioSpec wanted;
	bool result = FALSE;
	
	wanted.freq = samprate;
	wanted.samples = bufsize;
	wanted.channels = 1;
	wanted.format = AUDIO_U8;
	wanted.userdata = NULL;
	wanted.callback = fill_audio;

#ifdef _VGL
	restorekeyb();
#endif

	if ((SDL_Init(SDL_INIT_AUDIO)) >= 0)
		if ((SDL_OpenAudio(&wanted, NULL)) >= 0)
			result = TRUE;
	if (result == FALSE)
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
	else {
		buf = malloc(bufsize);
		bsize = bufsize;
		speaker_filter_init(samprate);
		wave_device_available = TRUE;
	}

#ifdef _VGL
	initkeyb();
#endif

	return(result);
}

void fill_audio(void *udata, Uint8 *stream, int len)
{
	int i;

	if (len > bsize)
		len = bsize;
	for(i = 0; i<len; i++) {
		samp s = getsample();
		if (speaker_filter_ready)
			s = speaker_filter(s);
		buf[i] = s;
	}

	SDL_MixAudio(stream, buf, len, SDL_MIX_MAXVOLUME);
}


void killsounddevice(void)
{
	SDL_PauseAudio(1);
	biquad_reset(&hp_filter);
	biquad_reset(&lp_filter);
}

