/*
 * Johnamos - order a pizza from the original Xbox.
 * Screens: setup -> stores -> menu -> item -> customise -> deals -> cart -> checkout -> done.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef NATIVE_TEST
#include <hal/debug.h>
#include <hal/video.h>
#include <nxdk/net.h>
#include <windows.h>
#endif

#include "gfx.h"
#include "net.h"
#include "dom.h"
#include "app.h"
#include "music.h"
#include "i18n.h"

#define MARGIN  32
#define RIGHT   (SCREEN_W - 32)
#define LISTW   (RIGHT - MARGIN)
#define TOP     72
#define BOTTOM  414

typedef enum { S_SETUP, S_STORES, S_MENU, S_ITEM, S_CUSTOM, S_DEALS, S_CART, S_CHECKOUT, S_DONE } Screen;

static Screen screen = S_SETUP;
static Input in;
static int setup_from_menu = 0;

/* ================================================================== */
/* small UI pieces                                                    */
/* ================================================================== */

static char toast_msg[120];
static unsigned toast_until;

static void toast(const char *msg)
{
    snprintf(toast_msg, sizeof(toast_msg), "%s", msg);
    toast_until = SDL_GetTicks() + 2600;
}

static void draw_toast(void)
{
    int w;
    if (SDL_GetTicks() > toast_until || !toast_msg[0]) return;
    w = gfx_text_w(F_BODY, toast_msg) + 36;
    if (w > LISTW) w = LISTW;
    gfx_rrect(SCREEN_W / 2 - w / 2, 382, w, 32, 14, COL_ACCENT);
    gfx_text_fit(F_BODY, SCREEN_W / 2 - w / 2 + 18, 386, w - 36, COL_WHITE, toast_msg);
}

/* flat white card with a thin border; selected = light blue with a blue outline */
static void card(int x, int y, int w, int h, int on)
{
    if (on) {
        gfx_rect(x, y, w, h, COL_ACCENT);
        gfx_rect(x + 3, y + 3, w - 6, h - 6, COL_CARD_SEL);
    } else {
        gfx_rect(x, y, w, h, COL_LINE);
        gfx_rect(x + 1, y + 1, w - 2, h - 2, COL_CARD);
    }
}

static void header(const char *title, const char *right)
{
    gfx_clear(COL_BG);          /* a screen may have just come back from a modal box */
    gfx_rect(0, 0, SCREEN_W, 62, COL_BAND);
    gfx_rect(0, 62, SCREEN_W, 3, COL_ACCENT);
    gfx_logo_draw(MARGIN, 9, 0);
    gfx_text(F_BIG, MARGIN + 58, 16, COL_ACCENT, title);
    if (right && right[0]) gfx_text_right(F_BODY, RIGHT, 22, COL_MUTED, right);
}

static void pad_button(int *x, int y, const char *key)
{
    unsigned col = 0;
    if (!strcmp(key, "A")) col = 0x3d9b4b;
    else if (!strcmp(key, "B")) col = 0xd9534f;
    else if (!strcmp(key, "X")) col = 0x428bca;
    else if (!strcmp(key, "Y")) col = 0xe6b422;
    if (col) {
        gfx_circle(*x + 10, y + 11, 10, col);
        gfx_text_center(F_SMALL, *x + 10, y + 2, COL_WHITE, key);
        *x += 26;
    } else {
        int w = gfx_text_w(F_SMALL, key) + 14;
        gfx_rrect(*x, y + 1, w, 21, 8, 0x59606e);
        gfx_text_center(F_SMALL, *x + w / 2, y + 2, COL_WHITE, key);
        *x += w + 5;
    }
}

/* spec: "A:Select|B:Back|LB RB:Category" - button names before the colon, label after */
static void hints(const char *spec)
{
    const char *p = spec;
    int x = MARGIN;
    gfx_rect(0, 420, SCREEN_W, 60, COL_BAND);
    gfx_rect(0, 420, SCREEN_W, 1, COL_LINE);
    while (*p) {
        char item[90], *label, *w;
        size_t n = 0;
        while (p[n] && p[n] != '|' && n < sizeof(item) - 1) { item[n] = p[n]; n++; }
        item[n] = 0;
        p += n;
        if (*p == '|') p++;
        label = strchr(item, ':');
        if (!label) continue;
        *label++ = 0;
        w = item;
        while (*w) {
            char key[16];
            size_t k = 0;
            while (w[k] && w[k] != ' ' && k < sizeof(key) - 1) { key[k] = w[k]; k++; }
            key[k] = 0;
            w += k;
            while (*w == ' ') w++;
            pad_button(&x, 436, key);
        }
        gfx_text(F_SMALL, x + 2, 437, COL_MUTED, label);
        x += gfx_text_w(F_SMALL, label) + 20;
    }
}

/* ---------- loading screen shown while the network works ---------- */

static char busy_msg[100];
static int busy_on;
static size_t busy_bytes;
static unsigned last_input_tick;

static void draw_busy(void)
{
    char dots[8] = "", kb[60] = "";
    int n = (int)((SDL_GetTicks() / 350) % 4), i;
    for (i = 0; i < n; i++) strcat(dots, ".");
    gfx_clear(COL_BG);
    gfx_logo_draw(SCREEN_W / 2 - gfx_logo_size(1) / 2, 100, 1);
    gfx_text_center(F_BIG, SCREEN_W / 2, 250, COL_TEXT, busy_msg);
    gfx_text_center(F_BIG, SCREEN_W / 2, 282, COL_ACCENT, dots);
    if (busy_bytes) {
        snprintf(kb, sizeof(kb), tr("%lu KB received"), (unsigned long)(busy_bytes / 1024));
        gfx_text_center(F_SMALL, SCREEN_W / 2, 330, COL_MUTED, kb);
    }
    gfx_present();
}

static void net_idle(size_t bytes)
{
    busy_bytes = bytes;
    if (busy_on) draw_busy();
    SDL_PumpEvents();
}

static void busy_start(const char *msg)
{
    snprintf(busy_msg, sizeof(busy_msg), "%s", msg);
    busy_on = 1;
    busy_bytes = 0;
    draw_busy();
}

static void busy_end(void) { busy_on = 0; last_input_tick = SDL_GetTicks(); }

/* ---------- frame helpers for modal screens ---------- */

static void frame_begin(void)
{
    input_poll(&in);
    if (in.any) last_input_tick = SDL_GetTicks();
}

static void frame_end(void)
{
    gfx_present();
#ifdef NATIVE_TEST
    SDL_Delay(3);
#else
    Sleep(8);
#endif
}

/* Modal message box. Returns 0 for the first button, 1 for the second (or B). */
static int modal(const char *title, const char *text, const char *first, const char *second, int def)
{
    int sel = def;
    for (;;) {
        int lines, h, y, nb = second ? 2 : 1, i, bw = 200;
        frame_begin();
        if (script_quit_requested()) return 0;
        if (nb == 2 && (in.left || in.right)) sel = !sel;
        if (in.a) return sel;
        if (in.b) return second ? 1 : 0;

        gfx_clear(COL_BG);
        header("Johnamos", NULL);
        lines = 0;
        h = 250;
        y = 100;
        card(64, y, SCREEN_W - 128, h, 0);
        gfx_text(F_BIG, 88, y + 14, COL_ACCENT, title);
        lines = gfx_text_wrap(F_BODY, 88, y + 56, SCREEN_W - 176, 6, COL_TEXT, text);
        (void)lines;
        for (i = 0; i < nb; i++) {
            int bx = SCREEN_W / 2 - (nb * bw + (nb - 1) * 16) / 2 + i * (bw + 16);
            gfx_rrect(bx, y + h - 56, bw, 40, 10, i == sel ? COL_ACCENT : COL_CARD_SEL);
            gfx_text_center(F_BODY, bx + bw / 2, y + h - 50, i == sel ? COL_WHITE : COL_TEXT, i == 0 ? first : second);
        }
        hints(nb == 2 ? "A:Choose|LEFT RIGHT:Move" : "A:OK");
        frame_end();
    }
}

static void message(const char *title, const char *text) { modal(title, text, "OK", NULL, 0); }

/* ================================================================== */
/* on-screen keyboard                                                 */
/* ================================================================== */

static const char *KB_LOWER[4] = { "1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm.-_" };
static const char *KB_UPPER[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM.-_" };
static const char *KB_SYM[4]   = { "!#$%&*()+=", "/?:;,'\"~^", "[]{}<>|\\`-", "_.@#0123-" };
static const char *KB_ACT[6]   = { "Shift", "123 / abc", "Space", ".com", "Delete", "Done" };
static const int KB_ACT_W[6]   = { 84, 104, 118, 66, 84, 80 };
static const int KB_ACT_W_EMAIL[6] = { 76, 96, 58, 130, 84, 80 };

/* Edits buf (max cap-1 chars). Returns 1 if the user pressed Done, 0 if cancelled. */
static int kb_edit(const char *title, char *buf, size_t cap)
{
    char work[128];
    int row = 1, col = 0, layer = 0, shift_once = 0;
    int numeric = !strcmp(title, "Phone") || !strcmp(title, "ZIP / postal code");
    int is_email = !strcmp(title, "Email");
    if (numeric) { row = 0; shift_once = 0; }
    snprintf(work, sizeof(work), "%.*s", (int)(cap < sizeof(work) ? cap - 1 : sizeof(work) - 1), buf);
    if (!work[0] && !numeric && !is_email) shift_once = 1;

    for (;;) {
        const char **rows;
        int len = (int)strlen(work), i, r, x0 = (SCREEN_W - 556) / 2, y0 = 196;
        char inject[128];
        frame_begin();
        if (script_quit_requested()) return 0;

        if (input_take_text(inject, sizeof(inject))) {
            snprintf(work, sizeof(work), "%.*s", (int)(cap - 1), inject);
            len = (int)strlen(work);
        }
        rows = layer == 2 ? KB_SYM : (layer == 1 || shift_once) ? KB_UPPER : KB_LOWER;
        if (layer == 2) rows = KB_SYM;

        if (in.left) { col--; if (col < 0) col = (row == 4 ? 5 : (int)strlen(rows[row]) - 1); }
        if (in.right) { col++; if (col > (row == 4 ? 5 : (int)strlen(rows[row]) - 1)) col = 0; }
        if (in.up) {
            if (row == 4) { row = 3; col = col * (int)strlen(rows[3]) / 6; }
            else if (row > 0) row--;
            else { row = 4; col = col * 6 / 10; }
        }
        if (in.down) {
            if (row == 3) { row = 4; col = col * 6 / (int)strlen(rows[3]); if (col > 5) col = 5; }
            else if (row == 4) row = 0;
            else row++;
        }
        if (col >= (row == 4 ? 6 : (int)strlen(rows[row]))) col = (row == 4 ? 5 : (int)strlen(rows[row]) - 1);
        if (in.b) return 0;
        if (in.start) { snprintf(buf, cap, "%s", work); return 1; }
        if (in.x && len > 0) { work[len - 1] = 0; }
        if (in.y && len + 1 < (int)cap) { work[len] = ' '; work[len + 1] = 0; }
        if (in.l) { layer = layer == 1 ? 0 : 1; shift_once = 0; }
        if (in.r) { layer = layer == 2 ? 0 : 2; shift_once = 0; }
        if (in.a) {
            if (row < 4) {
                if (len + 1 < (int)cap) { work[len] = rows[row][col]; work[len + 1] = 0; }
                shift_once = 0;
                /* capitalise after a space */
            } else if (col == 0) { layer = layer == 1 ? 0 : 1; shift_once = 0; }
            else if (col == 1) { layer = layer == 2 ? 0 : 2; shift_once = 0; }
            else if (col == 2) { if (len + 1 < (int)cap) { work[len] = ' '; work[len + 1] = 0; } }
            else if (col == 3) { const char *sfx = is_email ? "@gmail.com" : ".com"; if (len + (int)strlen(sfx) < (int)cap) strcat(work, sfx); }
            else if (col == 4) { if (len > 0) work[len - 1] = 0; }
            else { snprintf(buf, cap, "%s", work); return 1; }
        }
        len = (int)strlen(work);
        if (len > 0 && work[len - 1] == ' ' && layer == 0) shift_once = 1;

        gfx_clear(COL_BG);
        header("Johnamos", NULL);
        gfx_text(F_BIG, MARGIN, 76, COL_TEXT, title);
        card(MARGIN, 122, LISTW, 46, 0);
        gfx_frame(MARGIN, 122, LISTW, 46, 2, COL_ACCENT);
        {
            int tw = gfx_text_w(F_BIG, work);
            int maxw = LISTW - 90;
            int tx = MARGIN + 14;
            if (tw > maxw) tx -= (tw - maxw);
            /* clip by drawing the text then covering the overflow */
            gfx_text(F_BIG, tx, 126, COL_TEXT, work);
            if (tw > maxw) gfx_rect(MARGIN + 2, 124, 0, 0, COL_CARD);
            if ((SDL_GetTicks() / 450) % 2 == 0) gfx_rect(tx + tw + 2, 132, 3, 28, COL_ACCENT);
        }
        {
            char cnt[24];
            snprintf(cnt, sizeof(cnt), "%d/%d", len, (int)cap - 1);
            gfx_text_right(F_SMALL, RIGHT - 8, 172, COL_MUTED, cnt);
        }
        for (r = 0; r < 4; r++) {
            for (i = 0; i < (int)strlen(rows[r]); i++) {
                int x = x0 + i * 56, y = y0 + r * 44;
                char s[2] = { rows[r][i], 0 };
                gfx_rrect(x, y, 52, 40, 6, (row == r && col == i) ? COL_ACCENT : COL_CARD);
                gfx_text_center(F_BIG, x + 26, y + 5, (row == r && col == i) ? COL_WHITE : COL_TEXT, s);
            }
        }
        {
            int x = x0;
            for (i = 0; i < 6; i++) {
                int y = y0 + 4 * 44;
                int on = (row == 4 && col == i);
                unsigned bg = on ? COL_ACCENT : (i == 5 ? COL_GOOD : COL_CARD_SEL);
                if ((i == 0 && (layer == 1 || shift_once)) || (i == 1 && layer == 2)) bg = on ? COL_ACCENT : COL_ACCENT_DK;
                { const int *wv = is_email ? KB_ACT_W_EMAIL : KB_ACT_W;
                  gfx_rrect(x, y, wv[i], 40, 6, bg);
                  gfx_text_center(F_BODY, x + wv[i] / 2, y + 8, on || i == 5 ? COL_WHITE : COL_TEXT, (i == 3 && is_email) ? "@gmail.com" : KB_ACT[i]);
                  x += wv[i] + 6; }
            }
        }
        hints("A:Type|X:Delete|Y:Space|LB:Shift|RB:123|START:Done|B:Cancel");
        frame_end();
    }
}

/* ================================================================== */
/* product photos                                                     */
/* ================================================================== */

#define IMG_MAX 96
typedef struct { char code[128]; SDL_Texture *tex; int state; unsigned stamp; } ImgEnt;   /* state 1 ok, 2 failed */
static ImgEnt imgs[IMG_MAX];
static int nimgs;
static unsigned img_clock;
static char want_code[128];
static int img_fail_streak;

static ImgEnt *img_find(const char *code)
{
    int i;
    for (i = 0; i < nimgs; i++) if (!strcmp(imgs[i].code, code)) { imgs[i].stamp = ++img_clock; return &imgs[i]; }
    return NULL;
}

static SDL_Texture *img_get(const char *code)
{
    ImgEnt *e = img_find(code);
    if (e) return e->state == 1 ? e->tex : NULL;
    if (!want_code[0] && img_fail_streak < 6) snprintf(want_code, sizeof(want_code), "%s", code);
    return NULL;
}

static int img_base_discovered;

/* Domino's changes the version folder in its image URLs from time to time; read the current one from their order page. */
static void discover_image_base(void)
{
    http_resp r;
    const char *p, *m;
    img_base_discovered = 1;
    if (http_get(dom_host(), dom_order_page(), 700 * 1024, &r) != 0) return;
    /* the page mentions .../cache.dominos.com/<version folder>/assets/build/market/XX/_en/images/... */
    if (r.status == 200 && (p = strstr(r.body, "cache.dominos.com/")) != NULL) {
        p += 17;
        m = strstr(p, "/assets/build/market/");
        if (m && m - p < 60) {
            const char *mk = m + 21, *e = strchr(mk, '/');
            if (e && e - mk < 8 && e[1] == '_') {
                size_t keep = (size_t)(e - p) + 4;              /* through "/_en" or "/_es" */
                snprintf(dom_image_base, sizeof(dom_image_base), "/%.*s/images/img/products/larges", (int)keep - 0, p);
            }
        }
    }
    free(r.body);
}

static int fetch_photo(ImgEnt *e)
{
    char path[300];
    http_resp r;
    int status = 0;
    if (e->code[0] == '/') snprintf(path, sizeof(path), "%s", e->code);      /* address given by the menu */
    else snprintf(path, sizeof(path), "%s/%s.jpg", dom_image_base, e->code);
    if (http_get("cache.dominos.com", path, 600 * 1024, &r) != 0) return 0;
    status = r.status;
    if (r.status == 200 && r.len > 200) {
        e->tex = gfx_texture_from_image(r.body, r.len, 128, 128);
        if (e->tex) e->state = 1;
    }
    free(r.body);
    return status;
}

static void img_load_wanted(void)
{
    ImgEnt *e;
    int i, oldest = 0, status;

    if (!want_code[0]) return;
    if (nimgs < IMG_MAX) e = &imgs[nimgs++];
    else {
        for (i = 1; i < IMG_MAX; i++) if (imgs[i].stamp < imgs[oldest].stamp) oldest = i;
        e = &imgs[oldest];
        gfx_free_texture(e->tex);
    }
    memset(e, 0, sizeof(*e));
    snprintf(e->code, sizeof(e->code), "%s", want_code);
    e->stamp = ++img_clock;
    want_code[0] = 0;
    e->state = 2;
    status = fetch_photo(e);
    if (e->state != 1 && status == 404 && !img_base_discovered && e->code[0] != '/') {
        discover_image_base();
        fetch_photo(e);
    }
    img_fail_streak = e->state == 1 ? 0 : img_fail_streak + 1;
}

static void draw_photo(const char *code, int x, int y, int size)
{
    SDL_Texture *t = img_get(code);
    if (t) {
        gfx_draw_texture(t, x, y, size, size);
    } else {
        gfx_rect(x, y, size, size, 0xdcdcdc);
        if (size >= 100) gfx_logo_draw(x + (size - gfx_logo_size(0)) / 2, y + (size - gfx_logo_size(0)) / 2, 0);
    }
}

/* ================================================================== */
/* shared state                                                       */
/* ================================================================== */

static Store stores[8];
static int nstores, store_sel, need_search = 1;
static Store cur_store;
static int have_store;

static int cat_sel, menu_sel, menu_first;
static int cur_prod = -1;
static VarInfo vars[32];
static int nvars, var_sel, var_first;
static int prod_have_toppings;
static TopInfo tops[64];
static int ntops, chosen_var, cust_qty = 1, cust_sel, cust_first;
typedef struct { int kind, idx; } CEnt;         /* 0 qty, 1 add, 2 heading, 3 topping */
static CEnt cents[140];
static int ncents;
static Quote quote;
static int cart_sel, cart_first;
static int deals_sel, deals_first;
static char done_wait[40];

static void fmt_price(double v, char *out, size_t cap) { money(v, out, cap); }

/* ================================================================== */
/* SETUP                                                              */
/* ================================================================== */

typedef struct { const char *label; char *val; size_t cap; int required; } Field;

static const char *country_name(int k) { return k == 1 ? "Canada" : "United States"; }

static void screen_setup(void)
{
    static int sel;
    Field f[7] = {
        { "Full name",       cfg.name,   sizeof(cfg.name),   1 },
        { "Phone",           cfg.phone,  sizeof(cfg.phone),  1 },
        { "Email",           cfg.email,  sizeof(cfg.email),  1 },
        { "Street address",  cfg.street, sizeof(cfg.street), 1 },
        { "Apt / unit",      cfg.apt,    sizeof(cfg.apt),    0 },
        { "ZIP / postal code", cfg.zip,    sizeof(cfg.zip),    1 },
        { "Delivery notes",  cfg.notes,  sizeof(cfg.notes),  0 },
    };
    int i, rows = 9, y;

    if (in.up) sel = (sel + rows - 1) % rows;
    if (in.down) sel = (sel + 1) % rows;
    if (in.b && setup_from_menu) { screen = have_store ? S_MENU : S_STORES; return; }
    if (sel == 7 && (in.a || in.left || in.right)) {           /* country: cycle */
        int k = dom_country();
        k = k ? 0 : 1;
        cfg.country = k;
        dom_update_region();
    } else if (in.a || in.start) {
        if (sel < 7 && in.a) {
            char title[64];
            /* after Done, carry on into the next field if it is still empty (B or an already filled field stops) */
            for (;;) {
                snprintf(title, sizeof(title), "%s", f[sel].label);
                if (!kb_edit(title, f[sel].val, f[sel].cap)) break;
                dom_update_region();
                if (sel >= 6 || f[sel + 1].val[0]) break;
                sel++;
            }
            dom_update_region();
        } else {
            const char *miss = cfg_missing();
            if (miss[0]) {
                char m[120];
                snprintf(m, sizeof(m), tr("Please fill in: %s"), tr(miss));
                message("Almost there", m);
            } else {
                if (cfg_save() != 0) message("Could not save", "The details could not be written to the Xbox hard drive, but they will be used until you quit.");
                need_search = 1;
                setup_from_menu = 0;
                screen = S_STORES;
                return;
            }
        }
    }

    header("Your details", "Saved on this Xbox only");
    y = TOP + 2;
    for (i = 0; i < 8; i++) {
        int on = sel == i;
        card(MARGIN, y, LISTW, 34, on);
        if (i < 7) {
            gfx_text(F_BODY, MARGIN + 14, y + 5, COL_MUTED, f[i].label);
            if (f[i].val[0]) gfx_text_fit(F_BODY, MARGIN + 190, y + 5, LISTW - 210, COL_TEXT, f[i].val);
            else gfx_text(F_BODY, MARGIN + 190, y + 5, f[i].required ? COL_WARN : 0x9a9a9a, f[i].required ? "(required)" : "(optional)");
        } else {
            gfx_text(F_BODY, MARGIN + 14, y + 5, COL_MUTED, "Country");
            gfx_text(F_BODY, MARGIN + 190, y + 5, COL_TEXT, country_name(dom_country()));
            gfx_text_right(F_BODY, RIGHT - 14, y + 5, COL_ACCENT, "<   >");
        }
        y += 37;
    }
    gfx_rrect(MARGIN, y + 2, LISTW, 38, 10, sel == 8 ? COL_ACCENT : COL_ACCENT_DK);
    gfx_text_center(F_BIG, SCREEN_W / 2, y + 4, COL_WHITE, "Save and find stores");
    hints("A:Edit|UP DOWN:Move|START:Save");
}

/* ================================================================== */
/* LANGUAGE (asked at every start; A confirms the highlighted one)    */
/* ================================================================== */

static void choose_language(void)
{
    int sel = cfg.lang >= 0 && cfg.lang <= 2 ? cfg.lang : 0;
    static const char *names[3] = { "English", "Espa\xc3\xb1ol", "Fran\xc3\xa7" "ais" };
    for (;;) {
        int i, bw = 190, gap = 20, x0;
        frame_begin();
        if (script_quit_requested()) return;
        if (in.left && sel > 0) sel--;
        if (in.right && sel < 2) sel++;
        if (in.a || in.start) break;
        gfx_clear(COL_BG);
        ui_lang = 0;
        header("Johnamos", "");
        gfx_text_center(F_BIG, SCREEN_W / 2, 118, COL_TEXT, "Language / Idioma / Langue");
        x0 = SCREEN_W / 2 - (3 * bw + 2 * gap) / 2;
        for (i = 0; i < 3; i++) {
            int on = sel == i;
            gfx_rrect(x0 + i * (bw + gap), 190, bw, 90, 10, on ? COL_ACCENT : COL_CARD);
            gfx_frame(x0 + i * (bw + gap), 190, bw, 90, 2, on ? COL_ACCENT : COL_LINE);
            gfx_text_center(F_BIG, x0 + i * (bw + gap) + bw / 2, 219, on ? COL_WHITE : COL_TEXT, names[i]);
        }
        hints("A:Select / Elegir / Choisir|LEFT RIGHT:Move / Mover / D\xc3\xa9placer");
        frame_end();
    }
    cfg.lang = sel;
    ui_lang = sel;
    dom_update_region();
}

/* ================================================================== */
/* STORES                                                             */
/* ================================================================== */

static void do_search(void)
{
    int n;
    busy_start("Finding stores near you");
    n = dom_find_stores(stores, 8);
    busy_end();
    need_search = 0;
    if (n < 0) {
        nstores = 0;
        if (modal("Store search failed", dom_err, "Change my details", "Try again", 0) == 0) {
            setup_from_menu = 0;
            screen = S_SETUP;
        } else {
            need_search = 1;
        }
        return;
    }
    nstores = n;
    store_sel = 0;
}

static void enter_store(void)
{
    Store *s = &stores[store_sel];
    busy_start("Loading the menu");
    if (dom_load_menu(s->id) != 0) {
        busy_end();
        message("Menu problem", dom_err);
        return;
    }
    busy_end();
    if (!have_store || strcmp(cur_store.id, s->id) != 0) cart_clear();
    cur_store = *s;
    have_store = 1;
    snprintf(cfg.last_store, sizeof(cfg.last_store), "%s", s->id);
    cfg_save();
    cat_sel = menu_sel = menu_first = 0;
    screen = S_MENU;
}

static void screen_stores(void)
{
    int i, y;
    char sub[80];

    if (need_search) { do_search(); if (screen != S_STORES || need_search) return; }
    if (in.up && nstores) store_sel = (store_sel + nstores - 1) % nstores;
    if (in.down && nstores) store_sel = (store_sel + 1) % nstores;
    if (in.a && nstores) { enter_store(); return; }
    if (in.x) { setup_from_menu = 1; screen = S_SETUP; return; }
    if (in.y) { need_search = 1; return; }

    snprintf(sub, sizeof(sub), "%s, %s", cfg.street, cfg.zip);
    header("Stores near you", sub);
    y = TOP;
    {
        int first = store_sel > 3 ? store_sel - 3 : 0;
        for (i = first; i < nstores && y < BOTTOM - 40; i++) {
            Store *s = &stores[i];
            int on = i == store_sel;
            char wait[40], ph[40];
            card(MARGIN, y, LISTW, 74, on);
            gfx_rrect(MARGIN + 12, y + 12, 68, 24, 10, s->open ? COL_GOOD : COL_BAD);
            gfx_text_center(F_SMALL, MARGIN + 46, y + 15, COL_WHITE, s->open ? "OPEN" : "CLOSED");
            gfx_text_wrap(F_BODY, MARGIN + 96, y + 8, 300, 2, COL_TEXT, s->addr);
            if (s->wait_min) snprintf(wait, sizeof(wait), tr("%d-%d min"), s->wait_min, s->wait_max ? s->wait_max : s->wait_min);
            else snprintf(wait, sizeof(wait), "Delivery");
            gfx_text_right(F_BODY, RIGHT - 14, y + 8, COL_ACCENT, wait);
            snprintf(ph, sizeof(ph), "%s", s->phone);
            gfx_text_right(F_SMALL, RIGHT - 14, y + 34, COL_MUTED, ph);
            if (s->min_order > 0) {
                char mo[40], p[16];
                fmt_price(s->min_order, p, sizeof(p));
                snprintf(mo, sizeof(mo), tr("Minimum order %s"), p);
                gfx_text(F_SMALL, MARGIN + 96, y + 52, COL_MUTED, mo);
            }
            y += 80;
        }
    }
    hints("A:Choose store|X:My details|Y:Search again|LT RT BACK:Quit");
}

/* ================================================================== */
/* MENU                                                               */
/* ================================================================== */

static void open_item(int pi)
{
    cur_prod = pi;
    nvars = prod_variants(pi, vars, 32);
    var_sel = var_first = 0;
    screen = S_ITEM;
}

static void cart_summary(char *out, size_t cap)
{
    char p[16];
    fmt_price(cart_subtotal(), p, sizeof(p));
    if (cart_count() == 0) snprintf(out, cap, "Cart empty");
    else snprintf(out, cap, tr("Cart: %d item%s, %s"), cart_count(), cart_count() == 1 ? "" : "s", p);
}

static void screen_menu(void)
{
    Cat *c;
    int i, y, tx, first_tab, visible = 5;
    char right[60];

    if (menu.ncats == 0) { screen = S_STORES; return; }
    c = &menu.cats[cat_sel];
    if (in.l || in.left) { cat_sel = (cat_sel + menu.ncats - 1) % menu.ncats; menu_sel = menu_first = 0; }
    if (in.r || in.right) { cat_sel = (cat_sel + 1) % menu.ncats; menu_sel = menu_first = 0; }
    c = &menu.cats[cat_sel];
    if (in.up) menu_sel = (menu_sel + c->nprods - 1) % c->nprods;
    if (in.down) menu_sel = (menu_sel + 1) % c->nprods;
    if (menu_sel < menu_first) menu_first = menu_sel;
    if (menu_sel >= menu_first + visible) menu_first = menu_sel - visible + 1;
    if (menu_first > c->nprods - 1) menu_first = 0;
    if (in.a) { open_item(c->prods[menu_sel]); return; }
    if (in.x || in.start) { cart_sel = cart_first = 0; screen = S_CART; return; }
    if (in.y) { deals_sel = deals_first = 0; screen = S_DEALS; return; }
    if (in.b) {
        if (cart.n == 0 || modal("Leave this store?", "Going back to the store list will empty your cart.", "Stay here", "Leave", 0) == 1) {
            if (cart.n) cart_clear();
            have_store = 0;
            screen = S_STORES;
        }
        return;
    }

    cart_summary(right, sizeof(right));
    header("Menu", right);

    /* category tabs */
    first_tab = 0;
    for (;;) {
        int w = 0;
        for (i = first_tab; i <= cat_sel; i++) w += gfx_text_w(F_SMALL, menu.cats[i].name) + 30;
        if (w <= LISTW - 10 || first_tab >= cat_sel) break;
        first_tab++;
    }
    tx = MARGIN;
    for (i = first_tab; i < menu.ncats; i++) {
        int w = gfx_text_w(F_SMALL, menu.cats[i].name) + 24;
        if (tx + w > RIGHT) break;
        gfx_rrect(tx, TOP, w, 28, 12, i == cat_sel ? COL_ACCENT : COL_CARD);
        gfx_text_center(F_SMALL, tx + w / 2, TOP + 4, i == cat_sel ? COL_WHITE : COL_TEXT, menu.cats[i].name);
        tx += w + 6;
    }
    if (first_tab > 0) gfx_text(F_SMALL, 6, TOP + 4, COL_ACCENT, "<");
    if (tx >= RIGHT - 4 || first_tab + 1 < menu.ncats) { if (tx > RIGHT - 30) gfx_text(F_SMALL, RIGHT + 8, TOP + 4, COL_ACCENT, ">"); }

    y = TOP + 36;
    for (i = menu_first; i < c->nprods && i < menu_first + visible; i++) {
        ProdInfo p;
        int on = i == menu_sel;
        char price[32], pr[16];
        prod_info(c->prods[i], &p);
        card(MARGIN, y, LISTW, 58, on);
        draw_photo(p.image, MARGIN + 4, y + 4, 50);
        gfx_text_fit(F_BODY, MARGIN + 64, y + 4, 380, COL_TEXT, p.name);
        gfx_text_fit(F_SMALL, MARGIN + 64, y + 32, 400, COL_MUTED, p.desc[0] ? p.desc : "");
        fmt_price(p.minprice, pr, sizeof(pr));
        if (p.nvariants > 1) snprintf(price, sizeof(price), tr("from %s"), pr);
        else snprintf(price, sizeof(price), "%s", pr);
        gfx_text_right(F_BODY, RIGHT - 12, y + 16, COL_ACCENT, price);
        y += 60;
    }
    if (c->nprods > visible) {
        int bar_h = 300, th = bar_h * visible / c->nprods, ty = TOP + 36 + (bar_h - th) * menu_first / (c->nprods - visible > 0 ? c->nprods - visible : 1);
        gfx_rect(RIGHT + 8, TOP + 36, 3, bar_h, COL_CARD);
        gfx_rect(RIGHT + 8, ty, 3, th, COL_ACCENT);
    }
    hints("A:Open|LB RB:Category|X:Cart|Y:Deals|B:Stores");
}

/* ================================================================== */
/* ITEM (choose size)                                                 */
/* ================================================================== */

static void start_customise(void)
{
    int g, i;
    VarInfo *v = &vars[chosen_var];
    ntops = prod_toppings(cur_prod, v->deftop, tops, 64);
    ncents = 0;
    cents[ncents++] = (CEnt){ 0, 0 };
    cents[ncents++] = (CEnt){ 1, 0 };
    for (g = 0; g < 4; g++) {
        int any = 0;
        for (i = 0; i < ntops; i++) if (tops[i].group == g) any = 1;
        if (!any) continue;
        cents[ncents++] = (CEnt){ 2, g };
        for (i = 0; i < ntops && ncents < 138; i++) if (tops[i].group == g) cents[ncents++] = (CEnt){ 3, i };
    }
    cust_qty = 1;
    cust_sel = 1;
    cust_first = 0;
    screen = S_CUSTOM;
}

static void add_to_cart(int qty)
{
    CartLine l;
    VarInfo *v = &vars[chosen_var];
    char extra[110] = "", removed[110] = "";
    int i;

    memset(&l, 0, sizeof(l));
    snprintf(l.code, sizeof(l.code), "%s", v->code);
    snprintf(l.name, sizeof(l.name), "%s", v->name);
    l.price = v->price;
    l.qty = qty;
    build_options(v->deftop, v->defside, tops, ntops, l.options, sizeof(l.options));
    for (i = 0; i < ntops; i++) {
        if (tops[i].on && !tops[i].was_default) {
            if (strlen(extra) + strlen(tops[i].name) + 4 < sizeof(extra)) { if (extra[0]) strcat(extra, ", "); strcat(extra, tops[i].name); }
        } else if (!tops[i].on && tops[i].was_default) {
            if (strlen(removed) + strlen(tops[i].name) + 4 < sizeof(removed)) { if (removed[0]) strcat(removed, ", "); strcat(removed, tops[i].name); }
        }
    }
    if (extra[0] && removed[0]) snprintf(l.desc, sizeof(l.desc), tr("Add: %.50s  No: %.50s"), extra, removed);
    else if (extra[0]) snprintf(l.desc, sizeof(l.desc), tr("Add: %.100s"), extra);
    else if (removed[0]) snprintf(l.desc, sizeof(l.desc), tr("No: %.100s"), removed);
    else snprintf(l.desc, sizeof(l.desc), "As shown on the menu");
    if (cart_add(&l) != 0) { message("Cart is full", "Remove something from the cart first."); return; }
    toast("Added to your cart");
    screen = S_MENU;
}

static void screen_item(void)
{
    ProdInfo p;
    int i, y, visible = 4;
    char right[60];

    if (in.up && nvars) var_sel = (var_sel + nvars - 1) % nvars;
    if (in.down && nvars) var_sel = (var_sel + 1) % nvars;
    if (var_sel < var_first) var_first = var_sel;
    if (var_sel >= var_first + visible) var_first = var_sel - visible + 1;
    if (in.b) { screen = S_MENU; return; }
    if (in.x || in.start) { cart_sel = cart_first = 0; screen = S_CART; return; }
    if (in.a && nvars) {
        chosen_var = var_sel;
        ntops = prod_toppings(cur_prod, vars[chosen_var].deftop, tops, 64);
        if (ntops > 0) start_customise();
        else add_to_cart(1);
        return;
    }

    prod_info(cur_prod, &p);
    cart_summary(right, sizeof(right));
    header(p.name, "");
    (void)right;
    draw_photo(p.image, MARGIN, TOP + 4, 150);
    gfx_frame(MARGIN - 1, TOP + 3, 152, 152, 1, COL_LINE);
    gfx_text_wrap(F_BODY, MARGIN + 170, TOP + 4, LISTW - 170, 6, COL_MUTED, p.desc[0] ? p.desc : "Choose a size below.");
    gfx_text(F_BODY, MARGIN, TOP + 168, COL_TEXT, "Choose an option");
    y = TOP + 194;
    for (i = var_first; i < nvars && i < var_first + visible; i++) {
        int on = i == var_sel;
        char pr[16];
        card(MARGIN, y, LISTW, 32, on);
        gfx_text_fit(F_BODY, MARGIN + 12, y + 5, LISTW - 110, COL_TEXT, vars[i].name);
        fmt_price(vars[i].price, pr, sizeof(pr));
        gfx_text_right(F_BODY, RIGHT - 12, y + 5, COL_ACCENT, pr);
        y += 34;
    }
    hints("A:Choose|B:Back|X:Cart");
}

/* ================================================================== */
/* CUSTOMISE                                                          */
/* ================================================================== */

static void cust_move(int dir)
{
    int s = cust_sel, n = ncents;
    do { s = (s + dir + n) % n; } while (cents[s].kind == 2);
    cust_sel = s;
}

static void screen_custom(void)
{
    static const char *gname[4] = { "SAUCE", "CHEESE", "MEATS", "VEGGIES AND EXTRAS" };
    int i, y, visible = 8;
    VarInfo *v = &vars[chosen_var];
    char pr[16], line[100];

    if (in.up) cust_move(-1);
    if (in.down) cust_move(1);
    if (cust_sel < cust_first) cust_first = cust_sel;
    if (cust_sel >= cust_first + visible) cust_first = cust_sel - visible + 1;
    if (cust_sel == 0 && cust_first > 0) cust_first = 0;
    if (in.b) { screen = S_ITEM; return; }
    if (cents[cust_sel].kind == 0) {
        if (in.left && cust_qty > 1) cust_qty--;
        if (in.right && cust_qty < 9) cust_qty++;
    }
    if (in.a) {
        CEnt *e = &cents[cust_sel];
        if (e->kind == 3) tops[e->idx].on = !tops[e->idx].on;
        else if (e->kind == 1) { add_to_cart(cust_qty); return; }
        else if (e->kind == 0) cust_qty = cust_qty % 9 + 1;
    }
    if (in.start) { add_to_cart(cust_qty); return; }

    header("Customise", "");
    gfx_text_fit(F_BODY, MARGIN, TOP + 2, LISTW - 90, COL_TEXT, v->name);
    fmt_price(v->price, pr, sizeof(pr));
    gfx_text_right(F_BODY, RIGHT, TOP + 2, COL_ACCENT, pr);
    gfx_text(F_SMALL, MARGIN, TOP + 26, COL_MUTED, "Extra toppings can change the price. You see the final total before ordering.");
    y = TOP + 50;
    for (i = cust_first; i < ncents && i < cust_first + visible; i++) {
        CEnt *e = &cents[i];
        int on = i == cust_sel;
        if (e->kind == 2) {
            gfx_text(F_SMALL, MARGIN + 4, y + 8, COL_ACCENT, gname[e->idx]);
        } else {
            card(MARGIN, y, LISTW, 34, on);
            if (e->kind == 0) {
                snprintf(line, sizeof(line), "Quantity");
                gfx_text(F_BODY, MARGIN + 14, y + 6, COL_TEXT, line);
                snprintf(line, sizeof(line), "<  %d  >", cust_qty);
                gfx_text_right(F_BODY, RIGHT - 14, y + 6, COL_ACCENT, line);
            } else if (e->kind == 1) {
                gfx_rrect(MARGIN + 2, y + 2, LISTW - 4, 30, 8, on ? COL_ACCENT : COL_ACCENT_DK);
                gfx_text_center(F_BODY, SCREEN_W / 2, y + 6, COL_WHITE, "Add to cart");
            } else {
                TopInfo *t = &tops[e->idx];
                gfx_rect(MARGIN + 14, y + 8, 18, 18, on ? COL_TEXT : COL_MUTED);
                gfx_rect(MARGIN + 16, y + 10, 14, 14, t->on ? COL_GOOD : COL_WHITE);
                gfx_text_fit(F_BODY, MARGIN + 44, y + 6, LISTW - 160, COL_TEXT, t->name);
                if (t->was_default) gfx_text_right(F_SMALL, RIGHT - 14, y + 9, COL_MUTED, "included");
            }
        }
        y += 36;
    }
    hints("A:Toggle / Add|LEFT RIGHT:Quantity|START:Add|B:Back");
}

/* ================================================================== */
/* DEALS                                                              */
/* ================================================================== */

typedef struct { int kind, idx; } DEnt;         /* 0 heading (idx: 0 national 1 store), 1 coupon */
static DEnt dents[200];
static int ndents;

static void build_deals(void)
{
    int pass, i;
    ndents = 0;
    for (pass = 0; pass < 2; pass++) {
        int start = ndents, any = 0;
        dents[ndents++] = (DEnt){ 0, pass };
        for (i = 0; i < menu.ncoupons && ndents < 198; i++) {
            CouponInfo c;
            if (!coupon_visible(i)) continue;
            coupon_info(i, &c);
            if ((c.local ? 1 : 0) != pass) continue;
            dents[ndents++] = (DEnt){ 1, i };
            any = 1;
        }
        if (!any) ndents = start;
    }
}

static int same_text(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int coupon_in_cart(const char *code)
{
    int i;
    for (i = 0; i < cart.ncoupons; i++) if (!strcmp(cart.coupons[i].code, code)) return 1;
    return 0;
}

static void deals_move(int dir)
{
    int s = deals_sel, n = ndents, guard = 0;
    if (n == 0) return;
    do { s = (s + dir + n) % n; guard++; } while (dents[s].kind == 0 && guard <= n);
    deals_sel = s;
}

static void screen_deals(void)
{
    int i, y, visible = 5;
    static int built = -1;

    if (built != menu.ncoupons || ndents == 0) {
        build_deals();
        built = menu.ncoupons;
        deals_sel = 0;
        if (ndents) deals_move(1);
        deals_first = 0;
    }
    if (in.up) deals_move(-1);
    if (in.down) deals_move(1);
    if (deals_sel < deals_first) deals_first = deals_sel > 0 && dents[deals_sel - 1].kind == 0 ? deals_sel - 1 : deals_sel;
    if (deals_sel >= deals_first + visible) deals_first = deals_sel - visible + 1;
    if (in.b || in.y) { screen = S_MENU; return; }
    if (in.x || in.start) { cart_sel = cart_first = 0; screen = S_CART; return; }
    if (in.l) {
        char code[24] = "";
        if (kb_edit("Coupon code", code, sizeof(code)) && code[0]) {
            int k, found = -1;
            for (k = 0; k < menu.ncoupons; k++) if (same_text(menu.coupons[k].code, code)) found = k;
            if (found >= 0) { CouponInfo c; coupon_info(found, &c); cart_add_coupon(c.code, c.name); }
            else cart_add_coupon(code, code);
            toast("Code added. You will see if Domino's accepts it at the price check.");
        }
        return;
    }
    if (in.a && ndents && dents[deals_sel].kind == 1) {
        CouponInfo c;
        coupon_info(dents[deals_sel].idx, &c);
        if (coupon_in_cart(c.code)) {
            for (i = 0; i < cart.ncoupons; i++) if (!strcmp(cart.coupons[i].code, c.code)) { cart_remove_coupon(i); break; }
            toast("Deal removed");
        } else if (cart_add_coupon(c.code, c.name) == 0) {
            toast("Deal added. Now add the items it needs to your cart.");
        }
    }

    header("Deals and coupons", cur_store.id[0] ? "Your store's offers" : "");
    if (ndents == 0) {
        gfx_text_center(F_BODY, SCREEN_W / 2, 200, COL_MUTED, "This store has no coupons listed right now.");
    }
    y = TOP;
    for (i = deals_first; i < ndents && y < BOTTOM - 20; i++) {
        DEnt *d = &dents[i];
        if (d->kind == 0) {
            gfx_text(F_SMALL, MARGIN + 4, y + 6, COL_ACCENT, d->idx == 0 ? "NATIONAL DEALS" : "YOUR STORE'S DEALS");
            y += 26;
        } else {
            CouponInfo c;
            int on = i == deals_sel, in_cart;
            char pr[16];
            coupon_info(d->idx, &c);
            in_cart = coupon_in_cart(c.code);
            card(MARGIN, y, LISTW, 68, on);
            gfx_text_fit(F_BODY, MARGIN + 12, y + 5, LISTW - 200, COL_TEXT, c.name);
            gfx_text_wrap(F_SMALL, MARGIN + 12, y + 30, LISTW - 24, 2, COL_MUTED, c.desc);
            if (in_cart) {
                gfx_rrect(RIGHT - 100, y + 6, 90, 22, 10, COL_GOOD);
                gfx_text_center(F_SMALL, RIGHT - 55, y + 8, COL_WHITE, "In your order");
            } else if (c.price > 0) {
                fmt_price(c.price, pr, sizeof(pr));
                gfx_text_right(F_BODY, RIGHT - 12, y + 5, COL_ACCENT, pr);
            }
            y += 72;
        }
    }
    draw_toast();
    hints("A:Add/remove|LB:Enter code|X:Cart|B:Menu");
}

/* ================================================================== */
/* CART                                                               */
/* ================================================================== */

static void screen_cart(void)
{
    int rows = cart.n + cart.ncoupons + 1, i, y, visible = 3;
    char right[60], pr[16];

    if (cart_sel >= rows) cart_sel = rows - 1;
    if (in.up) cart_sel = (cart_sel + rows - 1) % rows;
    if (in.down) cart_sel = (cart_sel + 1) % rows;
    if (cart_sel < cart_first) cart_first = cart_sel;
    if (cart_sel >= cart_first + visible) cart_first = cart_sel - visible + 1;
    if (in.b) { screen = S_MENU; return; }
    if (cart_sel < cart.n) {
        CartLine *l = &cart.lines[cart_sel];
        if (in.left && l->qty > 1) l->qty--;
        if (in.right && l->qty < 9) l->qty++;
        if (in.x) { cart_remove(cart_sel); toast("Removed"); return; }
    } else if (cart_sel < cart.n + cart.ncoupons) {
        if (in.x || in.a) { cart_remove_coupon(cart_sel - cart.n); toast("Deal removed"); return; }
    }
    if (in.y) { deals_sel = deals_first = 0; screen = S_DEALS; return; }
    if ((in.a && cart_sel == rows - 1) || in.start) {
        if (cart.n == 0) { toast("Add something to your cart first"); return; }
        if (!cur_store.open && modal("Store is closed", "This store is not taking delivery orders right now. Try again when it opens, or pick another store.", "OK", "Order anyway", 0) == 0) return;
        busy_start("Checking prices with Domino's");
        if (dom_price(&cur_store, &quote) != 0) {
            busy_end();
            message("Could not price your order", dom_err);
            return;
        }
        busy_end();
        screen = S_CHECKOUT;
        return;
    }

    cart_summary(right, sizeof(right));
    header("Your cart", "");
    y = TOP;
    if (cart.n == 0 && cart.ncoupons == 0) {
        gfx_text_center(F_BODY, SCREEN_W / 2, 150, COL_MUTED, "Your cart is empty. Press B to go back to the menu.");
    }
    for (i = cart_first; i < rows && i < cart_first + visible + 2 && y < 300; i++) {
        int on = i == cart_sel;
        if (i < cart.n) {
            CartLine *l = &cart.lines[i];
            char q[24];
            card(MARGIN, y, LISTW, 62, on);
            gfx_text_fit(F_BODY, MARGIN + 12, y + 5, LISTW - 200, COL_TEXT, l->name);
            gfx_text_fit(F_SMALL, MARGIN + 12, y + 34, LISTW - 200, COL_MUTED, l->desc);
            fmt_price(l->price * l->qty, pr, sizeof(pr));
            gfx_text_right(F_BODY, RIGHT - 12, y + 5, COL_ACCENT, pr);
            snprintf(q, sizeof(q), "<  %d  >", l->qty);
            gfx_text_right(F_BODY, RIGHT - 12, y + 32, on ? COL_TEXT : COL_MUTED, q);
            y += 66;
        } else if (i < cart.n + cart.ncoupons) {
            CartCoupon *c = &cart.coupons[i - cart.n];
            card(MARGIN, y, LISTW, 38, on);
            gfx_rrect(MARGIN + 10, y + 8, 50, 22, 10, COL_GOOD);
            gfx_text_center(F_SMALL, MARGIN + 35, y + 10, COL_WHITE, "DEAL");
            gfx_text_fit(F_BODY, MARGIN + 72, y + 8, LISTW - 90, COL_TEXT, c->name);
            y += 42;
        } else {
            gfx_rrect(MARGIN, y + 4, LISTW, 42, 10, on ? COL_ACCENT : COL_ACCENT_DK);
            gfx_text_center(F_BIG, SCREEN_W / 2, y + 9, COL_WHITE, "Check price and continue");
            y += 52;
        }
    }
    fmt_price(cart_subtotal(), pr, sizeof(pr));
    gfx_rect(MARGIN, 342, LISTW, 1, COL_LINE);
    gfx_text(F_BODY, MARGIN, 350, COL_MUTED, "Menu price before deals, delivery fee and tax");
    gfx_text_right(F_BODY, RIGHT, 350, COL_TEXT, pr);
    draw_toast();
    hints("A:Select|LEFT RIGHT:Quantity|X:Remove|Y:Deals|B:Menu");
}

/* ================================================================== */
/* CHECKOUT                                                           */
/* ================================================================== */

static void screen_checkout(void)
{
    static int sel = 1;
    char pr[16], line[160];
    int y;

    if (in.left || in.right) sel = !sel;
    if (in.b) { screen = S_CART; return; }
    if (in.a) {
        if (sel == 0) { screen = S_CART; return; }
        {
            char text[420], tot[16];
            fmt_price(quote.total, tot, sizeof(tot));
            snprintf(text, sizeof(text), tr("Total %s, paid in cash to the driver. Domino's will email a confirmation to %s."), tot, cfg.email);
            if (modal("Place this order?", text, "Cancel", "Yes, order now", 0) == 1) {
                char msg[300];
                busy_start("Placing your order");
                if (dom_place(&cur_store, &quote, msg, sizeof(msg)) == 0) {
                    busy_end();
                    snprintf(done_wait, sizeof(done_wait), "%s", msg);
                    screen = S_DONE;
                    return;
                }
                busy_end();
                if (!strcmp(msg, "RECAPTCHA")) {
                    /* Domino's asks for a captcha (human check) that a console cannot do: give the order to read out by phone */
                    char items[200] = "", t[500];
                    int k;
                    for (k = 0; k < cart.n; k++) {
                        char one[100];
                        snprintf(one, sizeof(one), "%s%dx %s", k ? ", " : "", cart.lines[k].qty, cart.lines[k].name);
                        if (strlen(items) + strlen(one) < sizeof(items) - 1) strcat(items, one);
                    }
                    snprintf(t, sizeof(t), tr("Domino's now asks for a human check (CAPTCHA) that the Xbox cannot do, so this order was not placed. You can call the store on %s and read them your order: %s"),
                             cur_store.phone[0] ? cur_store.phone : "-", items);
                    message("Order not placed", t);
                } else {
                    message("Order not placed", msg);
                }
            }
        }
        return;
    }

    header("Checkout", dom_is_canada() ? "Pay cash at the door (CAD)" : "Pay cash at the door");
    /* left: where it goes */
    card(MARGIN, TOP, 300, 290, 0);
    gfx_text(F_SMALL, MARGIN + 14, TOP + 10, COL_ACCENT, "DELIVERING TO");
    y = TOP + 34;
    gfx_text_fit(F_BODY, MARGIN + 14, y, 272, COL_TEXT, cfg.name); y += 26;
    snprintf(line, sizeof(line), "%s%s%s", cfg.street, cfg.apt[0] ? " #" : "", cfg.apt);
    gfx_text_fit(F_BODY, MARGIN + 14, y, 272, COL_TEXT, line); y += 26;
    snprintf(line, sizeof(line), "%s%s%s %s", addr.city, addr.city[0] ? ", " : "", addr.region, cfg.zip);
    gfx_text_fit(F_BODY, MARGIN + 14, y, 272, COL_TEXT, line); y += 30;
    gfx_text_fit(F_BODY, MARGIN + 14, y, 272, COL_MUTED, cfg.phone); y += 26;
    gfx_text_fit(F_SMALL, MARGIN + 14, y, 272, COL_MUTED, cfg.email); y += 30;
    if (cfg.notes[0]) { gfx_text_wrap(F_SMALL, MARGIN + 14, y, 272, 2, COL_MUTED, cfg.notes); y += 40; }
    gfx_text(F_SMALL, MARGIN + 14, TOP + 236, COL_ACCENT, "PAYMENT");
    gfx_text(F_BODY, MARGIN + 14, TOP + 256, COL_TEXT, "Cash on delivery");

    /* right: totals */
    card(MARGIN + 316, TOP, LISTW - 316, 290, 0);
    gfx_text(F_SMALL, MARGIN + 330, TOP + 10, COL_ACCENT, "ORDER TOTAL");
    y = TOP + 36;
    fmt_price(quote.food, pr, sizeof(pr));
    gfx_text(F_BODY, MARGIN + 330, y, COL_MUTED, "Food");
    gfx_text_right(F_BODY, RIGHT - 14, y, COL_TEXT, pr); y += 28;
    if (quote.discount > 0.004) {
        fmt_price(-quote.discount, pr, sizeof(pr));
        gfx_text(F_BODY, MARGIN + 330, y, COL_MUTED, "Deals");
        gfx_text_right(F_BODY, RIGHT - 14, y, COL_GOOD, pr); y += 28;
    }
    if (quote.fees > 0.004) {
        fmt_price(quote.fees, pr, sizeof(pr));
        gfx_text(F_BODY, MARGIN + 330, y, COL_MUTED, "Delivery and fees");
        gfx_text_right(F_BODY, RIGHT - 14, y, COL_TEXT, pr); y += 28;
    }
    fmt_price(quote.tax, pr, sizeof(pr));
    gfx_text(F_BODY, MARGIN + 330, y, COL_MUTED, "Tax");
    gfx_text_right(F_BODY, RIGHT - 14, y, COL_TEXT, pr); y += 34;
    gfx_rect(MARGIN + 330, y - 4, LISTW - 344, 1, COL_LINE);
    fmt_price(quote.total, pr, sizeof(pr));
    gfx_text(F_BIG, MARGIN + 330, y + 4, COL_TEXT, dom_is_canada() ? "Total CAD" : "Total");
    gfx_text_right(F_BIG, RIGHT - 14, y + 4, COL_ACCENT, pr);
    if (quote.note[0]) gfx_text_wrap(F_SMALL, MARGIN + 330, y + 44, LISTW - 344, 4, COL_WARN, quote.note);

    {
        int bw = 220, by = TOP + 300;
        gfx_rrect(MARGIN, by, bw, 38, 10, sel == 0 ? COL_ACCENT : COL_CARD_SEL);
        gfx_text_center(F_BODY, MARGIN + bw / 2, by + 7, sel == 0 ? COL_WHITE : COL_TEXT, "Back to cart");
        gfx_rrect(RIGHT - bw, by, bw, 38, 10, sel == 1 ? COL_GOOD : COL_CARD_SEL);
        gfx_text_center(F_BODY, RIGHT - bw / 2, by + 7, sel == 1 ? COL_WHITE : COL_TEXT, "Place order");
    }
    hints("A:Choose|LEFT RIGHT:Move|B:Cart");
}

/* ================================================================== */
/* DONE                                                               */
/* ================================================================== */

static void screen_done(void)
{
    char line[200], pr[16];
    int i;

    if (in.a || in.b || in.start) {
        cart_clear();
        cat_sel = menu_sel = menu_first = 0;
        screen = S_MENU;
        return;
    }
    header("Order placed", "");
    gfx_circle(SCREEN_W / 2, 140, 44, COL_GOOD);
    for (i = 0; i < 14; i++) gfx_rect(SCREEN_W / 2 - 24 + i * 2, 142 + (i < 6 ? i * 3 : (11 - i) * 5 + 3 * 6 - 3 * 6), 8, 8, COL_WHITE);
    gfx_text_center(F_BIG, SCREEN_W / 2, 204, COL_TEXT, "Your pizza is on its way!");
    snprintf(line, sizeof(line), tr("Domino's is emailing a confirmation to %s"), cfg.email);
    gfx_text_wrap(F_BODY, 80, 246, SCREEN_W - 160, 2, COL_MUTED, line);
    if (done_wait[0]) {
        snprintf(line, sizeof(line), tr("Estimated time: %s minutes"), done_wait);
        gfx_text_center(F_BODY, SCREEN_W / 2, 296, COL_TEXT, line);
    }
    fmt_price(quote.total, pr, sizeof(pr));
    snprintf(line, sizeof(line), tr("Have %s in cash ready for the driver"), pr);
    gfx_text_center(F_BODY, SCREEN_W / 2, 326, COL_ACCENT, line);
    hints("A:Start another order");
}

/* ================================================================== */
/* main loop                                                          */
/* ================================================================== */

int app_run(void)
{
    if (gfx_init() != 0) return -1;
    net_idle_cb = net_idle;

    cfg_load();
    choose_language();
    if (cfg.lang >= 0) cfg_save();
    {
        char mp[400];
        gfx_data_path("music.mp3", mp, sizeof(mp));
        music_start(mp, cfg.music < 0 ? 55 : cfg.music);
    }
    busy_start("Starting the network");
#ifndef NATIVE_TEST
    if (nxNetInit(NULL) != 0) {
        busy_end();
        message("No network", "The Xbox could not get a network address. Check the cable and that your router hands out addresses (DHCP).");
    }
#endif
    busy_end();
    if (net_init() != 0) {
        char m[300];
        snprintf(m, sizeof(m), "%s", net_err);
        message("Setup problem", m);
    }
    screen = cfg_complete() ? S_STORES : S_SETUP;
    last_input_tick = SDL_GetTicks();

    for (;;) {
        frame_begin();
        if (script_quit_requested()) break;
        if (in.back) { music_toggle(); toast(music_is_on() ? "Music on" : "Music off"); }
        gfx_clear(COL_BG);
        switch (screen) {
        case S_SETUP:    screen_setup(); break;
        case S_STORES:   screen_stores(); break;
        case S_MENU:     screen_menu(); break;
        case S_ITEM:     screen_item(); break;
        case S_CUSTOM:   screen_custom(); break;
        case S_DEALS:    screen_deals(); break;
        case S_CART:     screen_cart(); break;
        case S_CHECKOUT: screen_checkout(); break;
        case S_DONE:     screen_done(); break;
        }
        if (screen == S_MENU || screen == S_ITEM) draw_toast();
        frame_end();
        /* photos load one at a time while you are not pressing anything */
        if (want_code[0] && (screen == S_MENU || screen == S_ITEM) && SDL_GetTicks() - last_input_tick > 250) {
            img_load_wanted();
            last_input_tick = SDL_GetTicks() - 200;
        }
        if (screen != S_MENU && screen != S_ITEM) want_code[0] = 0;
    }
    music_stop();
    net_close_all();
    gfx_quit();
    return 0;
}
