#ifndef PIZZA_NET_H
#define PIZZA_NET_H

#include <stddef.h>

typedef struct {
    int status;        /* HTTP status, 0 if none */
    char *body;        /* malloc'd, NUL terminated (free with free()) */
    size_t len;
} http_resp;

extern char net_device_id[40];        /* random id sent as X-DPZ-D (the website sends one too) */
extern char net_lang[8];              /* "en" or "es" (DPZ-Language header) */
extern char net_market[24];         /* "UNITED_STATES" or "CANADA" (sent as a header) */
extern char net_err[200];          /* last error text */
extern void (*net_idle_cb)(size_t bytes_so_far);   /* called while waiting, may be NULL */

/* Call once after the network is up. Returns 0 on success. */
int net_init(void);

/* Blocking requests. Connections are kept open per host and reused. Returns 0 on success
 * (any HTTP status), -1 on failure (see net_err). Caller frees r->body. */
int http_get(const char *host, const char *path, size_t max_body, http_resp *r);
int http_post_json(const char *host, const char *path, const char *json, size_t max_body, http_resp *r);

/* Like http_post_json but on a brand-new connection and never retried (used for placing orders,
 * so a flaky link can never send the same order twice). */
int http_post_json_once(const char *host, const char *path, const char *json, size_t max_body, http_resp *r);

void net_close_all(void);

#endif
