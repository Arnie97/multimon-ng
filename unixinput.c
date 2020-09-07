/*
 *      unixinput.c -- input sound samples
 *
 *      Copyright (C) 1996
 *          Thomas Sailer (sailer@ife.ee.ethz.ch, hb9jnx@hb9w.che.eu)
 *
 *      Copyright (C) 2012-2019
 *          Elias Oenal    (multimon-ng@eliasoenal.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ---------------------------------------------------------------------- */

#include "multimon.h"
#include "mongoose.h"

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _MSC_VER
#include <io.h>
#else
#include <unistd.h>
#endif
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#ifdef SUN_AUDIO
#include <sys/audioio.h>
#include <stropts.h>
#include <sys/conf.h>
#elif PULSE_AUDIO
#include <pulse/simple.h>
#include <pulse/error.h>
#elif WIN32_AUDIO
//see win32_soundin.c
#elif DUMMY_AUDIO
// NO AUDIO FOR OSX :/
#else /* SUN_AUDIO */
#include <sys/soundcard.h>
#include <sys/ioctl.h>
//#include <sys/wait.h>
#endif /* SUN_AUDIO */

#ifndef ONLY_RAW
#include <sys/wait.h>
#endif

/* ---------------------------------------------------------------------- */

static const struct demod_param *dem[] = { ALL_DEMOD };

#define NUMDEMOD (sizeof(dem)/sizeof(dem[0]))

static struct demod_state dem_st[NUMDEMOD];
static unsigned int dem_mask[(NUMDEMOD+31)/32];

#define MASK_SET(n) dem_mask[(n)>>5] |= 1<<((n)&0x1f)
#define MASK_RESET(n) dem_mask[(n)>>5] &= ~(1<<((n)&0x1f))
#define MASK_ISSET(n) (dem_mask[(n)>>5] & 1<<((n)&0x1f))

/* ---------------------------------------------------------------------- */

struct mg_str resp;
static unsigned overlap = 0;
static int sample_rate = -1;
static int verbose_level = 0;
static int repeatable_sox = 0;
static int mute_sox = 0;
static int integer_only = true;
static bool dont_flush = false;
static bool is_startline = true;
static int timestamp = 0;
static char *label = NULL;

extern bool fms_justhex;

extern int pocsag_mode;
extern int pocsag_invert_input;
extern int pocsag_error_correction;
extern int pocsag_show_partial_decodes;
extern int pocsag_heuristic_pruning;
extern int pocsag_prune_empty;
extern bool pocsag_init_charset(char *charset);

extern int aprs_mode;
extern int cw_dit_length;
extern int cw_gap_length;
extern int cw_threshold;
extern bool cw_disable_auto_threshold;
extern bool cw_disable_auto_timing;

void quit(void);
void http_server(void);
void process_buffer(float *float_buf, short *short_buf, unsigned int len);

/***********************************************************************
Copyright (c) 2006-2012, Skype Limited. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, (subject to the limitations in the disclaimer below)
are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
- Neither the name of Skype Limited, nor the names of specific
contributors, may be used to endorse or promote products derived from
this software without specific prior written permission.
NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED
BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/


/*****************************/
/* Silk decoder test program */
/*****************************/

#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE    1
#endif

#include "SKP_Silk_SDK_API.h"
#include "SKP_Silk_SigProc_FIX.h"

/* Define codec specific settings should be moved to h file */
#define MAX_BYTES_PER_FRAME     1024
#define MAX_INPUT_FRAMES        5
#define MAX_FRAME_LENGTH        480
#define FRAME_LENGTH_MS         20
#define MAX_API_FS_KHZ          48
#define MAX_LBRR_DELAY          2

#ifdef _SYSTEM_IS_BIG_ENDIAN
/* Function to convert a little endian int16 to a */
/* big endian int16 or vica verca                 */
void swap_endian(
    SKP_int16       vec[],
    SKP_int         len
)
{
    SKP_int i;
    SKP_int16 tmp;
    SKP_uint8 *p1, *p2;

    for( i = 0; i < len; i++ ){
        tmp = vec[ i ];
        p1 = (SKP_uint8 *)&vec[ i ]; p2 = (SKP_uint8 *)&tmp;
        p1[ 0 ] = p2[ 1 ]; p1[ 1 ] = p2[ 0 ];
    }
}
#endif

#if (defined(_WIN32) || defined(_WINCE))
#include <windows.h>	/* timer */
#else    // Linux or Mac
#include <sys/time.h>
#endif

#ifdef _WIN32

unsigned long GetHighResolutionTime() /* O: time in usec*/
{
    /* Returns a time counter in microsec	*/
    /* the resolution is platform dependent */
    /* but is typically 1.62 us resolution  */
    LARGE_INTEGER lpPerformanceCount;
    LARGE_INTEGER lpFrequency;
    QueryPerformanceCounter(&lpPerformanceCount);
    QueryPerformanceFrequency(&lpFrequency);
    return (unsigned long)((1000000*(lpPerformanceCount.QuadPart)) / lpFrequency.QuadPart);
}
#else    // Linux or Mac
unsigned long GetHighResolutionTime() /* O: time in usec*/
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return((tv.tv_sec*1000000)+(tv.tv_usec));
}
#endif // _WIN32

int decode_silk(const char *bitInFileName)
{
    unsigned long tottime, starttime;
    double    filetime;
    size_t    counter;
    size_t    fbuf_cnt = 0;
    SKP_int32 totPackets, i, k;
    SKP_int16 ret, len, tot_len;
    SKP_int16 nBytes;
    SKP_uint8 payload[    MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES * ( MAX_LBRR_DELAY + 1 ) ];
    SKP_uint8 *payloadEnd = NULL, *payloadToDec = NULL;
    SKP_uint8 FECpayload[ MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES ], *payloadPtr;
    SKP_int16 nBytesFEC;
    SKP_int16 nBytesPerPacket[ MAX_LBRR_DELAY + 1 ], totBytes;
    SKP_int16 out[ ( ( FRAME_LENGTH_MS * MAX_API_FS_KHZ ) << 1 ) * MAX_INPUT_FRAMES ], *outPtr;
    SKP_float buf[ ( ( FRAME_LENGTH_MS * MAX_API_FS_KHZ ) << 2 ) * MAX_INPUT_FRAMES ];
    FILE      *bitInFile;
    SKP_int32 packetSize_ms=0;
    SKP_int32 decSizeBytes;
    void      *psDec;
    SKP_int32 frames, lost, quiet;
    SKP_SILK_SDK_DecControlStruct DecControl;

    /* default settings */
    quiet     = 0;

    /* get arguments */
    if( !quiet ) {
        printf("********** Silk Decoder (Fixed Point) v %s ********************\n", SKP_Silk_SDK_get_version());
        printf("********** Compiled for %d bit cpu *******************************\n", (int)sizeof(void*) * 8 );
        printf( "Input:       %s\n", bitInFileName );
    }

    /* Open files */
    bitInFile = fopen( bitInFileName, "rb" );
    if( bitInFile == NULL ) {
        printf( "Error: could not open input file %s\n", bitInFileName );
        return 404;
    }

    /* Check Silk header */
    {
        const char *header = "\x02#!SILK_V3";
        char header_buf[ 20 ];
        counter = fread( header_buf, sizeof( char ), strlen( header ), bitInFile );
        if( memcmp( header_buf, header, strlen(header) ) != 0 ) {
            printf( "Error: Wrong Header %s\n", header_buf );
            return 400;
        }
    }

    /* Set the samplingrate that is requested for the output */
    DecControl.API_sampleRate = sample_rate;

    /* Initialize to one frame per packet, for proper concealment before first packet arrives */
    DecControl.framesPerPacket = 1;

    /* Create decoder */
    ret = SKP_Silk_SDK_Get_Decoder_Size( &decSizeBytes );
    if( ret ) {
        printf( "\nSKP_Silk_SDK_Get_Decoder_Size returned %d", ret );
    }
    psDec = malloc( decSizeBytes );

    /* Reset decoder */
    ret = SKP_Silk_SDK_InitDecoder( psDec );
    if( ret ) {
        printf( "\nSKP_Silk_InitDecoder returned %d", ret );
    }

    totPackets = 0;
    tottime    = 0;
    payloadEnd = payload;

    /* Simulate the jitter buffer holding MAX_FEC_DELAY packets */
    for( i = 0; i < MAX_LBRR_DELAY; i++ ) {
        /* Read payload size */
        counter = fread( &nBytes, sizeof( SKP_int16 ), 1, bitInFile );
#ifdef _SYSTEM_IS_BIG_ENDIAN
        swap_endian( &nBytes, 1 );
#endif
        /* Read payload */
        counter = fread( payloadEnd, sizeof( SKP_uint8 ), nBytes, bitInFile );

        if( ( SKP_int16 )counter < nBytes ) {
            break;
        }
        nBytesPerPacket[ i ] = nBytes;
        payloadEnd          += nBytes;
        totPackets++;
    }

    while( 1 ) {
        /* Read payload size */
        counter = fread( &nBytes, sizeof( SKP_int16 ), 1, bitInFile );
#ifdef _SYSTEM_IS_BIG_ENDIAN
        swap_endian( &nBytes, 1 );
#endif
        if( nBytes < 0 || counter < 1 ) {
            break;
        }

        /* Read payload */
        counter = fread( payloadEnd, sizeof( SKP_uint8 ), nBytes, bitInFile );
        if( ( SKP_int16 )counter < nBytes ) {
            break;
        }
        nBytesPerPacket[ MAX_LBRR_DELAY ] = nBytes;
        payloadEnd                       += nBytes;
        #include "silk.c"
    }

    /* Empty the recieve buffer */
    for( k = 0; k < MAX_LBRR_DELAY; k++ ) {
        #include "silk.c"
    }

    if( !quiet ) {
        printf( "\nDecoding Finished \n" );
    }

    /* Free decoder */
    free( psDec );

    /* Close files */
    fclose( bitInFile );

    filetime = totPackets * 1e-3 * packetSize_ms;
    if( !quiet ) {
        printf("\nFile length:                 %.3f s", filetime);
        printf("\nTime for decoding:           %.3f s (%.3f%% of realtime)", 1e-6 * tottime, 1e-4 * tottime / filetime);
        printf("\n\n");
    } else {
        /* print time and % of realtime */
        printf( "%.3f %.3f %d\n", 1e-6 * tottime, 1e-4 * tottime / filetime, totPackets );
    }
    return 200;
}
/* ---------------------------------------------------------------------- */

void _verbprintf(int verb_level, const char *fmt, ...)
{
	char time_buf[20];
	time_t t;
	struct tm* tm_info;

    if (verb_level > verbose_level)
        return;
    va_list args;
    va_start(args, fmt);

    if (is_startline)
    {
        if (label != NULL)
            fprintf(stdout, "%s: ", label);

        if (timestamp) {
            t = time(NULL);
            tm_info = localtime(&t);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            fprintf(stdout, "%s: ", time_buf);
        }

        is_startline = false;
    }
    if (NULL != strchr(fmt,'\n')) /* detect end of line in stream */
        is_startline = true;

    vfprintf(stdout, fmt, args);
    if(!dont_flush)
        fflush(stdout);
    va_end(args);
}

/* ---------------------------------------------------------------------- */

void process_buffer(float *float_buf, short *short_buf, unsigned int len)
{
    for (int i = 0; (unsigned int) i <  NUMDEMOD; i++)
        if (MASK_ISSET(i) && dem[i]->demod)
        {
            buffer_t buffer = {short_buf, float_buf};
            dem[i]->demod(dem_st+i, buffer, len);
        }
}

/* ---------------------------------------------------------------------- */
#ifdef SUN_AUDIO

static void input_sound(unsigned int sample_rate, unsigned int overlap,
                        const char *ifname)
{
    audio_info_t audioinfo;
    audio_info_t audioinfo2;
    audio_device_t audiodev;
    int fd;
    short buffer[8192];
    float fbuf[16384];
    unsigned int fbuf_cnt = 0;
    int i;
    short *sp;

    if ((fd = open(ifname ? ifname : "/dev/audio", O_RDONLY)) < 0) {
        perror("open");
        exit (10);
    }
    if (ioctl(fd, AUDIO_GETDEV, &audiodev) == -1) {
        perror("ioctl: AUDIO_GETDEV");
        exit (10);
    }
    AUDIO_INITINFO(&audioinfo);
    audioinfo.record.sample_rate = sample_rate;
    audioinfo.record.channels = 1;
    audioinfo.record.precision = 16;
    audioinfo.record.encoding = AUDIO_ENCODING_LINEAR;
    /*audioinfo.record.gain = 0x20;
      audioinfo.record.port = AUDIO_LINE_IN;
      audioinfo.monitor_gain = 0;*/
    if (ioctl(fd, AUDIO_SETINFO, &audioinfo) == -1) {
        perror("ioctl: AUDIO_SETINFO");
        exit (10);
    }
    if (ioctl(fd, I_FLUSH, FLUSHR) == -1) {
        perror("ioctl: I_FLUSH");
        exit (10);
    }
    if (ioctl(fd, AUDIO_GETINFO, &audioinfo2) == -1) {
        perror("ioctl: AUDIO_GETINFO");
        exit (10);
    }
    fprintf(stdout, "Audio device: name %s, ver %s, config %s, "
            "sampling rate %d\n", audiodev.name, audiodev.version,
            audiodev.config, audioinfo.record.sample_rate);
    for (;;) {
        i = read(fd, sp = buffer, sizeof(buffer));
        if (i < 0 && errno != EAGAIN) {
            perror("read");
            exit(4);
        }
        if (!i)
            break;
        if (i > 0) {
            if(integer_only)
        {
                fbuf_cnt = i/sizeof(buffer[0]);
        }
            else
            {
                for (; i >= sizeof(buffer[0]); i -= sizeof(buffer[0]), sp++)
                    fbuf[fbuf_cnt++] = (*sp) * (1.0/32768.0);
                if (i)
                    fprintf(stderr, "warning: noninteger number of samples read\n");
            }
            if (fbuf_cnt > overlap) {
                process_buffer(fbuf, buffer, fbuf_cnt-overlap);
                memmove(fbuf, fbuf+fbuf_cnt-overlap, overlap*sizeof(fbuf[0]));
                fbuf_cnt = overlap;
            }
        }
    }
    close(fd);
}

#elif DUMMY_AUDIO
static void input_sound(unsigned int sample_rate, unsigned int overlap,
                        const char *ifname)
{
    (void)sample_rate;
    (void)overlap;
    (void)ifname;
}
#elif WIN32_AUDIO
//Implemented in win32_soundin.c
void input_sound(unsigned int sample_rate, unsigned int overlap, const char *ifname);
#elif PULSE_AUDIO
static void input_sound(unsigned int sample_rate, unsigned int overlap,
                        const char *ifname)
{

    short buffer[8192];
    float fbuf[16384];
    unsigned int fbuf_cnt = 0;
    int i;
    int error;
    short *sp;

    (void) ifname;  // Suppress the warning.


    // Init stuff from pa.org
    pa_simple *s;
    pa_sample_spec ss;

    ss.format = PA_SAMPLE_S16NE;
    ss.channels = 1;
    ss.rate = sample_rate;


    /* Create the recording stream */
    if (!(s = pa_simple_new(NULL, "multimon-ng", PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
        fprintf(stderr, "unixinput.c: pa_simple_new() failed: %s\n", pa_strerror(error));
        exit(4);
    }

    for (;;) {
        i = pa_simple_read(s, sp = buffer, sizeof(buffer), &error);
        if (i < 0 && errno != EAGAIN) {
            perror("read");
            fprintf(stderr, "error 1\n");
            exit(4);
        }
        i=sizeof(buffer);
        if (!i)
            break;

        if (i > 0) {
            if(integer_only)
        {
                fbuf_cnt = i/sizeof(buffer[0]);
        }
            else
            {
                for (; (unsigned int) i >= sizeof(buffer[0]); i -= sizeof(buffer[0]), sp++)
                    fbuf[fbuf_cnt++] = (*sp) * (1.0/32768.0);
                if (i)
                    fprintf(stderr, "warning: noninteger number of samples read\n");
            }
            if (fbuf_cnt > overlap) {
                process_buffer(fbuf, buffer, fbuf_cnt-overlap);
                memmove(fbuf, fbuf+fbuf_cnt-overlap, overlap*sizeof(fbuf[0]));
                fbuf_cnt = overlap;
            }
        }
    }
    pa_simple_free(s);
}

#else /* SUN_AUDIO */
/* ---------------------------------------------------------------------- */

static void input_sound(unsigned int sample_rate, unsigned int overlap,
                        const char *ifname)
{
    int sndparam;
    int fd;
    union {
        short s[8192];
        unsigned char b[8192];
    } b;
    float fbuf[16384];
    unsigned int fbuf_cnt = 0;
    int i;
    short *sp;
    unsigned char *bp;
    int fmt = 0;

    if ((fd = open(ifname ? ifname : "/dev/dsp", O_RDONLY)) < 0) {
        perror("open");
        exit (10);
    }
    sndparam = AFMT_S16_NE; /* we want 16 bits/sample signed */
    if (ioctl(fd, SNDCTL_DSP_SETFMT, &sndparam) == -1) {
        perror("ioctl: SNDCTL_DSP_SETFMT");
        exit (10);
    }
    if (sndparam != AFMT_S16_NE) {
        fmt = 1;
        sndparam = AFMT_U8;
        if (ioctl(fd, SNDCTL_DSP_SETFMT, &sndparam) == -1) {
            perror("ioctl: SNDCTL_DSP_SETFMT");
            exit (10);
        }
        if (sndparam != AFMT_U8) {
            perror("ioctl: SNDCTL_DSP_SETFMT");
            exit (10);
        }
    }
    sndparam = 0;   /* we want only 1 channel */
    if (ioctl(fd, SNDCTL_DSP_STEREO, &sndparam) == -1) {
        perror("ioctl: SNDCTL_DSP_STEREO");
        exit (10);
    }
    if (sndparam != 0) {
        fprintf(stderr, "soundif: Error, cannot set the channel "
                "number to 1\n");
        exit (10);
    }
    sndparam = sample_rate;
    if (ioctl(fd, SNDCTL_DSP_SPEED, &sndparam) == -1) {
        perror("ioctl: SNDCTL_DSP_SPEED");
        exit (10);
    }
    if ((10*abs(sndparam-sample_rate)) > sample_rate) {
        perror("ioctl: SNDCTL_DSP_SPEED");
        exit (10);
    }
    if (sndparam != sample_rate) {
        fprintf(stderr, "Warning: Sampling rate is %u, "
                "requested %u\n", sndparam, sample_rate);
    }
#if 0
    sndparam = 4;
    if (ioctl(fd, SOUND_PCM_SUBDIVIDE, &sndparam) == -1) {
        perror("ioctl: SOUND_PCM_SUBDIVIDE");
    }
    if (sndparam != 4) {
        perror("ioctl: SOUND_PCM_SUBDIVIDE");
    }
#endif
    for (;;) {
        if (fmt) {
            perror("ioctl: 8BIT SAMPLES NOT SUPPORTED!");
            exit (10);
            //            i = read(fd, bp = b.b, sizeof(b.b));
            //            if (i < 0 && errno != EAGAIN) {
            //                perror("read");
            //                exit(4);
            //            }
            //            if (!i)
            //                break;
            //            if (i > 0) {
            //                for (; i >= sizeof(b.b[0]); i -= sizeof(b.b[0]), sp++)
            //                    fbuf[fbuf_cnt++] = ((int)(*bp)-0x80) * (1.0/128.0);
            //                if (i)
            //                    fprintf(stderr, "warning: noninteger number of samples read\n");
            //                if (fbuf_cnt > overlap) {
            //                    process_buffer(fbuf, fbuf_cnt-overlap);
            //                    memmove(fbuf, fbuf+fbuf_cnt-overlap, overlap*sizeof(fbuf[0]));
            //                    fbuf_cnt = overlap;
            //                }
            //            }
        } else {
            i = read(fd, sp = b.s, sizeof(b.s));
            if (i < 0 && errno != EAGAIN) {
                perror("read");
                exit(4);
            }
            if (!i)
                break;
            if (i > 0) {
                if(integer_only)
        {
                    fbuf_cnt = i/sizeof(b.s[0]);
        }
                else
                {
                    for (; i >= sizeof(b.s[0]); i -= sizeof(b.s[0]), sp++)
                        fbuf[fbuf_cnt++] = (*sp) * (1.0/32768.0);
                    if (i)
                        fprintf(stderr, "warning: noninteger number of samples read\n");
                }
                if (fbuf_cnt > overlap) {
                    process_buffer(fbuf, b.s, fbuf_cnt-overlap);
                    memmove(fbuf, fbuf+fbuf_cnt-overlap, overlap*sizeof(fbuf[0]));
                    fbuf_cnt = overlap;
                }
            }
        }
    }
    close(fd);
}
#endif /* SUN_AUDIO */

/* ---------------------------------------------------------------------- */

static void input_file(unsigned int sample_rate, unsigned int overlap,
                       const char *fname, const char *type)
{
    struct stat statbuf;
    int pipedes[2];
    int pid = 0, soxstat;
    int fd;
    int i;
    short buffer[8192];
    float fbuf[16384];
    unsigned int fbuf_cnt = 0;
    short *sp;

    /*
     * if the input type is not raw, sox is started to convert the
     * samples to the requested format
     */
    if (!strcmp(fname, "-"))
    {
        // read from stdin and force raw input
        fd = 0;
        type = "raw";
#ifdef WINDOWS
        setmode(fd, O_BINARY);
#endif
    }
    else if (!type || !strcmp(type, "raw")) {
#ifdef WINDOWS
        if ((fd = open(fname, O_RDONLY | O_BINARY)) < 0) {
#else
        if ((fd = open(fname, O_RDONLY)) < 0) {
#endif
            perror("open");
            exit(10);
        }
    }

#ifndef ONLY_RAW
    else {
        if (stat(fname, &statbuf)) {
            perror("stat");
            exit(10);
        }
        if (pipe(pipedes)) {
            perror("pipe");
            exit(10);
        }
        if (!(pid = fork())) {
            char srate[8];
            /*
             * child starts here... first set up filedescriptors,
             * then start sox...
             */
            sprintf(srate, "%d", sample_rate);
            close(pipedes[0]); /* close reading pipe end */
            close(1); /* close standard output */
            if (dup2(pipedes[1], 1) < 0)
                perror("dup2");
            close(pipedes[1]); /* close writing pipe end */
            execlp("sox", "sox", repeatable_sox?"-R":"-V2", mute_sox?"-V1":"-V2",
                   "-t", type, fname,
                   "-t", "raw", "-esigned-integer", "-b16", "-r", srate, "-", "remix", "1",
                   NULL);
            perror("execlp");
            exit(10);
        }
        if (pid < 0) {
            perror("fork");
            exit(10);
        }
        close(pipedes[1]); /* close writing pipe end */
        fd = pipedes[0];
    }
#endif

    /*
     * demodulate
     */
    for (;;) {
        i = read(fd, sp = buffer, sizeof(buffer));
        if (i < 0 && errno != EAGAIN) {
            perror("read");
            exit(4);
        }
        if (!i)
            break;
        if (i > 0) {
            if(integer_only)
            {
                fbuf_cnt = i/sizeof(buffer[0]);
            }
            else
            {
                for (; (unsigned int) i >= sizeof(buffer[0]); i -= sizeof(buffer[0]), sp++)
                    fbuf[fbuf_cnt++] = (*sp) * (1.0f/32768.0f);
                if (i)
                    fprintf(stderr, "warning: noninteger number of samples read\n");
            }
            if (fbuf_cnt > overlap) {
                process_buffer(fbuf, buffer, fbuf_cnt-overlap);
                memmove(fbuf, fbuf+fbuf_cnt-overlap, overlap*sizeof(fbuf[0]));
                fbuf_cnt = overlap;
            }
        }
    }
    close(fd);

#ifndef ONLY_RAW
    waitpid(pid, &soxstat, 0);
#endif
}

void quit(void)
{
    int i = 0;
    for (i = 0; (unsigned int) i < NUMDEMOD; i++)
    {
        if(MASK_ISSET(i))
            if (dem[i]->deinit)
                dem[i]->deinit(dem_st+i);
    }
}

/* ---------------------------------------------------------------------- */

static const char usage_str[] = "\n"
        "Usage: %s [file] [file] [file] ...\n"
        "  If no [file] is given, input will be read from your default sound\n"
        "  hardware. A filename of \"-\" denotes standard input.\n"
        "  -t <type>  : Input file type (any other type than raw requires sox)\n"
        "  -a <demod> : Add demodulator\n"
        "  -s <demod> : Subtract demodulator\n"
        "  -c         : Remove all demodulators (must be added with -a <demod>)\n"
        "  -q         : Quiet\n"
        "  -v <level> : Level of verbosity (e.g. '-v 3')\n"
        "               For POCSAG and MORSE_CW '-v1' prints decoding statistics.\n"
        "  -h         : This help\n"
        "  -A         : APRS mode (TNC2 text output)\n"
        "  -m         : Mute SoX warnings\n"
        "  -r         : Call SoX in repeatable mode (e.g. fixed random seed for dithering)\n"
        "  -n         : Don't flush stdout, increases performance.\n"
        "  -j         : FMS: Just output hex data and CRC, no parsing.\n"
        "  -e         : POCSAG: Hide empty messages.\n"
        "  -u         : POCSAG: Heuristically prune unlikely decodes.\n"
        "  -i         : POCSAG: Inverts the input samples. Try this if decoding fails.\n"
        "  -p         : POCSAG: Show partially received messages.\n"
        "  -f <mode>  : POCSAG: Overrides standards and forces decoding of data as <mode>\n"
        "                       (<mode> can be 'numeric', 'alpha', 'skyper' or 'auto')\n"
        "  -b <level> : POCSAG: BCH bit error correction level. Set 0 to disable, default is 2.\n"
        "                       Lower levels increase performance and lower false positives.\n"
        "  -C <cs>    : POCSAG: Set Charset.\n"
        "  -o         : CW: Set threshold for dit detection (default: 500)\n"
        "  -d         : CW: Dit length in ms (default: 50)\n"
        "  -g         : CW: Gap length in ms (default: 50)\n"
        "  -x         : CW: Disable auto threshold detection\n"
        "  -y         : CW: Disable auto timing detection\n"
        "  --timestamp: Add a time stamp in front of every printed line\n"
        "  --label    : Add a label to the front of every printed line\n"
        "   Raw input requires one channel, 16 bit, signed integer (platform-native)\n"
        "   samples at the demodulator's input sampling rate, which is\n"
        "   usually 22050 Hz. Raw input is assumed and required if piped input is used.\n";

int main(int argc, char *argv[])
{
    int errflg = 0;
    int quietflg = 0;
    int mask_first = 1;

    verbose_level = 3;
    pocsag_show_partial_decodes = 1;
    pocsag_heuristic_pruning = 1;
    pocsag_prune_empty = 1;
    pocsag_mode = POCSAG_MODE_NUMERIC;
    // case 'C': if (!pocsag_init_charset(optarg))
    // case 'i': pocsag_invert_input = true;

    memset(dem_mask, 0, sizeof(dem_mask));
    for (unsigned i = 0; i < NUMDEMOD; i++)
        if (!strcasecmp("CIRFSK", dem[i]->name))
            MASK_SET(i);
    mask_first = 0;

    if ( !quietflg )
    { // pay heed to the quietflg
    fprintf(stderr, "multimon-ng 1.1.8\n"
        "  (C) 1996/1997 by Tom Sailer HB9JNX/AE4WA\n"
        "  (C) 2012-2019 by Elias Oenal\n"
        "Available demodulators:");
    for (unsigned i = 0; i < NUMDEMOD; i++) {
        fprintf(stderr, " %s", dem[i]->name);
    }
    fprintf(stderr, "\n");
    }

    if (errflg) {
        (void)fprintf(stderr, usage_str, argv[0]);
        exit(2);
    }
    if (mask_first)
        memset(dem_mask, 0xff, sizeof(dem_mask));

    if (!quietflg)
        fprintf(stdout, "Enabled demodulators:");
    for (unsigned i = 0; i < NUMDEMOD; i++)
        if (MASK_ISSET(i)) {
            if (!quietflg)
                fprintf(stdout, " %s", dem[i]->name);       //Print demod name
            if(dem[i]->float_samples) integer_only = false; //Enable float samples on demand
            memset(dem_st+i, 0, sizeof(dem_st[i]));
            dem_st[i].dem_par = dem[i];
            if (dem[i]->init)
                dem[i]->init(dem_st+i);
            if (sample_rate == -1)
                sample_rate = dem[i]->samplerate;
            else if ( (unsigned int) sample_rate != dem[i]->samplerate) {
                if (!quietflg)
                    fprintf(stdout, "\n");
                fprintf(stderr, "Error: Current sampling rate %d, "
                        " demodulator \"%s\" requires %d\n",
                        sample_rate, dem[i]->name, dem[i]->samplerate);
                exit(3);
            }
            if (dem[i]->overlap > overlap)
                overlap = dem[i]->overlap;
        }

    printf("\n");
    http_server();
    quit();
    exit(0);
}

/* ---------------------------------------------------------------------- */

static void ev_handler(struct mg_connection *c, int ev, void *p)
{
    if (ev == MG_EV_HTTP_REQUEST) {
        struct http_message *hm = (struct http_message *)p;

        // We have received an HTTP request. Parsed request is contained in `hm`.
        // Send HTTP reply to the client which shows full original request.
        ((char *)hm->body.p)[hm->body.len] = '\0';
        mg_send_head(c, decode_silk(hm->body.p), -1, "Content-Type: application/json");

        const char *p = "";
        if (resp.len)
            p = resp.p;
        printf("HTTP Response: [\r\n%s]\r\n", p);
        mg_printf_http_chunk(c, "[%s]\r\n", p);
        mg_strfree(&resp);
        mg_send_http_chunk(c, "", 0);
    }
}

void http_server() {
    static const char *s_http_port = "7373";
    static struct mg_serve_http_opts s_http_server_opts;
    static const char *err_str;

    struct mg_mgr mgr;
    struct mg_connection *nc;
    struct mg_bind_opts bind_opts;

    mg_mgr_init(&mgr, NULL);
    s_http_server_opts.document_root = ".";

    /* Set HTTP server options */
    memset(&bind_opts, 0, sizeof(bind_opts));
    bind_opts.error_string = &err_str;
    if (!(nc = mg_bind_opt(&mgr, s_http_port, ev_handler, bind_opts))) {
    fprintf(stderr, "Error starting server on port %s: %s\n", s_http_port,
        *bind_opts.error_string);
        exit(1);
    }

    mg_set_protocol_http_websocket(nc);
    s_http_server_opts.enable_directory_listing = "yes";

    printf("Starting RESTful server on port %s, serving %s\n", s_http_port,
        s_http_server_opts.document_root);
    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
}
