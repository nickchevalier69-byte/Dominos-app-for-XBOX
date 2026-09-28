#ifndef PIZZA_MUSIC_H
#define PIZZA_MUSIC_H

/* Background music: an MP3 decoded on the fly and looped forever. */
int  music_start(const char *path, int volume_percent);   /* 0 ok, -1 no file / no audio */
void music_toggle(void);                                  /* mute / unmute */
int  music_is_on(void);
void music_stop(void);

/* used by the desktop test: fills len bytes of 16-bit stereo audio */
void music_render(unsigned char *stream, int len);

#endif
