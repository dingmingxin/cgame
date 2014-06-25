#include "saveload.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

#define error_assert(...)
#define error(...)

struct Store
{
    char *name;
    char *data;

    Store *child;
    Store *parent;
    Store *sibling;

    Store *iterchild; /* next child to visit when NULL name */

    char *str; /* result of store_get_str(...) */
};

/* --- streams ------------------------------------------------------------- */

/* growable string stream */
typedef struct Stream Stream;
struct Stream
{
    char *buf;
    size_t pos;
};

static void _stream_init(Stream *sm)
{
    sm->pos = 0;
    sm->buf = malloc(sm->pos + 1);
    sm->buf[sm->pos] = '\0';
}

static void _stream_deinit(Stream *sm)
{
    free(sm->buf);
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

    sm->buf = realloc(sm->buf, new_pos + 1);
    vsprintf(sm->buf + sm->pos, fmt, ap1);
    sm->pos = new_pos;
    va_end(ap1);
}

static void _stream_scanf_(Stream *sm, const char *fmt, int *n, ...)
{
    va_list ap;

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
    if (s)
        _stream_printf(sm, "%d %s ", (int) strlen(s), s);
    else
        _stream_printf(sm, "-1 ");
}
static char *_stream_read_string(Stream *sm)
{
    char *s;
    int n, r;

    if (sscanf(&sm->buf[sm->pos], "%d %n", &n, &r) != 1)
        error("corrupt save");
    sm->pos += r;

    if (n < 0)
        return NULL;

    s = malloc(n + 1);
    strncpy(s, &sm->buf[sm->pos], n);
    sm->pos += n;
    s[n] = '\0';
    return s;
}

/* --- internals ----------------------------------------------------------- */

static Store *_store_new(Store *parent)
{
    Store *s = malloc(sizeof(Store));
    s->name = NULL;
    s->data = NULL;
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
        s->child= t;
    }

    free(s->name);
    free(s->data);
    free(s->str);
}

void _store_write(Store *s, unsigned int indent, Stream *sm)
{
    Store *c;

    /* opening brace */
    if (s->child)
        _stream_printf(sm, "%*s{\n%*s", indent, "", indent + 4, "");
    else
        _stream_printf(sm, "%*s{ ", indent, "");

    /* name, data */
    _stream_write_string(sm, s->name);
    _stream_write_string(sm, s->data);
    if (s->child)
        _stream_printf(sm, "\n");

    /* children */
    for (c = s->child; c; c = c->sibling)
        _store_write(c, indent + 4, sm);

    /* closing brace */
    if (s->child)
        _stream_printf(sm, "%*s}\n", indent, "");
    else
        _stream_printf(sm, "}\n");
}

Store *_store_read(Store *parent, Stream *sm)
{
    Store *s = _store_new(parent);

    /* opening brace */
    if (sm->buf[sm->pos] != '{')
        error("corrupt save");
    while (isspace(sm->buf[++sm->pos]));

    /* name, data */
    s->name = _stream_read_string(sm);
    s->data = _stream_read_string(sm);

    /* children */
    for (;;)
    {
        while (isspace(sm->buf[sm->pos]))
            ++sm->pos;

        /* end? */
        if (sm->buf[sm->pos] == '}')
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
    Store *s = _store_new(parent);
    s->name = malloc(strlen(name) + 1);
    strcpy(s->name, name);

    return *sp = s;
}

bool store_child_load(Store **sp, const char *name, Store *parent)
{
    Store *s;

    /* if NULL name, pick next iteration child and advance */
    if (!name)
    {
        s = parent->iterchild;
        if (parent->iterchild)
            parent->iterchild = parent->iterchild->sibling;
        return *sp = s;
    }

    /* search all children */
    for (s = parent->child; s && strcmp(s->name, name); s = s->sibling);
    return *sp = s;
}

/* --- open/close ---------------------------------------------------------- */

Store *store_open()
{
    return _store_new(NULL);
}

Store *store_open_str(const char *str)
{
    Stream sm = { (char *) str, 0 };
    return _store_read(NULL, &sm);
}
const char *store_write_str(Store *s)
{
    Stream sm[1];

    _stream_init(sm);
    _store_write(s, 0, sm);
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

static void _store_printf(Store *s, const char *fmt, ...)
{
    va_list ap1, ap2;
    unsigned int n;

    error_assert(!s->data, "store '%s' shouldn't already have data", s->name);

    va_start(ap1, fmt);
    va_copy(ap2, ap1);

    n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);

    s->data = malloc(n + 1);
    vsprintf(s->data, fmt, ap1);
    va_end(ap1);
}
static int _deserializer_scanf(Store *s, const char *fmt, ...)
{
    va_list ap;
    int r;

    error_assert(s->curr->data, "section '%s' should have data",
                 s->curr->name);

    va_start(ap, fmt);
    r = vsscanf(s->data, fmt, ap);
    va_end(ap);
    return r;
}

void string_save(const char **c, const char *n, Store *s)
{
    Store *t;

    if (store_child_save(&t, n, s))
    {
        t->data = malloc(strlen(*c) + 1);
        strcpy(t->data, *c);
    }
}
bool string_load(char **c, const char *n, const char *d, Store *s)
{
    Store *t;

    if (store_child_load(&t, n, s))
    {
        error_assert(t->data, "section '%s' should have data", t->name);
        *c = malloc(strlen(t->data) + 1);
        strcpy(*c, t->data);
        return true;
    }

    *c = malloc(strlen(d) + 1);
    strcpy(*c, d);
    return false;
}

#if 1

int main()
{
    Store *s, *d, *sprite_s, *pool_s, *elem_s;
    char *c;

    s = store_open();
    {
        if (store_child_save(&sprite_s, "sprite", s))
        {
            c = "hello, world";
            string_save(&c, "prop1", sprite_s);

            c = "hello, world ... again";
            string_save(&c, "prop2", sprite_s);

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

            if (store_child_load(&pool_s, "pool", sprite_s))
                while (store_child_load(&elem_s, NULL, pool_s))
                    printf("        %s\n", elem_s->name);
        }
    }
    store_close(d);

    return 0;
}

#endif
