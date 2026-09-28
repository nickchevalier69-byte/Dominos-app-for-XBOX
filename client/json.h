/*
 * Small JSON reader: tokenises a whole document once, then lets you walk it.
 * Handles any whitespace / key order. Not a validator - it trusts the server.
 */
#ifndef PIZZA_JSON_H
#define PIZZA_JSON_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { J_OBJ = 0, J_ARR = 1, J_STR = 2, J_PRIM = 3 };

typedef struct { int type, start, end, size; } jtok;   /* size: keys (obj) / items (arr) */
typedef struct { const char *js; jtok *t; int n; } jdoc;

static void json_free(jdoc *d)
{
    free(d->t);
    d->t = NULL;
    d->n = 0;
}

/* Returns 0 on success. js must stay alive as long as the jdoc is used. */
static int json_parse(jdoc *d, const char *js, size_t len)
{
    int cap = 1024, n = 0, sp = 0;
    int stack_cap = 64;
    int *stack = malloc(sizeof(int) * stack_cap);
    char *keynext = malloc(stack_cap);
    jtok *t = malloc(sizeof(jtok) * cap);
    size_t i = 0;

    d->js = js; d->t = NULL; d->n = 0;
    if (!stack || !keynext || !t) { free(stack); free(keynext); free(t); return -1; }

#define J_NEW(ty, st) do { \
        if (n == cap) { cap *= 2; jtok *nt = realloc(t, sizeof(jtok) * cap); if (!nt) goto fail; t = nt; } \
        t[n].type = (ty); t[n].start = (int)(st); t[n].end = -1; t[n].size = 0; \
        if (sp > 0) { int p = stack[sp - 1]; \
            if (t[p].type == J_ARR) t[p].size++; \
            else if (keynext[sp - 1]) t[p].size++; } \
        n++; } while (0)

    for (i = 0; i < len; i++) {
        char c = js[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '{' || c == '[') {
            J_NEW(c == '{' ? J_OBJ : J_ARR, i);
            if (sp == stack_cap) {
                stack_cap *= 2;
                stack = realloc(stack, sizeof(int) * stack_cap);
                keynext = realloc(keynext, stack_cap);
                if (!stack || !keynext) goto fail;
            }
            stack[sp] = n - 1;
            keynext[sp] = 1;
            sp++;
        } else if (c == '}' || c == ']') {
            if (sp == 0) goto fail;
            sp--;
            t[stack[sp]].end = (int)i + 1;
        } else if (c == ',') {
            if (sp > 0) keynext[sp - 1] = 1;
        } else if (c == ':') {
            if (sp > 0) keynext[sp - 1] = 0;
        } else if (c == '"') {
            size_t st = ++i;
            while (i < len && js[i] != '"') { if (js[i] == '\\') i++; i++; }
            J_NEW(J_STR, st);
            t[n - 1].end = (int)i;
        } else {
            size_t st = i;
            while (i < len && js[i] != ',' && js[i] != '}' && js[i] != ']' &&
                   js[i] != ' ' && js[i] != '\n' && js[i] != '\r' && js[i] != '\t') i++;
            J_NEW(J_PRIM, st);
            t[n - 1].end = (int)i;
            i--;
        }
    }
#undef J_NEW
    free(stack);
    free(keynext);
    d->t = t;
    d->n = n;
    return n > 0 ? 0 : -1;
fail:
    free(stack);
    free(keynext);
    free(t);
    return -1;
}

/* index of the token after the subtree that starts at i */
static int jskip(const jdoc *d, int i)
{
    const jtok *t = &d->t[i];
    int j = i + 1, k;
    if (t->type == J_OBJ) {
        for (k = 0; k < t->size; k++) { j = jskip(d, j); j = jskip(d, j); }
    } else if (t->type == J_ARR) {
        for (k = 0; k < t->size; k++) j = jskip(d, j);
    }
    return j;
}

static int jeq(const jdoc *d, int i, const char *s)
{
    size_t l = strlen(s);
    return d->t[i].type == J_STR && (size_t)(d->t[i].end - d->t[i].start) == l &&
           !strncmp(d->js + d->t[i].start, s, l);
}

/* value token of obj[key], or -1 */
static int jget(const jdoc *d, int obj, const char *key)
{
    int k, j;
    if (obj < 0 || d->t[obj].type != J_OBJ) return -1;
    j = obj + 1;
    for (k = 0; k < d->t[obj].size; k++) {
        if (jeq(d, j, key)) return j + 1;
        j = jskip(d, j + 1);
    }
    return -1;
}

/* Nth item of an array */
static int jat(const jdoc *d, int arr, int idx)
{
    int k, j;
    if (arr < 0 || d->t[arr].type != J_ARR || idx >= d->t[arr].size) return -1;
    j = arr + 1;
    for (k = 0; k < idx; k++) j = jskip(d, j);
    return j;
}

static int jcount(const jdoc *d, int i)
{
    return (i >= 0 && (d->t[i].type == J_OBJ || d->t[i].type == J_ARR)) ? d->t[i].size : 0;
}

/* Copies a string token (unescaped) or a raw primitive into out. Returns 1 if i was valid. */
static int jstr(const jdoc *d, int i, char *out, size_t cap)
{
    const char *p, *e;
    size_t o = 0;
    if (cap) out[0] = 0;
    if (i < 0 || !cap) return 0;
    if (d->t[i].type == J_OBJ || d->t[i].type == J_ARR) return 0;
    p = d->js + d->t[i].start;
    e = d->js + d->t[i].end;
    if (d->t[i].type == J_PRIM && e - p == 4 && !strncmp(p, "null", 4)) return 1;
    while (p < e && o + 4 < cap) {
        if (*p == '\\' && p + 1 < e) {
            p++;
            switch (*p) {
            case 'n': case 't': case 'r': out[o++] = ' '; p++; break;
            case 'u': {
                unsigned cp = 0;
                int k;
                for (k = 1; k <= 4 && p + k < e; k++) {
                    char h = p[k];
                    cp = cp * 16 + (unsigned)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                p += 5;
                if (cp < 0x80) out[o++] = (char)cp;
                else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 63)); }
                else { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
                break; }
            default: out[o++] = *p++; break;
            }
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    return 1;
}

/* obj[key] as string; missing -> "" */
static int jgets(const jdoc *d, int obj, const char *key, char *out, size_t cap)
{
    return jstr(d, jget(d, obj, key), out, cap);
}

/* no atof in the Xbox C library: plain decimal parser (sign, digits, fraction, optional exponent) */
static double json_atof(const char *s)
{
    double v = 0, scale = 1;
    int neg = 0, exp = 0, eneg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); scale *= 10; s++; }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') { exp = exp * 10 + (*s - '0'); s++; }
        while (exp-- > 0) { if (eneg) scale *= 10; else v *= 10; }
    }
    v /= scale;
    return neg ? -v : v;
}

static double jnum(const jdoc *d, int i)
{
    char b[40];
    if (i < 0) return 0;
    jstr(d, i, b, sizeof(b));
    return json_atof(b);
}

static int jtrue(const jdoc *d, int i)
{
    return i >= 0 && d->t[i].type == J_PRIM && d->js[d->t[i].start] == 't';
}

#endif
