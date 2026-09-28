#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

#include "dom.h"
#include "net.h"

#define API_HOST (dom_host())

Cfg cfg;
Addr addr;
Menu menu;
Cart cart;
char dom_err[300] = "";
#define IMG_BASE_US "/olo/6_92_1/assets/build/market/US/_en/images/img/products/larges"
#define IMG_BASE_CA "/nolo/ca/en/6_88_3/assets/build/market/CA/_en/images/img/products/larges"
char dom_image_base[200] = IMG_BASE_US;
static int image_base_custom;          /* set by image_path= in pizza.cfg */
static int region_ca;

/* ================================================================== */
/* helpers                                                            */
/* ================================================================== */

void money(double v, char *out, size_t cap)
{
    long cents = (long)(v * 100.0 + (v < 0 ? -0.5 : 0.5));
    snprintf(out, cap, "%s$%ld.%02ld", cents < 0 ? "-" : "", labs(cents) / 100, labs(cents) % 100);
}

void url_encode(const char *s, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *s && o + 4 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}

/* growable string builder */
typedef struct { char *p; size_t len, cap; } SB;

static void sb_init(SB *b) { b->cap = 4096; b->p = malloc(b->cap); b->len = 0; if (b->p) b->p[0] = 0; }

static void sb_add(SB *b, const char *s)
{
    size_t n = strlen(s);
    if (!b->p) return;
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->p = realloc(b->p, b->cap);
        if (!b->p) return;
    }
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}

static void sb_fmt(SB *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void sb_fmt(SB *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_add(b, tmp);
}

/* adds "text" as a JSON string literal */
static void sb_jstr(SB *b, const char *s)
{
    char tmp[8];
    sb_add(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { tmp[0] = '\\'; tmp[1] = (char)c; tmp[2] = 0; sb_add(b, tmp); }
        else if (c < 0x20) { snprintf(tmp, sizeof(tmp), "\\u%04x", c); sb_add(b, tmp); }
        else { tmp[0] = (char)c; tmp[1] = 0; sb_add(b, tmp); }
    }
    sb_add(b, "\"");
}

static void trim(char *s)
{
    size_t n = strlen(s), st = 0;
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[st] && isspace((unsigned char)s[st])) st++;
    if (st) memmove(s, s + st, strlen(s + st) + 1);
}

/* ================================================================== */
/* region: a Canadian postal code starts with a letter (M5V 2T6), a US ZIP with a digit */
/* ================================================================== */

void dom_update_region(void)
{
    int k = cfg.country >= 0 ? cfg.country : (isalpha((unsigned char)cfg.zip[0]) ? 1 : 0);
    const char *mk;
    region_ca = k == 1;
    mk = region_ca ? "CANADA" : "UNITED_STATES";
    snprintf(net_market, sizeof(net_market), "%s", cfg.market[0] ? cfg.market : mk);
    snprintf(net_lang, sizeof(net_lang), "%s", cfg.lang == 2 ? "fr" : cfg.lang == 2 ? "fr" : cfg.lang == 1 ? "es" : "en");
    if (!image_base_custom)
        snprintf(dom_image_base, sizeof(dom_image_base), "%s", region_ca ? IMG_BASE_CA : IMG_BASE_US);
}

int dom_is_canada(void) { return region_ca; }
int dom_country(void) { return region_ca ? 1 : 0; }
const char *dom_host(void)
{
    if (cfg.host[0]) return cfg.host;
    return region_ca ? "order.dominos.ca" : "order.dominos.com";
}
const char *dom_currency(void) { return region_ca ? "CAD" : "USD"; }
const char *dom_order_page(void) { return "/en/pages/order/"; }

/* ================================================================== */
/* saved settings                                                     */
/* ================================================================== */

int cfg_load(void)
{
    char line[300];
    FILE *f = fopen("D:\\pizza.cfg", "r");
    memset(&cfg, 0, sizeof(cfg));
    cfg.music = -1;
    cfg.country = -1;
    cfg.lang = -1;
    if (!f) { dom_update_region(); return -1; }
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq++ = 0;
        trim(eq);
        if (!strcmp(line, "name")) snprintf(cfg.name, sizeof(cfg.name), "%s", eq);
        else if (!strcmp(line, "phone")) snprintf(cfg.phone, sizeof(cfg.phone), "%s", eq);
        else if (!strcmp(line, "email")) snprintf(cfg.email, sizeof(cfg.email), "%s", eq);
        else if (!strcmp(line, "street")) snprintf(cfg.street, sizeof(cfg.street), "%s", eq);
        else if (!strcmp(line, "apt")) snprintf(cfg.apt, sizeof(cfg.apt), "%s", eq);
        else if (!strcmp(line, "zip")) snprintf(cfg.zip, sizeof(cfg.zip), "%s", eq);
        else if (!strcmp(line, "notes")) snprintf(cfg.notes, sizeof(cfg.notes), "%s", eq);
        else if (!strcmp(line, "store")) snprintf(cfg.last_store, sizeof(cfg.last_store), "%s", eq);
        else if (!strcmp(line, "music")) cfg.music = atoi(eq);
        else if (!strcmp(line, "country")) cfg.country = !strcmp(eq, "ca") ? 1 : !strcmp(eq, "us") ? 0 : -1;
        else if (!strcmp(line, "lang")) cfg.lang = !strcmp(eq, "fr") ? 2 : !strcmp(eq, "es") ? 1 : !strcmp(eq, "en") ? 0 : -1;
        else if (!strcmp(line, "market")) snprintf(cfg.market, sizeof(cfg.market), "%s", eq);
        else if (!strcmp(line, "host")) snprintf(cfg.host, sizeof(cfg.host), "%s", eq);
        else if (!strcmp(line, "image_path")) { snprintf(dom_image_base, sizeof(dom_image_base), "%s", eq); image_base_custom = 1; }
    }
    fclose(f);
    dom_update_region();
    return 0;
}

int cfg_save(void)
{
    FILE *f = fopen("D:\\pizza.cfg", "w");
    if (!f) return -1;
    fprintf(f, "# Johnamos settings (edited from inside the app)\n");
    fprintf(f, "name=%s\nphone=%s\nemail=%s\nstreet=%s\napt=%s\nzip=%s\nnotes=%s\nstore=%s\n",
            cfg.name, cfg.phone, cfg.email, cfg.street, cfg.apt, cfg.zip, cfg.notes, cfg.last_store);
    if (cfg.music >= 0) fprintf(f, "music=%d\n", cfg.music);
    if (cfg.country >= 0) fprintf(f, "country=%s\n", cfg.country == 1 ? "ca" : "us");
    if (cfg.lang >= 0) fprintf(f, "lang=%s\n", cfg.lang == 2 ? "fr" : cfg.lang == 1 ? "es" : "en");
    if (cfg.market[0]) fprintf(f, "market=%s\n", cfg.market);
    if (cfg.host[0]) fprintf(f, "host=%s\n", cfg.host);
    if (image_base_custom) fprintf(f, "image_path=%s\n", dom_image_base);
    return fclose(f) == 0 ? 0 : -1;
}

const char *cfg_missing(void)
{
    if (!strchr(cfg.name, ' ') && cfg.name[0]) return "Name (first and last)";
    if (!cfg.name[0]) return "Name";
    if (strlen(cfg.phone) < 10) return "Phone";
    if (!strchr(cfg.email, '@') || !strchr(cfg.email, '.')) return "Email";
    if (!cfg.street[0]) return "Street address";
    {
        size_t zl = 0;
        const char *zp;
        for (zp = cfg.zip; *zp; zp++) if (*zp != ' ') zl++;
        if (zl < (isalpha((unsigned char)cfg.zip[0]) ? 6u : 5u)) return "ZIP / postal code";
    }
    return "";
}

int cfg_complete(void) { return cfg_missing()[0] == 0; }

/* ================================================================== */
/* stores                                                             */
/* ================================================================== */

static int fetch_json(const char *path, http_resp *r, jdoc *d, size_t max)
{
    if (http_get(API_HOST, path, max, r) != 0) {
        snprintf(dom_err, sizeof(dom_err), "%s", net_err);
        return -1;
    }
    if (r->status != 200) {
        snprintf(dom_err, sizeof(dom_err), "Domino's answered with HTTP %d.", r->status);
        free(r->body); r->body = NULL;
        return -1;
    }
    if (json_parse(d, r->body, r->len) != 0) {
        snprintf(dom_err, sizeof(dom_err), "Domino's reply could not be read.");
        free(r->body); r->body = NULL;
        return -1;
    }
    return 0;
}

int dom_find_stores(Store *out, int max)
{
    char street[160], enc_street[420], enc_zip[40], path[600];
    http_resp r;
    jdoc d;
    int stores, i, count = 0, a;

    {   /* Canadian postal codes are upper case */
        char *zc;
        for (zc = cfg.zip; *zc; zc++) *zc = (char)toupper((unsigned char)*zc);
    }
    dom_update_region();
    snprintf(street, sizeof(street), "%s%s%s", cfg.street, cfg.apt[0] ? " #" : "", cfg.apt);
    url_encode(street, enc_street, sizeof(enc_street));
    url_encode(cfg.zip, enc_zip, sizeof(enc_zip));
    snprintf(path, sizeof(path), "/power/store-locator?type=Delivery&c=%s&s=%s", enc_zip, enc_street);
    if (fetch_json(path, &r, &d, 2 * 1024 * 1024) != 0) return -1;

    /* how Domino's normalised the address (needed later to place the order) */
    memset(&addr, 0, sizeof(addr));
    a = jget(&d, 0, "Address");
    if (a >= 0) {
        jgets(&d, a, "Street", addr.street, sizeof(addr.street));
        jgets(&d, a, "City", addr.city, sizeof(addr.city));
        jgets(&d, a, "Region", addr.region, sizeof(addr.region));
        jgets(&d, a, "PostalCode", addr.zip, sizeof(addr.zip));
        jgets(&d, a, "StreetName", addr.street_name, sizeof(addr.street_name));
        jgets(&d, a, "StreetNumber", addr.street_number, sizeof(addr.street_number));
        jgets(&d, a, "UnitNumber", addr.unit, sizeof(addr.unit));
    }
    if (!addr.street[0]) snprintf(addr.street, sizeof(addr.street), "%s", cfg.street);
    if (!addr.zip[0]) snprintf(addr.zip, sizeof(addr.zip), "%s", cfg.zip);
    if (!addr.unit[0]) snprintf(addr.unit, sizeof(addr.unit), "%s", cfg.apt);
    if (!addr.street_number[0]) {                       /* fall back to splitting "350 5th Ave" */
        const char *sp = strchr(cfg.street, ' ');
        if (sp && isdigit((unsigned char)cfg.street[0])) {
            snprintf(addr.street_number, sizeof(addr.street_number), "%.*s", (int)(sp - cfg.street), cfg.street);
            snprintf(addr.street_name, sizeof(addr.street_name), "%s", sp + 1);
        }
    }

    stores = jget(&d, 0, "Stores");
    for (i = 0; i < jcount(&d, stores) && count < max; i++) {
        int s = jat(&d, stores, i), v, w;
        Store *o = &out[count];
        char loc[100], *nl;
        memset(o, 0, sizeof(*o));
        v = jget(&d, s, "IsDeliveryStore");
        if (v >= 0 && !jtrue(&d, v)) continue;
        jgets(&d, s, "StoreID", o->id, sizeof(o->id));
        jgets(&d, s, "AddressDescription", o->addr, sizeof(o->addr));
        jgets(&d, s, "Phone", o->phone, sizeof(o->phone));
        jgets(&d, s, "LocationInfo", loc, sizeof(loc));
        if (loc[0]) { char *p = strstr(o->addr, loc); if (p) *p = 0; }
        (void)(nl = NULL);
        trim(o->addr);
        o->open = jtrue(&d, jget(&d, s, "IsOpen"));
        o->min_order = jnum(&d, jget(&d, s, "MinimumDeliveryOrderAmount"));
        w = jget(&d, jget(&d, s, "ServiceMethodEstimatedWaitMinutes"), "Delivery");
        if (w >= 0) {
            o->wait_min = (int)jnum(&d, jget(&d, w, "Min"));
            o->wait_max = (int)jnum(&d, jget(&d, w, "Max"));
        }
        if (o->id[0]) count++;
    }
    json_free(&d);
    free(r.body);
    if (count == 0) snprintf(dom_err, sizeof(dom_err), "No delivery stores found for that address. Check the street and ZIP in Settings.");
    return count ? count : -1;
}

/* ================================================================== */
/* menu                                                               */
/* ================================================================== */

static Ent *load_ents(const char *key, int *count_out)
{
    int obj = jget(&menu.d, 0, key), k, j, n;
    Ent *e;
    *count_out = 0;
    if (obj < 0 || menu.d.t[obj].type != J_OBJ) return NULL;
    n = menu.d.t[obj].size;
    e = calloc((size_t)n + 1, sizeof(Ent));
    if (!e) return NULL;
    j = obj + 1;
    for (k = 0; k < n; k++) {
        jstr(&menu.d, j, e[k].code, sizeof(e[k].code));
        e[k].tok = j + 1;
        j = jskip(&menu.d, j + 1);
    }
    *count_out = n;
    return e;
}

static int find_ent(const Ent *e, int n, const char *code)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(e[i].code, code)) return i;
    return -1;
}

static void add_cat(int catobj, const char *name_override)
{
    int prods = jget(&menu.d, catobj, "Products"), k, np = jcount(&menu.d, prods), cnt = 0;
    Cat *c;
    char code[40];
    Cat *nc;
    if (np <= 0) return;
    nc = realloc(menu.cats, sizeof(Cat) * (size_t)(menu.ncats + 1));
    if (!nc) return;
    menu.cats = nc;
    c = &menu.cats[menu.ncats];
    memset(c, 0, sizeof(*c));
    jgets(&menu.d, catobj, "Name", c->name, sizeof(c->name));
    if (name_override && !c->name[0]) snprintf(c->name, sizeof(c->name), "%s", name_override);
    c->prods = malloc(sizeof(int) * (size_t)np);
    if (!c->prods) return;
    for (k = 0; k < np; k++) {
        int pi;
        jstr(&menu.d, jat(&menu.d, prods, k), code, sizeof(code));
        pi = find_ent(menu.products, menu.nproducts, code);
        if (pi >= 0) c->prods[cnt++] = pi;
    }
    c->nprods = cnt;
    if (cnt == 0 || !c->name[0]) { free(c->prods); return; }
    menu.ncats++;
}

void dom_free_menu(void)
{
    int i;
    for (i = 0; i < menu.ncats; i++) free(menu.cats[i].prods);
    free(menu.cats);
    free(menu.products);
    free(menu.variants);
    free(menu.coupons);
    if (menu.d.t) json_free(&menu.d);
    free(menu.js);
    memset(&menu, 0, sizeof(menu));
}

int dom_load_menu(const char *store_id)
{
    char path[120];
    http_resp r;
    int cats, k;

    dom_free_menu();
    snprintf(path, sizeof(path), "/power/store/%s/menu?lang=%s&structured=true", store_id, net_lang);
    if (http_get(API_HOST, path, 6 * 1024 * 1024, &r) != 0) {
        snprintf(dom_err, sizeof(dom_err), "%s", net_err);
        return -1;
    }
    if (r.status != 200) {
        snprintf(dom_err, sizeof(dom_err), "The store menu could not be loaded (HTTP %d).", r.status);
        free(r.body);
        return -1;
    }
    menu.js = r.body;
    menu.jslen = r.len;
    if (json_parse(&menu.d, menu.js, menu.jslen) != 0) {
        snprintf(dom_err, sizeof(dom_err), "The store menu could not be read.");
        dom_free_menu();
        return -1;
    }
    snprintf(menu.store_id, sizeof(menu.store_id), "%s", store_id);
    menu.products = load_ents("Products", &menu.nproducts);
    menu.variants = load_ents("Variants", &menu.nvariants);
    menu.coupons  = load_ents("Coupons", &menu.ncoupons);
    menu.toppings_tok = jget(&menu.d, 0, "Toppings");
    if (!menu.products || !menu.variants) {
        snprintf(dom_err, sizeof(dom_err), "The store menu had an unexpected layout.");
        dom_free_menu();
        return -1;
    }

    cats = jget(&menu.d, jget(&menu.d, jget(&menu.d, 0, "Categorization"), "Food"), "Categories");
    for (k = 0; k < jcount(&menu.d, cats); k++) {
        int c = jat(&menu.d, cats, k), subs, s;
        char nm[48];
        jgets(&menu.d, c, "Name", nm, sizeof(nm));
        if (!nm[0]) continue;
        add_cat(c, nm);
        subs = jget(&menu.d, c, "Categories");
        for (s = 0; s < jcount(&menu.d, subs); s++) add_cat(jat(&menu.d, subs, s), nm);
    }
    if (menu.ncats == 0) {
        snprintf(dom_err, sizeof(dom_err), "This store's menu has no items right now.");
        dom_free_menu();
        return -1;
    }
    return 0;
}

void prod_info(int pi, ProdInfo *o)
{
    int t = menu.products[pi].tok, v, k, n;
    memset(o, 0, sizeof(*o));
    snprintf(o->code, sizeof(o->code), "%s", menu.products[pi].code);
    jgets(&menu.d, t, "Name", o->name, sizeof(o->name));
    jgets(&menu.d, t, "Description", o->desc, sizeof(o->desc));
    jgets(&menu.d, t, "ImageCode", o->image, sizeof(o->image));
    if (!o->image[0]) snprintf(o->image, sizeof(o->image), "%s", o->code);
    {   /* menus fetched with includeAssets list ready-made photo addresses */
        char url[200];
        int im = jget(&menu.d, t, "Images");
        url[0] = 0;
        if (im >= 0) {
            jgets(&menu.d, im, "LARGE", url, sizeof(url));
            if (!url[0]) jgets(&menu.d, im, "THUMBNAIL", url, sizeof(url));
        }
        if (!strncmp(url, "https://cache.dominos.com/", 25) && strlen(url) - 25 < sizeof(o->image))
            snprintf(o->image, sizeof(o->image), "%s", url + 25);
    }
    jgets(&menu.d, t, "ProductType", o->type, sizeof(o->type));
    v = jget(&menu.d, t, "Variants");
    n = jcount(&menu.d, v);
    o->minprice = -1;
    for (k = 0; k < n; k++) {
        char code[40];
        int vi;
        jstr(&menu.d, jat(&menu.d, v, k), code, sizeof(code));
        vi = find_ent(menu.variants, menu.nvariants, code);
        if (vi >= 0) {
            double p = jnum(&menu.d, jget(&menu.d, menu.variants[vi].tok, "Price"));
            o->nvariants++;
            if (o->minprice < 0 || p < o->minprice) o->minprice = p;
        }
    }
    if (o->minprice < 0) o->minprice = 0;
}

int prod_variants(int pi, VarInfo *out, int max)
{
    int t = menu.products[pi].tok, v = jget(&menu.d, t, "Variants"), k, n = jcount(&menu.d, v), cnt = 0;
    for (k = 0; k < n && cnt < max; k++) {
        char code[40];
        int vi, vt, tags;
        VarInfo *o = &out[cnt];
        jstr(&menu.d, jat(&menu.d, v, k), code, sizeof(code));
        vi = find_ent(menu.variants, menu.nvariants, code);
        if (vi < 0) continue;
        vt = menu.variants[vi].tok;
        memset(o, 0, sizeof(*o));
        snprintf(o->code, sizeof(o->code), "%s", code);
        jgets(&menu.d, vt, "Name", o->name, sizeof(o->name));
        o->price = jnum(&menu.d, jget(&menu.d, vt, "Price"));
        tags = jget(&menu.d, vt, "Tags");
        jgets(&menu.d, tags, "DefaultToppings", o->deftop, sizeof(o->deftop));
        jgets(&menu.d, tags, "DefaultSides", o->defside, sizeof(o->defside));
        cnt++;
    }
    return cnt;
}

/* "X=1,C=1.5" -> value for code, or NULL */
static const char *list_value(const char *list, const char *code, char *buf, size_t cap)
{
    size_t cl = strlen(code);
    const char *p = list;
    while (p && *p) {
        if (!strncmp(p, code, cl) && p[cl] == '=') {
            size_t n = 0;
            p += cl + 1;
            while (p[n] && p[n] != ',' && n + 1 < cap) { buf[n] = p[n]; n++; }
            buf[n] = 0;
            return buf;
        }
        p = strchr(p, ',');
        if (p) p++;
    }
    return NULL;
}

int prod_toppings(int pi, const char *deftop, TopInfo *out, int max)
{
    int t = menu.products[pi].tok, cnt = 0;
    char avail[900], type[24], tmp[16];
    int group_obj, k, n;
    const char *p;

    jgets(&menu.d, t, "AvailableToppings", avail, sizeof(avail));
    jgets(&menu.d, t, "ProductType", type, sizeof(type));
    if (!avail[0] || menu.toppings_tok < 0) return 0;
    group_obj = jget(&menu.d, menu.toppings_tok, type);
    if (group_obj < 0) return 0;
    n = jcount(&menu.d, group_obj);
    /* keep the order Domino's lists them in */
    {
        int j = group_obj + 1;
        for (k = 0; k < n && cnt < max; k++) {
            char code[16];
            int vt = j + 1, tags;
            TopInfo *o = &out[cnt];
            jstr(&menu.d, j, code, sizeof(code));
            j = jskip(&menu.d, j + 1);
            /* is it available for this product? codes in avail are "X=0:0.5:1,C=..." */
            p = avail;
            {
                size_t cl = strlen(code);
                int found = 0;
                while (p && *p) {
                    if (!strncmp(p, code, cl) && (p[cl] == '=' || p[cl] == ',' || p[cl] == 0)) { found = 1; break; }
                    p = strchr(p, ',');
                    if (p) p++;
                }
                if (!found) continue;
            }
            memset(o, 0, sizeof(*o));
            snprintf(o->code, sizeof(o->code), "%s", code);
            jgets(&menu.d, vt, "Name", o->name, sizeof(o->name));
            tags = jget(&menu.d, vt, "Tags");
            if (jget(&menu.d, tags, "Sauce") >= 0) o->group = G_SAUCE;
            else if (jget(&menu.d, tags, "Cheese") >= 0) o->group = G_CHEESE;
            else if (jtrue(&menu.d, jget(&menu.d, tags, "NonMeat"))) o->group = G_VEG;
            else o->group = G_MEAT;
            o->was_default = list_value(deftop, code, tmp, sizeof(tmp)) != NULL;
            o->on = o->was_default;
            cnt++;
        }
    }
    return cnt;
}

void coupon_info(int ci, CouponInfo *o)
{
    int t = menu.coupons[ci].tok;
    memset(o, 0, sizeof(*o));
    snprintf(o->code, sizeof(o->code), "%s", menu.coupons[ci].code);
    jgets(&menu.d, t, "Name", o->name, sizeof(o->name));
    jgets(&menu.d, t, "Description", o->desc, sizeof(o->desc));
    o->price = jnum(&menu.d, jget(&menu.d, t, "Price"));
    o->local = jtrue(&menu.d, jget(&menu.d, t, "Local"));
}

int coupon_visible(int ci)
{
    int t = menu.coupons[ci].tok, tags = jget(&menu.d, t, "Tags");
    char nm[8], sm[16];
    if (jtrue(&menu.d, jget(&menu.d, tags, "Hidden"))) return 0;
    jgets(&menu.d, tags, "ServiceMethodOverride", sm, sizeof(sm));
    if (!strcmp(sm, "Carryout")) return 0;               /* pick-up only; this app orders delivery */
    jgets(&menu.d, t, "Name", nm, sizeof(nm));
    return nm[0] != 0;
}

/* ================================================================== */
/* cart                                                               */
/* ================================================================== */

int cart_add(const CartLine *l)
{
    int i;
    for (i = 0; i < cart.n; i++) {                      /* same item and options: bump quantity */
        if (!strcmp(cart.lines[i].code, l->code) && !strcmp(cart.lines[i].options, l->options)) {
            cart.lines[i].qty += l->qty;
            if (cart.lines[i].qty > 9) cart.lines[i].qty = 9;
            return 0;
        }
    }
    if (cart.n >= CART_MAX) return -1;
    cart.lines[cart.n++] = *l;
    return 0;
}

void cart_remove(int i)
{
    if (i < 0 || i >= cart.n) return;
    memmove(&cart.lines[i], &cart.lines[i + 1], sizeof(CartLine) * (size_t)(cart.n - i - 1));
    cart.n--;
}

void cart_clear(void) { memset(&cart, 0, sizeof(cart)); }

double cart_subtotal(void)
{
    double t = 0;
    int i;
    for (i = 0; i < cart.n; i++) t += cart.lines[i].price * cart.lines[i].qty;
    return t;
}

int cart_count(void)
{
    int t = 0, i;
    for (i = 0; i < cart.n; i++) t += cart.lines[i].qty;
    return t;
}

int cart_add_coupon(const char *code, const char *name)
{
    int i;
    for (i = 0; i < cart.ncoupons; i++) if (!strcmp(cart.coupons[i].code, code)) return 0;
    if (cart.ncoupons >= 8) return -1;
    snprintf(cart.coupons[cart.ncoupons].code, sizeof(cart.coupons[0].code), "%s", code);
    snprintf(cart.coupons[cart.ncoupons].name, sizeof(cart.coupons[0].name), "%s", name ? name : code);
    cart.ncoupons++;
    return 0;
}

void cart_remove_coupon(int i)
{
    if (i < 0 || i >= cart.ncoupons) return;
    memmove(&cart.coupons[i], &cart.coupons[i + 1], sizeof(CartCoupon) * (size_t)(cart.ncoupons - i - 1));
    cart.ncoupons--;
}

/* Builds {"X":{"1/1":"1"},"C":{"1/1":"1"},"SIDE":"1"} from the variant defaults and the toggles. */
void build_options(const char *deftop, const char *defside, const TopInfo *t, int nt, char *out, size_t cap)
{
    SB b;
    char code[24], val[16];
    const char *p;
    int first = 1, i;

    sb_init(&b);
    sb_add(&b, "{");
    /* default toppings, unless the user switched that topping off */
    p = deftop;
    while (p && *p) {
        const char *eq = strchr(p, '='), *comma = strchr(p, ',');
        size_t cl, vl;
        int off = 0;
        if (!eq) break;
        cl = (size_t)(eq - p);
        vl = comma ? (size_t)(comma - eq - 1) : strlen(eq + 1);
        snprintf(code, sizeof(code), "%.*s", (int)cl, p);
        snprintf(val, sizeof(val), "%.*s", (int)vl, eq + 1);
        for (i = 0; i < nt; i++) if (!strcmp(t[i].code, code) && !t[i].on) off = 1;
        if (!off) {
            if (!first) sb_add(&b, ",");
            sb_fmt(&b, "\"%s\":{\"1/1\":\"%s\"}", code, val);
            first = 0;
        }
        p = comma ? comma + 1 : NULL;
    }
    /* toppings added on top of the defaults */
    for (i = 0; i < nt; i++) {
        char tmp[16];
        if (t[i].on && !list_value(deftop, t[i].code, tmp, sizeof(tmp))) {
            if (!first) sb_add(&b, ",");
            sb_fmt(&b, "\"%s\":{\"1/1\":\"1\"}", t[i].code);
            first = 0;
        }
    }
    /* default sides (dips, seasoning packets, ...) */
    p = defside;
    while (p && *p) {
        const char *eq = strchr(p, '='), *comma = strchr(p, ',');
        size_t cl, vl;
        if (!eq) break;
        cl = (size_t)(eq - p);
        vl = comma ? (size_t)(comma - eq - 1) : strlen(eq + 1);
        if (!first) sb_add(&b, ",");
        sb_fmt(&b, "\"%.*s\":\"%.*s\"", (int)cl, p, (int)vl, eq + 1);
        first = 0;
        p = comma ? comma + 1 : NULL;
    }
    sb_add(&b, "}");
    snprintf(out, cap, "%s", b.p ? b.p : "{}");
    free(b.p);
}

/* ================================================================== */
/* price check and order                                              */
/* ================================================================== */

static void add_first_last(SB *b, char *first, size_t fcap, char *last, size_t lcap)
{
    const char *sp = strrchr(cfg.name, ' ');
    (void)b;
    if (sp) {
        snprintf(first, fcap, "%.*s", (int)(sp - cfg.name), cfg.name);
        snprintf(last, lcap, "%s", sp + 1);
    } else {
        snprintf(first, fcap, "%s", cfg.name);
        last[0] = 0;
    }
}

static void order_common(SB *b, const Store *s, const char *order_id, int placing, const char *amount)
{
    char first[48], last[48];
    int i;

    add_first_last(b, first, sizeof(first), last, sizeof(last));
    sb_add(b, "{");
    if (placing) sb_add(b, "\"Status\":0,");
    sb_add(b, "\"Order\":{\"Address\":{\"Street\":");
    sb_jstr(b, addr.street);
    sb_add(b, ",\"City\":"); sb_jstr(b, addr.city);
    sb_add(b, ",\"Region\":"); sb_jstr(b, addr.region);
    sb_add(b, ",\"PostalCode\":"); sb_jstr(b, addr.zip);
    sb_add(b, ",\"Type\":"); sb_jstr(b, addr.unit[0] ? "Apartment" : "House");
    sb_add(b, ",\"StreetName\":"); sb_jstr(b, addr.street_name);
    sb_add(b, ",\"StreetNumber\":"); sb_jstr(b, addr.street_number);
    if (addr.unit[0]) { sb_add(b, ",\"UnitNumber\":"); sb_jstr(b, addr.unit); }
    if (placing) { sb_add(b, ",\"DeliveryInstructions\":"); sb_jstr(b, cfg.notes); }
    sb_add(b, "},");

    if (placing) sb_add(b, "\"EstimatedWaitMinutes\":\"21-31\",\"Channel\":\"Mobile\",\"DataWarehouseUpdate\":false,");

    sb_add(b, "\"Coupons\":[");
    for (i = 0; i < cart.ncoupons; i++) {
        if (i) sb_add(b, ",");
        sb_add(b, "{\"Code\":"); sb_jstr(b, cart.coupons[i].code);
        sb_fmt(b, ",\"Qty\":1,\"ID\":%d,\"IsBelongingToOrder\":false,\"IsNew\":true}", i + 1);
    }
    sb_add(b, "],\"CustomerID\":\"\",\"Email\":");
    sb_jstr(b, placing ? cfg.email : "");
    sb_add(b, ",\"Extension\":\"\",\"FirstName\":"); sb_jstr(b, placing ? first : "");
    sb_add(b, ",\"LastName\":"); sb_jstr(b, placing ? last : "");
    sb_add(b, ",\"LanguageCode\":"); sb_jstr(b, net_lang); sb_add(b, ",");
    if (!placing) sb_add(b, "\"metaData\":null,");
    sb_add(b, "\"OrderChannel\":\"OLO\",\"OrderID\":"); sb_jstr(b, order_id);
    sb_add(b, ",\"OrderMethod\":\"Web\",\"OrderTaker\":");
    sb_add(b, placing ? "\"power\"" : "null");
    if (placing) sb_add(b, ",\"OrderTakeSeconds\":0");
    sb_add(b, ",\"Payments\":[");
    if (placing) {
        sb_add(b, "{\"Type\":\"Cash\",\"Amount\":");
        sb_jstr(b, amount);
        sb_add(b, "}");
    }
    sb_add(b, "],");
    if (placing) sb_add(b, "\"PendingOrder\":false,\"PlaceOrderMs\":0,\"Platform\":\"web\",\"PriceOrderMs\":0,");
    {
        char digits[24];
        size_t o = 0;
        const char *pp;
        for (pp = cfg.phone; *pp && o + 1 < sizeof(digits); pp++) if (*pp >= '0' && *pp <= '9') digits[o++] = *pp;
        digits[o] = 0;
        sb_add(b, "\"Phone\":"); sb_jstr(b, placing ? digits : "");
    }
    sb_add(b, ",\"PhonePrefix\":\"\",\"Products\":[");
    for (i = 0; i < cart.n; i++) {
        if (i) sb_add(b, ",");
        sb_add(b, "{\"Code\":"); sb_jstr(b, cart.lines[i].code);
        sb_fmt(b, ",\"Qty\":%d,\"ID\":%d,\"isNew\":true,\"Options\":%s}", cart.lines[i].qty, i + 1, cart.lines[i].options);
    }
    sb_add(b, "],\"ServiceMethod\":\"Delivery\",\"SourceOrganizationURI\":\"order.dominos.com\",\"StoreID\":");
    sb_jstr(b, s->id);
    sb_add(b, ",\"Tags\":{},\"Version\":\"1.0\",");
    if (placing) sb_add(b, "\"Status\":0,");
    sb_add(b, "\"NoCombine\":true,\"Partners\":{},\"HotspotsLite\":false,\"OrderInfoCollection\":[]");
    if (placing) sb_add(b, ",\"TestOrderFlagCCProcess\":false,\"metaData\":{\"PiePassPickup\":false,\"calculateNutrition\":true,\"contactless\":false}");
    sb_add(b, "}}");
}

/* Collects Domino's messages (Order.StatusItems and per-coupon problems) into out. */
static void collect_messages(const jdoc *d, char *out, size_t cap)
{
    int order = jget(d, 0, "Order"), items = jget(d, order, "StatusItems"), k, coupons;
    size_t o = strlen(out);
    for (k = 0; k < jcount(d, items) && o + 4 < cap; k++) {
        int it = jat(d, items, k);
        char code[60], text[160];
        jgets(d, it, "Code", code, sizeof(code));
        jgets(d, it, "PulseText", text, sizeof(text));
        if (!strcmp(code, "AutoAddedOrderId") || !strcmp(code, "AutoAddedStoreId")) continue;
        o += (size_t)snprintf(out + o, cap - o, "%s%s", o ? "  " : "", text[0] ? text : code);
    }
    coupons = jget(d, order, "Coupons");
    for (k = 0; k < jcount(d, coupons) && o + 4 < cap; k++) {
        int c = jat(d, coupons, k), st = jget(d, c, "Status");
        char code[24], text[120];
        if (st < 0 || (int)jnum(d, st) == 0) continue;
        jgets(d, c, "Code", code, sizeof(code));
        jgets(d, jget(d, c, "StatusItems") >= 0 ? jat(d, jget(d, c, "StatusItems"), 0) : -1, "Code", text, sizeof(text));
        o += (size_t)snprintf(out + o, cap - o, "%sCoupon %s: %s", o ? "  " : "", code, text[0] ? text : "not applied");
    }
}

static int post_step(const char *path, const char *body, int once, http_resp *r, jdoc *d, int *status_out)
{
    int rc = once ? http_post_json_once(API_HOST, path, body, 512 * 1024, r) : http_post_json(API_HOST, path, body, 512 * 1024, r);
    if (rc != 0) { snprintf(dom_err, sizeof(dom_err), "%s", net_err); return -1; }
    if (json_parse(d, r->body, r->len) != 0) {
        snprintf(dom_err, sizeof(dom_err), "Domino's answered (HTTP %d) but the reply could not be read.", r->status);
        free(r->body); r->body = NULL;
        return -1;
    }
    *status_out = (int)jnum(d, jget(d, 0, "Status"));
    return 0;
}

int dom_price(const Store *s, Quote *q)
{
    int step, status = 0;
    char order_id[48] = "";
    memset(q, 0, sizeof(*q));
    if (cart.n == 0) { snprintf(dom_err, sizeof(dom_err), "Your cart is empty."); return -1; }

    for (step = 0; step < 3; step++) {
        SB b;
        http_resp r;
        jdoc d;
        char msg[400] = "";
        sb_init(&b);
        order_common(&b, s, order_id, 0, "");
        if (step == 2) {                                   /* mark the final step like the website does */
            char *m = strstr(b.p, "\"metaData\":null");
            if (m) {
                const char *rep = "\"metaData\":{\"orderFunnel\":\"payments\"}";
                size_t tail = strlen(m + 15);
                memmove(m + strlen(rep), m + 15, tail + 1);
                memcpy(m, rep, strlen(rep));
            }
        }
        if (post_step(step == 2 ? "/power/price-order" : "/power/validate-order", b.p, 0, &r, &d, &status) != 0) {
            free(b.p);
            return -1;
        }
        free(b.p);
        collect_messages(&d, msg, sizeof(msg));
        if (status != 0 && status != 1) {
            snprintf(dom_err, sizeof(dom_err), "%s", msg[0] ? msg : "Domino's could not price this order.");
            json_free(&d); free(r.body);
            return -1;
        }
        {
            int order = jget(&d, 0, "Order");
            jgets(&d, order, "OrderID", order_id, sizeof(order_id));
            if (step == 2) {
                int am = jget(&d, order, "Amounts"), bd = jget(&d, order, "AmountsBreakdown");
                q->total = jnum(&d, jget(&d, am, "Customer"));
                q->fees = jnum(&d, jget(&d, am, "Surcharge"));
                q->tax = jnum(&d, jget(&d, am, "Tax"));
                q->food = jnum(&d, jget(&d, bd, "FoodAndBeverage"));
                if (q->food == 0) q->food = jnum(&d, jget(&d, am, "Menu"));
                q->discount = jnum(&d, jget(&d, am, "Discount"));
                if (q->discount == 0) q->discount = jnum(&d, jget(&d, bd, "Savings"));
                if (q->fees == 0) q->fees = jnum(&d, jget(&d, bd, "DeliveryFee"));
                if (q->tax == 0) q->tax = jnum(&d, jget(&d, am, "Tax1")) + jnum(&d, jget(&d, am, "Tax2"));
                snprintf(q->note, sizeof(q->note), "%s", msg);
                snprintf(q->order_id, sizeof(q->order_id), "%s", order_id);
                q->ok = q->total > 0;
            }
        }
        json_free(&d);
        free(r.body);
    }
    if (!q->ok) { snprintf(dom_err, sizeof(dom_err), "Domino's did not return a total for this order."); return -1; }
    return 0;
}

int dom_place(const Store *s, const Quote *q, char *msg, size_t cap)
{
    SB b;
    http_resp r;
    jdoc d;
    int status = 0;
    char amount[24], text[400] = "";

    snprintf(amount, sizeof(amount), "%.2f", q->total);
    sb_init(&b);
    order_common(&b, s, q->order_id, 1, amount);
    if (post_step("/power/place-order", b.p, 1, &r, &d, &status) != 0) {
        free(b.p);
        snprintf(msg, cap, "%s Your order was NOT confirmed - check your email or call the store before trying again.", dom_err);
        return -1;
    }
    free(b.p);
    collect_messages(&d, text, sizeof(text));
    if (status != 0 && status != 1) {
        snprintf(msg, cap, "%s", text[0] ? text : "Domino's did not accept the order.");
        json_free(&d); free(r.body);
        return -1;
    }
    {
        int order = jget(&d, 0, "Order");
        char wait[40] = "";
        jgets(&d, order, "EstimatedWaitMinutes", wait, sizeof(wait));
        snprintf(msg, cap, "%s", wait[0] ? wait : "");
    }
    json_free(&d);
    free(r.body);
    return 0;
}
