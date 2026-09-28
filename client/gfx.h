#ifndef PIZZA_GFX_H
#define PIZZA_GFX_H

#include <SDL.h>
#include <stddef.h>

extern int gfx_sw;            /* logical width: 640 (4:3) or 854 (16:9 at 720p) */
#define SCREEN_W gfx_sw
#define SCREEN_H 480

/* palette 0xRRGGBB - light, flat, blue/red like the Domino's ordering site */
#define COL_BG        0xeeeeeeu
#define COL_BAND      0xffffffu
#define COL_CARD      0xffffffu
#define COL_CARD_SEL  0xdcebf3u
#define COL_TEXT      0x1f1f1fu
#define COL_MUTED     0x6a6a6au
#define COL_ACCENT    0x006491u
#define COL_ACCENT_DK 0xe31837u
#define COL_GOOD      0x2e8540u
#define COL_BAD       0xc8102eu
#define COL_WARN      0xb35c00u
#define COL_LINE      0xcfcfcfu
#define COL_WHITE     0xffffffu

enum { F_SMALL = 0, F_BODY, F_BIG, F_HUGE, F_COUNT };

typedef struct {
    int up, down, left, right;      /* d-pad / left stick, with key repeat */
    int a, b, x, y, start, back, l, r;   /* pressed this frame */
    int any;                        /* anything happened this frame */
    int lx, ly, rx, ry;             /* analog sticks, -32768..32767 (dead zone applied) */
} Input;

int  gfx_init(void);
void gfx_data_path(const char *name, char *out, size_t cap);   /* D:\\name on the Xbox */
void gfx_quit(void);

void gfx_clear(unsigned col);
void gfx_rect(int x, int y, int w, int h, unsigned col);
void gfx_rrect(int x, int y, int w, int h, int r, unsigned col);
void gfx_frame(int x, int y, int w, int h, int thick, unsigned col);
void gfx_circle(int cx, int cy, int r, unsigned col);
void gfx_present(void);

int  gfx_text_w(int font, const char *s);
int  gfx_font_h(int font);
void gfx_text(int font, int x, int y, unsigned col, const char *s);
void gfx_text_right(int font, int xr, int y, unsigned col, const char *s);
void gfx_text_center(int font, int xc, int y, unsigned col, const char *s);
void gfx_text_fit(int font, int x, int y, int maxw, unsigned col, const char *s);       /* adds ... if too long */
int  gfx_text_wrap(int font, int x, int y, int maxw, int maxlines, unsigned col, const char *s);  /* returns lines used */

void gfx_logo_draw(int x, int y, int big);       /* logo_s.png (44px) or logo_l.png (120px), top-left at x,y */
int  gfx_logo_size(int big);

/* images */
typedef struct SDL_Texture SDL_Texture;
SDL_Texture *gfx_texture_from_image(const void *data, size_t len, int w, int h);
SDL_Texture *gfx_texture_raw(const void *data, size_t len);   /* no pre-scaling, GPU scales it */
void gfx_draw_texture(SDL_Texture *t, int x, int y, int w, int h);
void gfx_free_texture(SDL_Texture *t);

/* input */
void input_poll(Input *in);
int  input_take_text(char *out, size_t cap);    /* text injected by the desktop test script */
int  script_quit_requested(void);
int  screenshot(const char *name);              /* desktop test only */

#endif
