/*
 * HTTPS client for the original Xbox (lwIP sockets + mbedTLS).
 * Keeps one TLS connection per host open and reuses it, because a full TLS handshake
 * on a 733 MHz Pentium III is slow.
 */
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"

#include "net.h"

#define NET_TIMEOUT_MS 20000
#define MAX_CONNS 4

char net_err[200] = "";
char net_market[24] = "UNITED_STATES";
char net_lang[8] = "en";
char net_device_id[40] = "";
void (*net_idle_cb)(size_t) = NULL;

/* overrides used only by the desktop test build */
char net_test_ip[24] = "";
int  net_test_port = 0;

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_ssl_config conf;
static mbedtls_x509_crt cacert;
static int inited = 0;
static size_t progress_bytes = 0;

typedef struct {
    char host[64];
    int fd;
    int up;
    mbedtls_ssl_context ssl;
} Conn;
static Conn conns[MAX_CONNS];

/* ------------------------------------------------------------------ */
/* Entropy: no hardware RNG on the Xbox, so mix timing jitter.         */
/* ------------------------------------------------------------------ */

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    size_t i;
    unsigned int mix = (unsigned int)GetTickCount();
    (void)data;
    for (i = 0; i < len; i++) {
        unsigned long long t0 = __builtin_ia32_rdtsc();
        volatile int spin;
        for (spin = 0; spin < 200 + (int)(t0 & 0x3F); spin++) { }
        {
            unsigned long long t1 = __builtin_ia32_rdtsc();
            mix = mix * 1664525u + 1013904223u + (unsigned int)(t1 - t0) + (unsigned int)(t1 >> 7);
        }
        if ((i & 15) == 15) Sleep(1);
        output[i] = (unsigned char)(mix >> 13);
    }
    *olen = len;
    return 0;
}

static void set_err(const char *what, int ret)
{
    char buf[100];
    if (ret) {
        mbedtls_strerror(ret, buf, sizeof(buf));
        snprintf(net_err, sizeof(net_err), "%s: %s (-0x%04X)", what, buf, (unsigned)-ret);
    } else {
        snprintf(net_err, sizeof(net_err), "%s", what);
    }
}

/* ------------------------------------------------------------------ */
/* Sockets                                                            */
/* ------------------------------------------------------------------ */

static int wait_fd(int fd, int for_write, int total_ms)
{
    int waited = 0;
    while (waited < total_ms) {
        fd_set fds;
        struct timeval tv;
        int r;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 150 * 1000;
        r = select(fd + 1, for_write ? NULL : &fds, for_write ? &fds : NULL, NULL, &tv);
        if (r > 0) return 1;
        waited += 150;
        if (net_idle_cb) net_idle_cb(progress_bytes);
    }
    return 0;
}

static int tcp_connect(const char *host)
{
    struct addrinfo hints, *res = NULL;
    char portstr[8];
    int fd, on = 1, port = 443;
    const char *lookup = host;

    if (net_test_ip[0]) { lookup = net_test_ip; port = net_test_port; }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(lookup, portstr, &hints, &res) != 0 || !res) {
        snprintf(net_err, sizeof(net_err), "Could not look up %s (DNS). Check your network.", host);
        return -1;
    }
    fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) { freeaddrinfo(res); set_err("socket() failed", 0); return -1; }
    ioctlsocket(fd, FIONBIO, &on);
    connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (!wait_fd(fd, 1, NET_TIMEOUT_MS)) {
        closesocket(fd);
        snprintf(net_err, sizeof(net_err), "Connecting to %s timed out.", host);
        return -1;
    }
    {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err) {
            closesocket(fd);
            snprintf(net_err, sizeof(net_err), "Connection to %s refused (%d).", host, err);
            return -1;
        }
    }
    on = 0;
    ioctlsocket(fd, FIONBIO, &on);
    return fd;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int n = send(*(int *)ctx, (const char *)buf, (int)len, 0);
    return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx, n;
    if (!wait_fd(fd, 0, NET_TIMEOUT_MS)) return MBEDTLS_ERR_SSL_TIMEOUT;
    n = recv(fd, (char *)buf, (int)len, 0);
    if (n < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    return n;                                   /* 0 = connection closed */
}

/* ------------------------------------------------------------------ */
/* Init                                                               */
/* ------------------------------------------------------------------ */

int net_init(void)
{
    FILE *f;
    char *pem;
    size_t pem_len;
    int ret;

    if (inited) return 0;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);

    ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"pizza", 5);
    if (ret) { set_err("Random generator", ret); return -1; }

    f = fopen("D:\\cacert.pem", "rb");
    if (!f) { set_err("Missing D:\\cacert.pem - copy it next to default.xbe", 0); return -1; }
    fseek(f, 0, SEEK_END);
    pem_len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    pem = malloc(pem_len + 1);
    if (!pem || fread(pem, 1, pem_len, f) != pem_len) { fclose(f); free(pem); set_err("Could not read cacert.pem", 0); return -1; }
    fclose(f);
    pem[pem_len] = 0;
    ret = mbedtls_x509_crt_parse(&cacert, (const unsigned char *)pem, pem_len + 1);
    free(pem);
    if (ret < 0) { set_err("Certificate bundle", ret); return -1; }

    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret) { set_err("TLS config", ret); return -1; }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    {
        unsigned char rb[16];
        mbedtls_ctr_drbg_random(&drbg, rb, sizeof(rb));
        rb[6] = (unsigned char)((rb[6] & 0x0f) | 0x40); rb[8] = (unsigned char)((rb[8] & 0x3f) | 0x80);
        snprintf(net_device_id, sizeof(net_device_id), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 rb[0], rb[1], rb[2], rb[3], rb[4], rb[5], rb[6], rb[7], rb[8], rb[9], rb[10], rb[11], rb[12], rb[13], rb[14], rb[15]);
    }
    inited = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Connections                                                        */
/* ------------------------------------------------------------------ */

static void conn_close(Conn *c)
{
    if (c->up) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        closesocket(c->fd);
    }
    c->up = 0;
    c->fd = -1;
}

void net_close_all(void)
{
    int i;
    for (i = 0; i < MAX_CONNS; i++) if (conns[i].up) conn_close(&conns[i]);
}

static int conn_open(Conn *c, const char *host)
{
    int ret;
    uint32_t flags;

    c->fd = tcp_connect(host);
    if (c->fd < 0) return -1;
    mbedtls_ssl_init(&c->ssl);
    ret = mbedtls_ssl_setup(&c->ssl, &conf);
    if (ret) { set_err("TLS setup", ret); mbedtls_ssl_free(&c->ssl); closesocket(c->fd); return -1; }
    mbedtls_ssl_set_hostname(&c->ssl, net_test_ip[0] ? "localhost" : host);
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, bio_send, bio_recv, NULL);
    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                char vb[120];
                flags = mbedtls_ssl_get_verify_result(&c->ssl);
                mbedtls_x509_crt_verify_info(vb, sizeof(vb), "", flags);
                snprintf(net_err, sizeof(net_err), "Certificate for %s not trusted: %.120s", host, vb);
            } else {
                set_err("TLS handshake", ret);
            }
            mbedtls_ssl_free(&c->ssl);
            closesocket(c->fd);
            return -1;
        }
    }
    snprintf(c->host, sizeof(c->host), "%s", host);
    c->up = 1;
    return 0;
}

static Conn *conn_for(const char *host, int *reused)
{
    int i, free_slot = -1;
    for (i = 0; i < MAX_CONNS; i++) {
        if (conns[i].up && !strcmp(conns[i].host, host)) { *reused = 1; return &conns[i]; }
        if (!conns[i].up && free_slot < 0) free_slot = i;
    }
    *reused = 0;
    if (free_slot < 0) { conn_close(&conns[0]); free_slot = 0; }
    if (conn_open(&conns[free_slot], host) != 0) return NULL;
    return &conns[free_slot];
}

/* ------------------------------------------------------------------ */
/* HTTP                                                               */
/* ------------------------------------------------------------------ */

static int conn_write_all(Conn *c, const char *p, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        int ret = mbedtls_ssl_write(&c->ssl, (const unsigned char *)p + sent, n - sent);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret < 0) { set_err("Sending request", ret); return -1; }
        sent += (size_t)ret;
    }
    return 0;
}

/* reads more into *buf. returns bytes read, 0 on clean EOF, -1 on error */
static long fill(Conn *c, char **buf, size_t *len, size_t *cap, size_t max)
{
    int ret;
    if (*len + 8192 > *cap) {
        size_t ncap = *cap * 2;
        char *nb;
        if (ncap > max + 65536) ncap = max + 65536;
        if (ncap <= *len + 1024) { snprintf(net_err, sizeof(net_err), "Reply is too large."); return -1; }
        nb = realloc(*buf, ncap + 1);
        if (!nb) { snprintf(net_err, sizeof(net_err), "Out of memory."); return -1; }
        *buf = nb;
        *cap = ncap;
    }
    for (;;) {
        ret = mbedtls_ssl_read(&c->ssl, (unsigned char *)*buf + *len, *cap - *len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        break;
    }
    if (ret > 0) {
        *len += (size_t)ret;
        progress_bytes = *len;
        if (net_idle_cb) net_idle_cb(progress_bytes);
        return ret;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == MBEDTLS_ERR_SSL_CONN_EOF) return 0;
    set_err("Reading reply", ret);
    return -1;
}

static int ci_starts(const char *s, const char *prefix)
{
    while (*prefix) {
        char a = *s++, b = *prefix++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != b) return 0;
    }
    return 1;
}

/* Walks chunks. If complete, returns 1 and *body_len is the decoded size; when decode!=0 rewrites in place. */
static int chunks_complete(char *body, size_t len, int decode, size_t *body_len)
{
    size_t in = 0, out = 0;
    for (;;) {
        char *end;
        size_t line_end, chunk;
        if (in >= len) return 0;
        chunk = strtoul(body + in, &end, 16);
        if (end == body + in) return 0;
        line_end = in;
        while (line_end < len && body[line_end] != '\n') line_end++;
        if (line_end >= len) return 0;
        line_end++;
        if (chunk == 0) {
            /* need the final CRLF after the zero chunk (trailers are ignored) */
            if (line_end + 2 > len) return 0;
            *body_len = out;
            return 1;
        }
        if (line_end + chunk + 2 > len) return 0;
        if (decode) memmove(body + out, body + line_end, chunk);
        out += chunk;
        in = line_end + chunk + 2;
    }
}

/* Returns 0 ok, -1 failure, -2 failure before any reply byte (stale connection) */
static int do_request(Conn *c, const char *host, const char *method, const char *path,
                      const char *body, size_t max, http_resp *r)
{
    char *head = malloc(2048 + (body ? 0 : 0));
    char *buf = NULL;
    size_t len = 0, cap = 65536, hdr_end = 0, body_off, blen;
    long content_length = -1;
    int chunked = 0, conn_close_hdr = 0, status = 0, hn;
    size_t bl = body ? strlen(body) : 0;

    if (!head) { snprintf(net_err, sizeof(net_err), "Out of memory."); return -1; }
    if (body) {
        hn = snprintf(head, 2048,
            "%s %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json, text/plain, */*\r\n"
            "Accept-Encoding: identity\r\nAccept-Language: en-US,en;q=0.5\r\n"
            "Content-Type: application/json; charset=utf-8\r\nContent-Length: %lu\r\n"
            "Origin: https://%s\r\nMarket: %s\r\nDPZ-Language: %s\r\nDPZ-Market: %s\r\nX-DPZ-D: %s\r\n"
            "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.0 Safari/605.1.15\r\n"
            "Connection: keep-alive\r\n\r\n", method, path, host, (unsigned long)bl, host, net_market, net_lang, net_market, net_device_id);
    } else {
        hn = snprintf(head, 2048,
            "%s %s HTTP/1.1\r\nHost: %s\r\nAccept: application/json, text/javascript, */*; q=0.01\r\n"
            "Accept-Encoding: identity\r\nMarket: %s\r\nDPZ-Language: %s\r\nDPZ-Market: %s\r\nX-DPZ-D: %s\r\n"
            "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.0 Safari/605.1.15\r\n"
            "Connection: keep-alive\r\n\r\n", method, path, host, net_market, net_lang, net_market, net_device_id);
    }
    if (hn <= 0 || hn >= 2048) { free(head); snprintf(net_err, sizeof(net_err), "Request path too long."); return -1; }
    progress_bytes = 0;
    if (conn_write_all(c, head, (size_t)hn) != 0 || (body && conn_write_all(c, body, bl) != 0)) {
        free(head);
        return -2;
    }
    free(head);

    buf = malloc(cap + 1);
    if (!buf) { snprintf(net_err, sizeof(net_err), "Out of memory."); return -1; }

    for (;;) {                                        /* headers */
        long n;
        size_t i;
        for (i = 0; i + 3 < len; i++)
            if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') { hdr_end = i; goto have_headers; }
        n = fill(c, &buf, &len, &cap, max);
        if (n < 0) { free(buf); return len == 0 ? -2 : -1; }
        if (n == 0) { free(buf); if (len == 0) return -2; snprintf(net_err, sizeof(net_err), "Server hung up mid-reply."); return -1; }
    }
have_headers:
    buf[len] = 0;
    body_off = hdr_end + 4;
    if (!strncmp(buf, "HTTP/", 5)) status = atoi(buf + 9);
    {
        char *line = strstr(buf, "\r\n");
        while (line && (size_t)(line - buf) < hdr_end) {
            line += 2;
            if (ci_starts(line, "content-length:")) content_length = atol(line + 15);
            else if (ci_starts(line, "transfer-encoding:")) {
                char *e = strstr(line, "\r\n");
                char *ch = strstr(line, "chunked");
                if (ch && (!e || ch < e)) chunked = 1;
            } else if (ci_starts(line, "connection:")) {
                char *e = strstr(line, "\r\n");
                char *cl = strstr(line, "close");
                if (cl && (!e || cl < e)) conn_close_hdr = 1;
            }
            line = strstr(line, "\r\n");
        }
    }
    if (status == 204 || status == 304) content_length = 0;

    if (chunked) {
        size_t decoded = 0;
        for (;;) {
            long n;
            if (chunks_complete(buf + body_off, len - body_off, 0, &decoded)) break;
            n = fill(c, &buf, &len, &cap, max);
            if (n < 0) { free(buf); return -1; }
            if (n == 0) { conn_close_hdr = 1; break; }
        }
        buf[len] = 0;
        chunks_complete(buf + body_off, len - body_off, 1, &decoded);
        blen = decoded;
    } else if (content_length >= 0) {
        while (len - body_off < (size_t)content_length) {
            long n = fill(c, &buf, &len, &cap, max);
            if (n < 0) { free(buf); return -1; }
            if (n == 0) { conn_close_hdr = 1; break; }
        }
        blen = len - body_off < (size_t)content_length ? len - body_off : (size_t)content_length;
    } else {
        for (;;) {
            long n = fill(c, &buf, &len, &cap, max);
            if (n < 0) { free(buf); return -1; }
            if (n == 0) break;
        }
        conn_close_hdr = 1;
        blen = len - body_off;
    }
    memmove(buf, buf + body_off, blen);
    buf[blen] = 0;
    r->status = status;
    r->body = buf;
    r->len = blen;
    if (conn_close_hdr) conn_close(c);
    return 0;
}

/* retry_ok: a request that got no reply on a reused connection is tried once more on a fresh
 * one. Never allow that for orders: the first attempt may already have been processed. */
static int request(const char *host, const char *method, const char *path, const char *body,
                   size_t max_body, http_resp *r, int retry_ok)
{
    int attempt;
    r->status = 0; r->body = NULL; r->len = 0;
    if (!inited && net_init() != 0) return -1;
    for (attempt = 0; attempt < 2; attempt++) {
        int reused, rc;
        Conn *c = conn_for(host, &reused);
        if (!c) return -1;
        rc = do_request(c, host, method, path, body, max_body, r);
        if (rc == 0) return 0;
        conn_close(c);
        if (rc == -2 && reused && retry_ok && attempt == 0) continue;   /* stale keep-alive connection */
        if (rc == -2) snprintf(net_err, sizeof(net_err), "No reply from %s.", host);
        return -1;
    }
    return -1;
}

int http_get(const char *host, const char *path, size_t max_body, http_resp *r)
{
    return request(host, "GET", path, NULL, max_body, r, 1);
}

int http_post_json(const char *host, const char *path, const char *json, size_t max_body, http_resp *r)
{
    return request(host, "POST", path, json, max_body, r, 1);
}

int http_post_json_once(const char *host, const char *path, const char *json, size_t max_body, http_resp *r)
{
    int i;
    for (i = 0; i < MAX_CONNS; i++)                 /* start from a fresh connection */
        if (conns[i].up && !strcmp(conns[i].host, host)) conn_close(&conns[i]);
    return request(host, "POST", path, json, max_body, r, 0);
}
