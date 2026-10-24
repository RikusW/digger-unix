/* Generic sound device header */

typedef unsigned char samp;      /* 8 bit unsigned samples */
#define MIN_SAMP 0
#define MAX_SAMP 0xff

extern samp *buffer;
extern Uint4 firsts,last,size;

/* Number of emulator samples generated per output sample.  The speaker
   emulation is run at a higher rate than the output device to keep the very
   narrow PWM pulses used by the music from aliasing. */
extern Uint4 snd_os;

bool setsounddevice(int base,int irq,int dma,Uint4 samprate,Uint4 bufsize);
bool initsounddevice(void);
void killsounddevice(void);
