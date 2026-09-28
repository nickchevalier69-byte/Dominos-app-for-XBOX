#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef NATIVE_TEST
#include <hal/debug.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#endif

#include "gfx.h"
#include "i18n.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static TTF_Font *fonts[F_COUNT];
static int font_px[F_COUNT] = { 15, 19, 27, 46 };
static void load_logos(void);
static SDL_Surface *scale_box(SDL_Surface *src, int tw, int th);

/*
 * Video modes. The layout is always 480 logical lines tall:
 *   720p:            1280x720, scale 1.5, layout 854 wide (16:9)
 *   480 widescreen:  640x480 pixels shown stretched on a 16:9 TV, so the layout is 854 wide and
 *                    squeezed to 0.75 horizontally (everything looks the right shape on screen)
 *   480 normal:      640x480, layout 640 wide
 */
int gfx_sw = 640;
static int hd;
static int wide480;
#define SC(v) (hd ? ((v) * 3 + 1) / 2 : (v))                                   /* logical height -> pixels */
#define TX(v) (hd ? ((v) * 3 + 1) / 2 : wide480 ? ((v) * 3 + 2) / 4 : (v))    /* logical width -> pixels */
#define UN(v) (hd ? ((v) * 2 + 1) / 3 : (v))                                   /* real -> logical */

/* ------------------------------------------------------------------ */
/* setup                                                              */
/* ------------------------------------------------------------------ */

void gfx_data_path(const char *name, char *out, size_t cap)
{
#ifdef NATIVE_TEST
    const char *d = getenv("XBOX_D");
    snprintf(out, cap, "%s/%s", d ? d : ".", name);
#else
    snprintf(out, cap, "D:\\%s", name);
#endif
}
static void data_path(const char *name, char *out, size_t cap) { gfx_data_path(name, out, cap); }

int gfx_init(void)
{
    char path[400];
    int i;

#ifdef NATIVE_TEST
    hd = getenv("HD") != NULL;
    wide480 = !hd && getenv("WIDE") != NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return -1;
    window = SDL_CreateWindow("Johnamos", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, hd ? 1280 : 640, hd ? 720 : 480, SDL_WINDOW_SHOWN);
    if (!window) return -1;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
#else
    {
        /* pizza.cfg may say video=480 (force 4:3 480 mode) or widescreen=0/1 (override the dashboard setting) */
        char cp[400], line[128];
        FILE *cf;
        int want720 = 1, wide = (XVideoGetEncoderSettings() & VIDEO_WIDESCREEN) != 0;
        gfx_data_path("pizza.cfg", cp, sizeof(cp));
        cf = fopen(cp, "r");
        if (cf) {
            while (fgets(line, sizeof(line), cf)) {
                if (!strncmp(line, "video=", 6)) want720 = atoi(line + 6) != 480;
                if (!strncmp(line, "widescreen=", 11)) wide = atoi(line + 11) != 0;
            }
            fclose(cf);
        }
        hd = (want720 && XVideoSetMode(1280, 720, 32, REFRESH_DEFAULT)) ? 1 : 0;
        if (!hd) XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
        wide480 = !hd && wide;
    }
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) return -1;
    if (SDL_VideoInit(NULL) != 0) return -1;
    window = SDL_CreateWindow("Johnamos", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, hd ? 1280 : 640, hd ? 720 : 480, SDL_WINDOW_SHOWN);
    if (!window) return -1;
    renderer = SDL_CreateRenderer(window, -1, 0);
#endif
    if (!renderer) return -1;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (wide480) { SDL_RenderSetScale(renderer, 0.75f, 1.0f); gfx_sw = 854; }
    if (hd) { SDL_RenderSetScale(renderer, 1.5f, 1.5f); gfx_sw = 854; }

    if (TTF_Init() != 0) return -2;
    data_path("vegur-regular.ttf", path, sizeof(path));
    for (i = 0; i < F_COUNT; i++) {
        fonts[i] = TTF_OpenFont(path, SC(font_px[i]));
        if (!fonts[i]) return -3;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    load_logos();
    return 0;
}

void gfx_quit(void)
{
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
/* primitives                                                         */
/* ------------------------------------------------------------------ */

static void setcol(unsigned c, int a)
{
    SDL_SetRenderDrawColor(renderer, (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, (Uint8)a);
}

void gfx_clear(unsigned col) { setcol(col, 255); SDL_RenderClear(renderer); }

void gfx_rect(int x, int y, int w, int h, unsigned col)
{
    SDL_Rect r = { x, y, w, h };
    setcol(col, 255);
    SDL_RenderFillRect(renderer, &r);
}

void gfx_rrect(int x, int y, int w, int h, int r, unsigned col)
{
    int i;
    if (r > 3) r = 3;                       /* flat, square-ish look */
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r <= 0) { gfx_rect(x, y, w, h, col); return; }
    gfx_rect(x, y + r, w, h - 2 * r, col);
    for (i = 0; i < r; i++) {
        double dy = r - i - 0.5;
        int inset = r - (int)floor(sqrt((double)r * r - dy * dy) + 0.5);
        gfx_rect(x + inset, y + i, w - 2 * inset, 1, col);
        gfx_rect(x + inset, y + h - 1 - i, w - 2 * inset, 1, col);
    }
}

void gfx_frame(int x, int y, int w, int h, int thick, unsigned col)
{
    gfx_rect(x, y, w, thick, col);
    gfx_rect(x, y + h - thick, w, thick, col);
    gfx_rect(x, y, thick, h, col);
    gfx_rect(x + w - thick, y, thick, h, col);
}

void gfx_circle(int cx, int cy, int r, unsigned col)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int dx = (int)(sqrt((double)r * r - (double)dy * dy) + 0.5);
        gfx_rect(cx - dx, cy + dy, dx * 2 + 1, 1, col);
    }
}

void gfx_present(void) { SDL_RenderPresent(renderer); }

/* ------------------------------------------------------------------ */
/* text, with a small cache of rendered strings                       */
/* ------------------------------------------------------------------ */

#define TC_N 256
typedef struct { char s[100]; int font; unsigned col; SDL_Texture *tex; int w, h; unsigned stamp; } TextEnt;
static TextEnt tc[TC_N];
static unsigned tc_clock;

static TextEnt *text_entry(int font, unsigned col, const char *s)
{
    int i, oldest = 0;
    TextEnt *e;
    SDL_Surface *surf;
    SDL_Color c;

    tc_clock++;
    for (i = 0; i < TC_N; i++) {
        if (tc[i].tex && tc[i].font == font && tc[i].col == col && !strncmp(tc[i].s, s, sizeof(tc[i].s) - 1)) {
            tc[i].stamp = tc_clock;
            return &tc[i];
        }
        if (tc[i].stamp < tc[oldest].stamp || !tc[i].tex) oldest = i;
        if (!tc[i].tex) { oldest = i; break; }
    }
    e = &tc[oldest];
    if (e->tex) { SDL_DestroyTexture(e->tex); e->tex = NULL; }
    c.r = (Uint8)(col >> 16); c.g = (Uint8)(col >> 8); c.b = (Uint8)col; c.a = 255;
    surf = TTF_RenderUTF8_Blended(fonts[font], s, c);
    if (!surf) return NULL;
    e->w = UN(surf->w); e->h = UN(surf->h);
    if (wide480) {                           /* pre-squeeze so text stays smooth under the 0.75 x scale */
        SDL_Surface *cv = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_Surface *sq = cv ? scale_box(cv, TX(surf->w), surf->h) : NULL;
        if (cv) SDL_FreeSurface(cv);
        if (sq) { SDL_FreeSurface(surf); surf = sq; }
    }
    e->tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!e->tex) return NULL;
    SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_BLEND);
    snprintf(e->s, sizeof(e->s), "%s", s);
    e->font = font; e->col = col; e->stamp = tc_clock;
    return e;
}

int gfx_text_w(int font, const char *s)
{
    int w = 0, h = 0;
    if (!s[0]) return 0;
    s = tr(s);
    TTF_SizeUTF8(fonts[font], s, &w, &h);
    return UN(w);
}

int gfx_font_h(int font) { return UN(TTF_FontHeight(fonts[font])); }

void gfx_text(int font, int x, int y, unsigned col, const char *s)
{
    TextEnt *e;
    SDL_Rect dst;
    if (!s || !s[0]) return;
    s = tr(s);
    e = text_entry(font, col, s);
    if (!e) return;
    dst.x = x; dst.y = y; dst.w = e->w; dst.h = e->h;
    SDL_RenderCopy(renderer, e->tex, NULL, &dst);
}

void gfx_text_right(int font, int xr, int y, unsigned col, const char *s)
{
    gfx_text(font, xr - gfx_text_w(font, s), y, col, s);
}

void gfx_text_center(int font, int xc, int y, unsigned col, const char *s)
{
    gfx_text(font, xc - gfx_text_w(font, s) / 2, y, col, s);
}

/* draw on one line, cutting with "..." if wider than maxw */
void gfx_text_fit(int font, int x, int y, int maxw, unsigned col, const char *s)
{
    char buf[200];
    size_t n;
    if (!s || !s[0]) return;
    s = tr(s);
    if (gfx_text_w(font, s) <= maxw) { gfx_text(font, x, y, col, s); return; }
    snprintf(buf, sizeof(buf) - 4, "%s", s);
    n = strlen(buf);
    while (n > 0) {
        n--;
        while (n > 0 && (buf[n] & 0xC0) == 0x80) n--;      /* stay on a UTF-8 boundary */
        buf[n] = 0;
        strcat(buf, "...");
        if (gfx_text_w(font, buf) <= maxw) break;
        buf[n] = 0;
    }
    gfx_text(font, x, y, col, buf);
}

/* word-wrap into at most maxlines lines (last one gets ... if cut); returns lines used */
int gfx_text_wrap(int font, int x, int y, int maxw, int maxlines, unsigned col, const char *s)
{
    char line[200];
    const char *p = s;
    int lines = 0, lh = gfx_font_h(font);
    if (!s || !s[0]) return 0;
    s = tr(s);
    p = s;
    while (*p && lines < maxlines) {
        size_t len = 0, last_space = 0;
        int more;
        while (*p == ' ') p++;
        if (!*p) break;
        line[0] = 0;
        for (;;) {
            size_t wl = 0;
            const char *q = p + len;
            char save;
            while (q[wl] && q[wl] != ' ') wl++;      /* next word */
            if (len + wl >= sizeof(line) - 4) break;
            memcpy(line + len, q, wl);
            save = line[len + wl];
            line[len + wl] = 0;
            if (gfx_text_w(font, line) > maxw && last_space > 0) { line[len + wl] = save; break; }
            last_space = len + wl;
            len += wl;
            if (!q[wl]) break;
            line[len++] = ' ';                        /* keep the space and try the next word */
            if (!p[len]) break;
        }
        while (len > 0 && line[len - 1] == ' ') len--;
        line[len] = 0;
        p += len;
        more = *p != 0;
        while (*p == ' ') p++;
        if (lines == maxlines - 1 && *p) {
            char tmp[210];
            snprintf(tmp, sizeof(tmp), "%s ...", line);
            gfx_text_fit(font, x, y + lines * lh, maxw, col, tmp);
        } else {
            gfx_text_fit(font, x, y + lines * lh, maxw, col, line);
        }
        lines++;
        if (!more) break;
    }
    return lines;
}

/* ------------------------------------------------------------------ */
/* logo (logo_s.png / logo_l.png next to default.xbe)                 */
/* ------------------------------------------------------------------ */

static SDL_Texture *logo_tex[2];
static int logo_px[2] = { 44, 120 };

static void load_logos(void)
{
    static const char *names[2] = { "logo_s.png", "logo_l.png" };
    int i;
    for (i = 0; i < 2; i++) {
        char path[400];
        SDL_Surface *sf, *cv, *sc;
        data_path(names[i], path, sizeof(path));
        sf = IMG_Load(path);
        if (!sf) continue;
        cv = SDL_ConvertSurfaceFormat(sf, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(sf);
        if (!cv) continue;
        /* the PNGs are square and stored large; resize once to the exact pixels needed */
        sc = scale_box(cv, TX(logo_px[i]), SC(logo_px[i]));
        SDL_FreeSurface(cv);
        if (!sc) continue;
        logo_tex[i] = SDL_CreateTextureFromSurface(renderer, sc);
        SDL_FreeSurface(sc);
        if (logo_tex[i]) SDL_SetTextureBlendMode(logo_tex[i], SDL_BLENDMODE_BLEND);
    }
}

int gfx_logo_size(int big) { return logo_px[big ? 1 : 0]; }

void gfx_logo_draw(int x, int y, int big)
{
    SDL_Texture *t = logo_tex[big ? 1 : 0];
    int w = 0, h = 0;
    SDL_Rect d;
    if (!t) return;
    SDL_QueryTexture(t, NULL, NULL, &w, &h);
    d.x = x; d.y = y; d.h = d.w = logo_px[big ? 1 : 0];
    SDL_RenderCopy(renderer, t, NULL, &d);
}

/* ------------------------------------------------------------------ */
/* images                                                             */
/* ------------------------------------------------------------------ */

/* area-average resize of an ARGB8888 surface (smooth, unlike SDL_BlitScaled) */
static SDL_Surface *scale_box(SDL_Surface *src, int tw, int th)
{
    SDL_Surface *dst;
    int x, y;
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    dst = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!dst) return NULL;
    for (y = 0; y < th; y++) {
        int y0 = y * src->h / th, y1 = (y + 1) * src->h / th;
        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < tw; x++) {
            int x0 = x * src->w / tw, x1 = (x + 1) * src->w / tw, xx, yy;
            unsigned a = 0, r = 0, gg = 0, b = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            for (yy = y0; yy < y1 && yy < src->h; yy++) {
                const Uint32 *row = (const Uint32 *)((const Uint8 *)src->pixels + yy * src->pitch);
                for (xx = x0; xx < x1 && xx < src->w; xx++) {
                    Uint32 p = row[xx];
                    unsigned pa = p >> 24;
                    a += pa; r += ((p >> 16) & 255) * pa; gg += ((p >> 8) & 255) * pa; b += (p & 255) * pa; n++;
                }
            }
            if (!n) n = 1;
            if (a) { r /= a; gg /= a; b /= a; }     /* alpha-weighted colour */
            ((Uint32 *)((Uint8 *)dst->pixels + y * dst->pitch))[x] = ((a / n) << 24) | (r << 16) | (gg << 8) | b;
        }
    }
    return dst;
}

SDL_Texture *gfx_texture_from_image(const void *data, size_t len, int w, int h)
{
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)len);
    SDL_Surface *src0, *src, *dst;
    SDL_Texture *t;
    if (!rw) return NULL;
    src0 = IMG_Load_RW(rw, 1);
    if (!src0) return NULL;
    src = SDL_ConvertSurfaceFormat(src0, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(src0);
    if (!src) return NULL;
    dst = scale_box(src, TX(w), SC(h));
    SDL_FreeSurface(src);
    if (!dst) return NULL;
    t = SDL_CreateTextureFromSurface(renderer, dst);
    SDL_FreeSurface(dst);
    return t;
}

SDL_Texture *gfx_texture_raw(const void *data, size_t len)
{
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)len);
    SDL_Surface *s;
    SDL_Texture *t;
    if (!rw) return NULL;
    s = IMG_Load_RW(rw, 1);
    if (!s) return NULL;
    t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    return t;
}

void gfx_draw_texture(SDL_Texture *t, int x, int y, int w, int h)
{
    SDL_Rect d = { x, y, w, h };
    SDL_RenderCopy(renderer, t, NULL, &d);
}

void gfx_free_texture(SDL_Texture *t) { if (t) SDL_DestroyTexture(t); }

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */

#define MAX_PADS 4
static SDL_GameController *pads[MAX_PADS];

#ifdef NATIVE_TEST
static char *script_buf;
static char *script_pos;
static int script_wait;
static int script_done;
static char pending_text[128];
#endif

static int held_prev[12];
static unsigned dir_next[4];

int screenshot(const char *name)
{
#ifdef NATIVE_TEST
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, hd ? 1280 : 640, hd ? 720 : 480, 32, SDL_PIXELFORMAT_ARGB8888);
    char path[300];
    int ok;
    if (!s) return -1;
    { SDL_Rect full = {0, 0, hd ? 1280 : 640, hd ? 720 : 480}; SDL_RenderReadPixels(renderer, &full, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch); }
    snprintf(path, sizeof(path), "%s/%s.bmp", getenv("SHOT_DIR") ? getenv("SHOT_DIR") : ".", name);
    ok = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return ok;
#else
    (void)name;
    return 0;
#endif
}

int input_take_text(char *out, size_t cap)
{
#ifdef NATIVE_TEST
    if (pending_text[0]) {
        snprintf(out, cap, "%s", pending_text);
        pending_text[0] = 0;
        return 1;
    }
#else
    (void)out; (void)cap;
#endif
    return 0;
}

int script_quit_requested(void)
{
#ifdef NATIVE_TEST
    return script_done;
#else
    return 0;
#endif
}

#ifdef NATIVE_TEST
static void script_step(Input *in)
{
    char tok[160];
    size_t n = 0;
    if (!script_buf) {
        const char *s = getenv("PIZZA_SCRIPT");
        if (!s) return;
        script_buf = strdup(s);
        script_pos = script_buf;
    }
    if (script_wait > 0) { script_wait--; return; }
    if (!script_pos || !*script_pos) { script_done = 1; return; }
    while (script_pos[n] && script_pos[n] != ',' && n < sizeof(tok) - 1) { tok[n] = script_pos[n]; n++; }
    tok[n] = 0;
    script_pos += n;
    if (*script_pos == ',') script_pos++;
    if (!strncmp(tok, "wait:", 5)) script_wait = atoi(tok + 5);
    else if (!strncmp(tok, "shot:", 5)) screenshot(tok + 5);
    else if (!strncmp(tok, "type:", 5)) snprintf(pending_text, sizeof(pending_text), "%s", tok + 5);
    else if (!strcmp(tok, "quit")) script_done = 1;
    else if (!strcmp(tok, "up")) in->up = 1;
    else if (!strcmp(tok, "down")) in->down = 1;
    else if (!strcmp(tok, "left")) in->left = 1;
    else if (!strcmp(tok, "right")) in->right = 1;
    else if (!strcmp(tok, "a")) in->a = 1;
    else if (!strcmp(tok, "b")) in->b = 1;
    else if (!strcmp(tok, "x")) in->x = 1;
    else if (!strcmp(tok, "y")) in->y = 1;
    else if (!strcmp(tok, "start")) in->start = 1;
    else if (!strcmp(tok, "back")) in->back = 1;
    else if (!strcmp(tok, "l")) in->l = 1;
    else if (!strcmp(tok, "r")) in->r = 1;
    else if (!strncmp(tok, "wait", 4)) script_wait = 1;
}
#endif

void input_poll(Input *in)
{
    SDL_Event e;
    int held[12] = { 0 };
    int i;
    unsigned now = SDL_GetTicks();

    memset(in, 0, sizeof(*in));
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            for (i = 0; i < MAX_PADS; i++) {
                if (!pads[i]) { pads[i] = SDL_GameControllerOpen(e.cdevice.which); break; }
            }
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            for (i = 0; i < MAX_PADS; i++)
                if (pads[i] && SDL_GameControllerFromInstanceID(e.cdevice.which) == pads[i]) { SDL_GameControllerClose(pads[i]); pads[i] = NULL; }
        }
    }
    SDL_GameControllerUpdate();
    for (i = 0; i < MAX_PADS; i++) {
        SDL_GameController *p = pads[i];
        int lx, ly;
        if (!p) continue;
        held[0] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP);
        held[1] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        held[2] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
        held[3] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        lx = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
        ly = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
        if (ly < -16000) held[0] = 1;
        if (ly > 16000) held[1] = 1;
        if (lx < -16000) held[2] = 1;
        if (lx > 16000) held[3] = 1;
        {
            int rx2 = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTX), ry2 = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTY);
            if (abs(lx) > abs(in->lx)) in->lx = lx;
            if (abs(ly) > abs(in->ly)) in->ly = ly;
            if (abs(rx2) > abs(in->rx)) in->rx = rx2;
            if (abs(ry2) > abs(in->ry)) in->ry = ry2;
        }
        held[4] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A);
        held[5] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B);
        held[6] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X);
        held[7] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_Y);
        held[8] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_START);
        held[9] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK);
        held[10] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        held[11] |= SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
#ifndef NATIVE_TEST
        /* like XBMC: hold both triggers + Back to quit back to the dashboard / XBMC */
        if (SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000 &&
            SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000 &&
            SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK)) {
            XLaunchXBE(NULL);          /* asks the Xbox to run the dashboard; only returns on failure */
            XReboot();
        }
#endif
    }
#ifdef NATIVE_TEST
    {
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        held[0] |= k[SDL_SCANCODE_UP]; held[1] |= k[SDL_SCANCODE_DOWN];
        held[2] |= k[SDL_SCANCODE_LEFT]; held[3] |= k[SDL_SCANCODE_RIGHT];
        held[4] |= k[SDL_SCANCODE_RETURN]; held[5] |= k[SDL_SCANCODE_ESCAPE];
        held[6] |= k[SDL_SCANCODE_X]; held[7] |= k[SDL_SCANCODE_Y];
        held[8] |= k[SDL_SCANCODE_TAB]; held[10] |= k[SDL_SCANCODE_Q]; held[11] |= k[SDL_SCANCODE_E];
    }
#endif

    /* directions repeat while held */
    for (i = 0; i < 4; i++) {
        int fire = 0;
        if (held[i] && !held_prev[i]) { fire = 1; dir_next[i] = now + 380; }
        else if (held[i] && now >= dir_next[i]) { fire = 1; dir_next[i] = now + 85; }
        if (fire) {
            if (i == 0) in->up = 1; else if (i == 1) in->down = 1;
            else if (i == 2) in->left = 1; else in->right = 1;
        }
    }
    if (held[4] && !held_prev[4]) in->a = 1;
    if (held[5] && !held_prev[5]) in->b = 1;
    if (held[6] && !held_prev[6]) in->x = 1;
    if (held[7] && !held_prev[7]) in->y = 1;
    if (held[8] && !held_prev[8]) in->start = 1;
    if (held[9] && !held_prev[9]) in->back = 1;
    if (held[10] && !held_prev[10]) in->l = 1;
    if (held[11] && !held_prev[11]) in->r = 1;
    memcpy(held_prev, held, sizeof(held));
    if (abs(in->lx) < 9000) in->lx = 0;
    if (abs(in->ly) < 9000) in->ly = 0;
    if (abs(in->rx) < 9000) in->rx = 0;
    if (abs(in->ry) < 9000) in->ry = 0;
#ifdef NATIVE_TEST
    script_step(in);
#endif
    in->any = in->up || in->down || in->left || in->right || in->a || in->b || in->x || in->y ||
              in->start || in->back || in->l || in->r;
}
