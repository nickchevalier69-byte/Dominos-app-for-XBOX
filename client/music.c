/*
 * Looping MP3 background music.
 * The whole file is kept in memory and decoded by minimp3 (public domain) inside SDL's audio
 * thread, so no disk access happens while playing.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "music.h"

static unsigned char *mp3;
static long mp3_len, mp3_off;
static mp3dec_t dec;
static short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];    /* one decoded frame */
static int pcm_have, pcm_pos;                       /* in 16-bit samples, interleaved stereo */
static int volume = 64;                             /* 0..128 */
static int muted;
static int dev;

/* Decodes the next frame into pcm[] as stereo. Returns 0 if nothing (yet). */
static int next_frame(void)
{
    mp3dec_frame_info_t info;
    int samples, i;

    if (mp3_off >= mp3_len) {                       /* end of the song: start over */
        mp3_off = 0;
        mp3dec_init(&dec);
        return 0;
    }
    samples = mp3dec_decode_frame(&dec, mp3 + mp3_off, (int)(mp3_len - mp3_off), pcm, &info);
    if (info.frame_bytes <= 0) { mp3_off = mp3_len; return 0; }
    mp3_off += info.frame_bytes;
    if (samples <= 0) return 0;
    if (info.channels == 1) {                       /* mono -> stereo, back to front so it can be done in place */
        for (i = samples - 1; i >= 0; i--) { pcm[i * 2] = pcm[i]; pcm[i * 2 + 1] = pcm[i]; }
        samples *= 2;
    } else {
        samples *= info.channels;
    }
    pcm_have = samples;
    pcm_pos = 0;
    return 1;
}

void music_render(unsigned char *stream, int len)
{
    short *out = (short *)stream;
    int want = len / 2, done = 0;
    int guard = 0;

    while (done < want) {
        if (pcm_pos >= pcm_have) {
            if (!next_frame() && ++guard > 4000) break;      /* never spin forever on a broken file */
            continue;
        }
        {
            int n = pcm_have - pcm_pos;
            if (n > want - done) n = want - done;
            if (muted) memset(out + done, 0, (size_t)n * 2);
            else {
                int i;
                for (i = 0; i < n; i++) out[done + i] = (short)((pcm[pcm_pos + i] * volume) >> 7);
            }
            pcm_pos += n;
            done += n;
        }
    }
    if (done < want) memset(out + done, 0, (size_t)(want - done) * 2);
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    music_render(stream, len);
}

int music_start(const char *path, int volume_percent)
{
    FILE *f = fopen(path, "rb");
    SDL_AudioSpec want, got;
    mp3dec_frame_info_t info;
    short probe[MINIMP3_MAX_SAMPLES_PER_FRAME];
    long tries;

    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    mp3_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    mp3 = malloc((size_t)mp3_len);
    if (!mp3 || fread(mp3, 1, (size_t)mp3_len, f) != (size_t)mp3_len) { fclose(f); free(mp3); mp3 = NULL; return -1; }
    fclose(f);

    /* find the sample rate from the first good frame */
    mp3dec_init(&dec);
    memset(&info, 0, sizeof(info));
    for (tries = 0; tries < 64 && info.hz == 0; tries++) {
        int n = mp3dec_decode_frame(&dec, mp3 + mp3_off, (int)(mp3_len - mp3_off), probe, &info);
        if (info.frame_bytes <= 0) break;
        mp3_off += info.frame_bytes;
        (void)n;
    }
    if (info.hz == 0) { free(mp3); mp3 = NULL; return -1; }
    mp3_off = 0;
    mp3dec_init(&dec);
    pcm_have = pcm_pos = 0;

    volume = volume_percent * 128 / 100;
    if (volume > 128) volume = 128;
    if (volume < 0) volume = 0;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -1;
    SDL_zero(want);
    want.freq = info.hz;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = audio_cb;
    dev = (int)SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);   /* SDL converts if the hardware rate differs */
    if (dev == 0) return -1;
    SDL_PauseAudioDevice((SDL_AudioDeviceID)dev, 0);
    return 0;
}

void music_toggle(void) { muted = !muted; }
int music_is_on(void) { return dev != 0 && !muted; }

void music_stop(void)
{
    if (dev) { SDL_CloseAudioDevice((SDL_AudioDeviceID)dev); dev = 0; }
    free(mp3);
    mp3 = NULL;
}
