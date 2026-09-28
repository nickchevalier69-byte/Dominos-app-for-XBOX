/*
 * Domino's ordering logic: store search, menu, cart, price check and placing an order.
 * Talks to the same public ordering service the Domino's website uses.
 */
#ifndef PIZZA_DOM_H
#define PIZZA_DOM_H

#include <stddef.h>
#include "json.h"

/* ---------- saved user info ---------- */
typedef struct {
    char name[64], phone[24], email[64];
    char street[64], apt[16], zip[12];
    char notes[80];
    char last_store[16];
    int music;                               /* volume 0-100, -1 = default */
    int country;                             /* 0 US, 1 Canada, -1 = guess from the postal code */
    int lang;                                /* 0 English, 1 Spanish, 2 French, -1 = not chosen yet */
    char market[24], host[64];               /* optional overrides (market=, host= in pizza.cfg) */
} Cfg;
extern Cfg cfg;

int  cfg_load(void);
int  cfg_save(void);                       /* 0 ok */
int  cfg_complete(void);                   /* everything needed to order is filled in */
const char *cfg_missing(void);             /* name of the first missing field, or "" */

/* ---------- stores ---------- */
typedef struct {
    char id[12], addr[100], phone[24];
    int open, wait_min, wait_max;
    double min_order;
} Store;

/* delivery address as Domino's normalised it (from the store search reply) */
typedef struct {
    char street[80], city[40], region[8], zip[12], street_name[60], street_number[16], unit[16];
} Addr;
extern Addr addr;

int dom_find_stores(Store *out, int max);   /* count, or -1 (see dom_err) */
extern char dom_err[300];

/* ---------- menu ---------- */
typedef struct { char code[40]; int tok; } Ent;
typedef struct { char name[48]; int *prods; int nprods; } Cat;
typedef struct {
    char *js; size_t jslen; jdoc d;
    Ent *products; int nproducts;
    Ent *variants; int nvariants;
    Ent *coupons;  int ncoupons;
    Cat *cats;     int ncats;
    int toppings_tok;
    char store_id[16];
} Menu;
extern Menu menu;

int  dom_load_menu(const char *store_id);   /* 0 ok */
void dom_free_menu(void);

typedef struct {
    char code[40], name[80], desc[260], image[128], type[24];   /* image: image code, or a /path from the menu */
    double minprice;
    int nvariants;
} ProdInfo;
void prod_info(int pi, ProdInfo *out);

typedef struct {
    char code[40], name[90];
    double price;
    char deftop[200], defside[100];
} VarInfo;
int prod_variants(int pi, VarInfo *out, int max);

enum { G_SAUCE = 0, G_CHEESE, G_MEAT, G_VEG };
typedef struct { char code[16]; char name[44]; int group; int on; int was_default; } TopInfo;
int prod_toppings(int pi, const char *deftop, TopInfo *out, int max);

typedef struct { char code[24], name[64], desc[220]; double price; int local; } CouponInfo;
void coupon_info(int ci, CouponInfo *out);
int  coupon_visible(int ci);

/* ---------- cart ---------- */
#define CART_MAX 24
typedef struct {
    char code[40], name[90], desc[120];
    double price;
    int qty;
    char options[1400];                      /* JSON object text */
} CartLine;
typedef struct { char code[24], name[64]; } CartCoupon;
typedef struct {
    CartLine lines[CART_MAX]; int n;
    CartCoupon coupons[8]; int ncoupons;
} Cart;
extern Cart cart;

int    cart_add(const CartLine *l);          /* 0 ok, -1 full */
void   cart_remove(int i);
void   cart_clear(void);
double cart_subtotal(void);                  /* sum of menu prices, before coupons/tax */
int    cart_count(void);                     /* total quantity */
int    cart_add_coupon(const char *code, const char *name);
void   cart_remove_coupon(int i);
void   build_options(const char *deftop, const char *defside, const TopInfo *t, int nt, char *out, size_t cap);

/* ---------- price check and order ---------- */
typedef struct {
    double food, discount, fees, tax, total;
    char order_id[48];
    char note[400];                          /* warnings/messages from Domino's (coupons etc.) */
    int ok;
} Quote;

int dom_price(const Store *s, Quote *q);     /* 0 ok, -1 failed (dom_err) */
int dom_place(const Store *s, const Quote *q, char *msg, size_t cap);   /* 0 placed */

/* ---------- region (from the ZIP / postal code) ---------- */
void        dom_update_region(void);       /* call after cfg.zip changes */
int         dom_is_canada(void);
int         dom_country(void);             /* 0 US, 1 Canada (what is really in use) */
const char *dom_order_page(void);          /* page that mentions the current image folder */
const char *dom_host(void);                /* order.dominos.com or order.dominos.ca */
const char *dom_currency(void);            /* "USD" or "CAD" */

/* ---------- misc ---------- */
void money(double v, char *out, size_t cap);        /* "$12.99" */
void url_encode(const char *s, char *out, size_t cap);
extern char dom_image_base[200];                    /* where product photos live */

#endif
