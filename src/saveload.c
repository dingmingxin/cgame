#include "saveload.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

#include "error.h"

/* growable string stream */
typedef struct Stream Stream;
struct Stream
{
    char *buf;
    size_t pos; /* next char to read, or next pos to write to */
    size_t cap; /* allocated size of buf */
};

struct Store
{
    char *name;
    Stream sm[1];
    bool compressed;

    Store *child;
    Store *parent;
    Store *sibling;

    Store *iterchild; /* next child to visit when NULL name */

    char *str; /* result of store_get_str(...) */
};

/* --- streams ------------------------------------------------------------- */

static void _stream_init(Stream *sm)
{
    sm->buf = NULL;
    sm->pos = 0;
    sm->cap = 0;
}

static void _stream_deinit(Stream *sm)
{
    free(sm->buf);
}

/* grow so that pos is within allocated space */
static void _stream_grow(Stream *sm, size_t pos)
{
    if (pos >= sm->cap)
    {
        if (sm->cap < 2)
            sm->cap = 2;
        while (pos >= sm->cap)
            sm->cap <<= 1;
        sm->buf = realloc(sm->buf, sm->cap);
    }
}

/* writes at pos, truncates to end of written string */
static void _stream_printf(Stream *sm, const char *fmt, ...)
{
    va_list ap1, ap2;
    size_t new_pos;

    va_start(ap1, fmt);
    va_copy(ap2, ap1);

    new_pos = sm->pos + vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);

    _stream_grow(sm, new_pos);
    vsprintf(sm->buf + sm->pos, fmt, ap1);
    sm->pos = new_pos;
    va_end(ap1);
}

static void _stream_scanf_(Stream *sm, const char *fmt, int *n, ...)
{
    va_list ap;

    error_assert(sm->buf, "stream buffer should be initialized");

    /*
     * scanf is tricky because we need to move forward by number of
     * scanned characters -- *n will store number of characters read,
     * needs to also be put at end of parameter list (see
     * _stream_scanf(...) macro), and "%n" needs to be appended at
     * end of original fmt
     */

    va_start(ap, n);
    vsscanf(&sm->buf[sm->pos], fmt, ap);
    va_end(ap);
    sm->pos += *n;
}
#define _stream_scanf(sm, fmt, ...)                                     \
    do                                                                  \
    {                                                                   \
        int n_read__;                                                   \
        _stream_scanf_(sm, fmt "%n", &n_read__, ##__VA_ARGS__, &n_read__); \
    } while (0)

/* strings are written as "<len> <str> " or "-1 " if NULL */
static void _stream_write_string(Stream *sm, const char *s)
{
    /* NULL? */
    if (!s)
    {
        _stream_printf(sm, "n ");
        return;
    }

    _stream_printf(sm, "\"");

    for (; *s; ++s)
    {
        _stream_grow(sm, sm->pos);

        /* escape quotes */
        if (*s == '"')
        {
            sm->buf[sm->pos++] = '\\';
            _stream_grow(sm, sm->pos);
        }

        sm->buf[sm->pos++] = *s;
    }

    _stream_printf(sm, "\" ");
}
/* store allocated length in plen if non-NULL */
static char *_stream_read_string_(Stream *sm, size_t *plen)
{
    Stream rm[1];

    /* NULL? */
    if (sm->buf[sm->pos] == 'n')
    {
        if (strncmp(&sm->buf[sm->pos], "n ", 2))
            error("corrupt save");
        sm->pos += 2;
        return NULL;
    }

    _stream_init(rm);

    /* opening quote */
    if (sm->buf[sm->pos] != '"')
        error("corrupt save");
    ++sm->pos;

    while (sm->buf[sm->pos] != '"')
    {
        _stream_grow(rm, rm->pos);

        /* escape quotes */
        if (sm->buf[sm->pos] == '\\' && sm->buf[sm->pos + 1] == '"')
        {
            rm->buf[rm->pos++] = '"';
            sm->pos += 2;
        }
        else
            rm->buf[rm->pos++] = sm->buf[sm->pos++];
    }
    sm->pos += 2; /* closing quote, space */

    _stream_grow(rm, rm->pos);
    rm->buf[rm->pos] = '\0';

    if (plen)
        *plen = rm->cap;

    return rm->buf;
}
#define _stream_read_string(sm) _stream_read_string_(sm, NULL)

/* --- internals ----------------------------------------------------------- */

static Store *_store_new(Store *parent)
{
    Store *s = malloc(sizeof(Store));

    s->name = NULL;
    _stream_init(s->sm);
    s->compressed = false;

    s->parent = parent;
    s->child = NULL;
    s->sibling = s->parent ? s->parent->child : NULL;
    if (s->parent)
        s->parent->iterchild = s->parent->child = s;

    s->iterchild = NULL;
    s->str = NULL;

    return s;
}

static void _store_free(Store *s)
{
    Store *t;

    while (s->child)
    {
        t = s->child->sibling;
        _store_free(s->child);
        s->child = t;
    }

    free(s->name);
    _stream_deinit(s->sm);
    free(s->str);

    free(s);
}

/* { ... } for normal, [ ... ] for compressed */

static void _store_write(Store *s, Stream *sm)
{
    Store *c;

    _stream_printf(sm, s->compressed ? "[ " : "{ ");
    _stream_write_string(sm, s->name);
    _stream_write_string(sm, s->sm->buf);
    for (c = s->child; c; c = c->sibling)
        _store_write(c, sm);
    _stream_printf(sm, s->compressed ? "] " : "} ");
}

#define INDENT 2

static void _store_write_pretty(Store *s, unsigned int indent, Stream *sm)
{
    Store *c;

    /* compressed stuff isn't pretty */
    if (s->compressed)
    {
        _stream_printf(sm, "%*s", indent, "");
        _store_write(s, sm);
        _stream_printf(sm, "\n");
        return;
    }

    /* opening brace */
    _stream_printf(sm, "%*s{ ", indent, "");

    /* name, data */
    _stream_write_string(sm, s->name);
    _stream_write_string(sm, s->sm->buf);
    if (s->child)
        _stream_printf(sm, "\n");

    /* children */
    for (c = s->child; c; c = c->sibling)
        _store_write_pretty(c, indent + INDENT, sm);

    /* closing brace */
    if (s->child)
        _stream_printf(sm, "%*s}\n", indent, "");
    else
        _stream_printf(sm, "}\n");
}

static Store *_store_read(Store *parent, Stream *sm)
{
    char close_brace = '}'; /* type of close brace to expect */
    Store *s = _store_new(parent);

    /* opening brace */
    if (sm->buf[sm->pos] == '[')
    {
        s->compressed = true;
        close_brace = ']';
    }
    else if (sm->buf[sm->pos] != '{')
        error("corrupt save");
    while (isspace(sm->buf[++sm->pos]));

    /* name, data */
    s->name = _stream_read_string(sm);
    s->sm->buf = _stream_read_string_(sm, &s->sm->cap);
    s->sm->pos = 0;

    /* children */
    for (;;)
    {
        while (isspace(sm->buf[sm->pos]))
            ++sm->pos;

        /* end? */
        if (sm->buf[sm->pos] == close_brace)
        {
            ++sm->pos;
            break;
        }

        _store_read(s, sm);
    }

    return s;
}

/* --- child save/load ----------------------------------------------------- */

bool store_child_save(Store **sp, const char *name, Store *parent)
{
    Store *s;

    /* compressed? keep it flat */
    if (parent->compressed)
        return (*sp = parent) != NULL;

    s = _store_new(parent);
    if (name)
    {
        s->name = malloc(strlen(name) + 1);
        strcpy(s->name, name);
    }
    return (*sp = s) != NULL;
}

bool store_child_save_compressed(Store **sp, const char *name, Store *parent)
{
    bool r = store_child_save(sp, name, parent);
    (*sp)->compressed = true;
    return r;
}

bool store_child_load(Store **sp, const char *name, Store *parent)
{
    Store *s;

    /* compressed? expect flat */
    if (parent->compressed)
        return (*sp = parent) != NULL;

    /* if NULL name, pick next iteration child and advance */
    if (!name)
    {
        s = parent->iterchild;
        if (parent->iterchild)
            parent->iterchild = parent->iterchild->sibling;
        return (*sp = s) != NULL;
    }

    /* search all children */
    for (s = parent->child; s && (!s->name || strcmp(s->name, name));
         s = s->sibling);
    return (*sp = s) != NULL;
}

/* --- open/close ---------------------------------------------------------- */

Store *store_open()
{
    return _store_new(NULL);
}

Store *store_open_str(const char *str)
{
    Stream sm = { (char *) str, 0, 0 };
    return _store_read(NULL, &sm);
}
const char *store_write_str(Store *s)
{
    Stream sm[1];

    _stream_init(sm);
    _store_write_pretty(s, 0, sm);
    free(s->str);
    s->str = sm->buf;
    return s->str; /* don't deinit sm, keep string */
}

/* file stores store_write_str(...) result in "<len>\n<str>" format */
Store *store_open_file(const char *filename)
{
    Store *s;
    FILE *f;
    unsigned int n;
    char *str;

    f = fopen(filename, "r");
    error_assert(f, "file '%s' must be open for reading", filename);

    fscanf(f, "%u\n", &n);
    str = malloc(n + 1);
    fread(str, 1, n, f);
    fclose(f);
    str[n] = '\0';
    s = store_open_str(str);
    free(str);
    return s;
}
void store_write_file(Store *s, const char *filename)
{
    FILE *f;
    const char *str;
    unsigned int n;

    f = fopen(filename, "w");
    error_assert(f, "file '%s' must be open for writing", filename);

    str = store_write_str(s);
    n = strlen(str);
    fprintf(f, "%u\n", n);
    fwrite(str, 1, n, f);
    fclose(f);
}

void store_close(Store *s)
{
    _store_free(s);
}

/* --- primitives ---------------------------------------------------------- */

#define _store_printf(s, fmt, ...) \
    _stream_printf(s->sm, fmt, ##__VA_ARGS__)
#define _store_scanf(s, fmt, ...) \
    _stream_scanf(s->sm, fmt, ##__VA_ARGS__)

/* use 'i' for infinity */
void scalar_save(const Scalar *f, const char *n, Store *s)
{
    Store *t;

    if (store_child_save(&t, n, s))
    {
        if (*f == SCALAR_INFINITY)
            _store_printf(t, "i ");
        else
            _store_printf(t, "%f ", *f);
    }
}
bool scalar_load(Scalar *f, const char *n, Scalar d, Store *s)
{
    Store *t;

    if (store_child_load(&t, n, s))
    {
        if (t->sm->buf[t->sm->pos] == 'i')
        {
            *f = SCALAR_INFINITY;
            _store_scanf(t, "i ");
        }
        else
            _store_scanf(t, "%f ", f);
        return true;
    }

    *f = d;
    return false;
}

void uint_save(const unsigned int *u, const char *n, Store *s)
{
    Store *t;

    if (store_child_save(&t, n, s))
        _store_printf(t, "%u ", *u);
}
bool uint_load(unsigned int *u, const char *n, unsigned int d, Store *s)
{
    Store *t;

    if (store_child_load(&t, n, s))
        _store_scanf(t, "%u ", u);
    else
        *u = d;
    return t != NULL;
}

void int_save(const int *i, const char *n, Store *s)
{
    Store *t;

    if (store_child_save(&t, n, s))
        _store_printf(t, "%d ", *i);
}
bool int_load(int *i, const char *n, int d, Store *s)
{
    Store *t;

    if (store_child_load(&t, n, s))
        _store_scanf(t, "%d ", i);
    else
        *i = d;
    return t != NULL;
}

void bool_save(const bool *b, const char *n, Store *s)
{
    Store *t;

    if (store_child_save(&t, n, s))
        _store_printf(t, "%d ", (int) *b);
}
bool bool_load(bool *b, const char *n, bool d, Store *s)
{
    int i = d;
    Store *t;

    if (store_child_load(&t, n, s))
        _store_scanf(t, "%d ", &i);
    *b = i;
    return t != NULL;
}

void string_save(const char **c, const char *n, Store *s)
{
    Store *t;

    if (store_child_save(&t, n, s))
        _stream_write_string(t->sm, *c);
}
bool string_load(char **c, const char *n, const char *d, Store *s)
{
    Store *t;

    if (store_child_load(&t, n, s))
    {
        *c = _stream_read_string(t->sm);
        return true;
    }

    if (d)
    {
        *c = malloc(strlen(d) + 1);
        strcpy(*c, d);
    }
    else
        *c = NULL;
    return false;
}

#if 0

int main()
{
    Store *s, *d, *sprite_s, *pool_s, *elem_s;
    char *c;
    Scalar r;

    s = store_open();
    {
        if (store_child_save(&sprite_s, "sprite", s))
        {
            c = "hello, world";
            string_save(&c, "prop1", sprite_s);

            c = "hello, world ... again";
            string_save(&c, "prop2", sprite_s);

            r = SCALAR_INFINITY;
            scalar_save(&r, "prop6", sprite_s);

            if (store_child_save(&pool_s, "pool", sprite_s))
            {
                store_child_save(&elem_s, "elem1", pool_s);
                store_child_save(&elem_s, "elem2", pool_s);
            }
        }
    }
    store_write_file(s, "test.sav");
    store_close(s);

    /* ---- */

    d = store_open_file("test.sav");
    {
        if (store_child_load(&sprite_s, "sprite", d))
        {
            printf("%s\n", sprite_s->name);

            string_load(&c, "prop1", "hai", sprite_s);
            printf("    prop1: %s\n", c);

            string_load(&c, "prop3", "hai", sprite_s);
            printf("    prop3: %s\n", c);

            string_load(&c, "prop2", "hai", sprite_s);
            printf("    prop2: %s\n", c);

            scalar_load(&r, "prop6", 4.2, sprite_s);
            printf("    prop6: %f\n", r);

            if (store_child_load(&pool_s, "pool", sprite_s))
                while (store_child_load(&elem_s, NULL, pool_s))
                    printf("        %s\n", elem_s->name);
        }
    }
    store_close(d);

    return 0;
}

#endif
