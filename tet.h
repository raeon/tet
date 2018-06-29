//
// Created by joris on 6/23/18.
//

#ifndef TET_H
#define TET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>

//    ____ ___  _   _ _____ ___ ____
//   / ___/ _ \| \ | |  ___|_ _/ ___|
//  | |  | | | |  \| | |_   | | |  _
//  | |___ |_| | |\  |  _|  | | |_| |
//   \____\___/|_| \_|_|   |___\____|
//

// types
#define TET_TYPE_BYTE uint8_t
#define TET_TYPE_NUMBER int32_t
#define TET_TYPE_SIZE size_t

// tstate->jmps
// desc:    This array contains jump buffers used with longjmp when handling errors.
// fields:  _LEN is initial size
#define TET_STATE_JMPS_LEN 4

// tstate->objs
//      _LEN is initial size
//      _GROW is the growth factor
//      _SHRINK is the shrink factor
#define TET_STATE_OBJS_LEN 8
#define TET_STATE_OBJS_GROW(l) ((l) * 2)
#define TET_STATE_OBJS_SHRINK(l) ((l) / 2)

// tstate->ptrs
//      _LEN is initial size, change should not be needed unless changes
//           to allocation logic have been made.
#define TET_STATE_PTRS_LEN 4

// tframe->stack
// fields:  _LEN is initial size
//          _GROW is the growth factor
#define TET_FRAME_STACK_LEN 8
#define TET_FRAME_STACK_GROW(l) ((l) * 2)

// tet_strbuf
//      _LEN is initial size, used for formatting errors (from C).
#define TET_STRBUF_LEN 256

//   ____  _____ ____ _        _    ____  _____ ____
//  |  _ \| ____/ ___| |      / \  |  _ \| ____/ ___|
//  | | | |  _|| |   | |     / _ \ | |_) |  _| \___ \
//  | |_| | |___ |___| |___ / ___ \|  _ <| |___ ___) |
//  |____/|_____\____|_____/_/   \_\_| \_\_____|____/
//
typedef TET_TYPE_BYTE tmark;
typedef TET_TYPE_NUMBER tnum;
typedef TET_TYPE_SIZE tsize;

typedef enum tvaltype {
    TVAL_ERROR,
    TVAL_NUMBER,
    TVAL_SYMBOL,
    TVAL_STRING,
    TVAL_SEXPR,
    TVAL_QEXPR,
    TVAL_ENV,
    TVAL_FRAME,
    TVAL_BUILTIN,
    TVAL_LAMBDA,
} tvaltype;

typedef enum tobjtype {
    TMARK_STATE = 0b00,
    TMARK_ENV = 0b01,
    TMARK_FRAME = 0b10,
    TMARK_VALUE = 0b11,
} tobjtype;

#define TOBJ_MARK_OFFSET ((tmark) 6)
#define TOBJ_MARK_TYPE ((tmark) 0xC0)
#define TOBJ_MARK_VALUE ((tmark) 0x3F)
#define SETMARK(v, m) ((v)->mark = ((v)->mark & TOBJ_MARK_TYPE) + ((m) & TOBJ_MARK_VALUE))
#define SETMARKTYPE(v, t) ((v)->mark = (tmark)(t) << TOBJ_MARK_OFFSET)
#define GETMARK(v) ((v)->mark & TOBJ_MARK_VALUE)
#define GETMARKTYPE(v) ((v)->mark >> TOBJ_MARK_OFFSET)

typedef struct tstate tstate;
typedef struct tobj tobj;
typedef struct tenv tenv;
typedef struct tframe tframe;
typedef struct tval tval;
typedef tsize (*tbuiltin)(tframe *f);

//    ____ _     ___  ____    _    _     ____
//   / ___| |   / _ \| __ )  / \  | |   / ___|
//  | |  _| |  | | | |  _ \ / _ \ | |   \___ \
//  | |_| | |___ |_| | |_) / ___ \| |___ ___) |
//   \____|_____\___/|____/_/   \_\_____|____/
//

static tval tet_memerr;
static char tet_strbuf[TET_STRBUF_LEN];

// Primitive memory functions.
void *talloc(tsize l);
void *trealloc(void *p, tsize l);
void tfree(void *p);

// Error-throwing memory functions.
void *tealloc(tstate *s, tsize l);
void *terealloc(tstate *s, void *p, tsize l);

// Registering memory functions.
void *tralloc(tstate *s, tsize l); // allocate and register memory
void trclean(tstate *s); // free() all registered memory
void trforget(tstate *s, tsize l); // forget last 'l' memory registrations

// Otherwise uncategorized functions.
char *tvaltype_print(tvaltype t);

//   __  __    _    ____ ____   ___  ____
//  |  \/  |  / \  / ___|  _ \ / _ \/ ___|
//  | |\/| | / _ \| |   | |_) | | | \___ \
//  | |  | |/ ___ \ |___|  _ <| |_| |___) |
//  |_|  |_/_/   \_\____|_| \_\\___/|____/
//

// Error handling
#define TET_CATCH(s, e, expr) \
    if (setjmp((s)->jmps[(s)->jmpi++].buf)) {\
        tsize i = (s)->jmpi;\
        tval *(e) = (tval*) (s)->jmps[i].val;\
        trclean(s); expr;\
    }
#define TET_UNCATCH(s) (s)->jmpi--
#define TET_THROWRAW(s, e) {\
        tsize i = --(s)->jmpi;\
        (s)->jmps[i].val = (void*) (e);\
        longjmp((s)->jmps[i].buf, 1);\
    }
#define TET_THROW(s, fmt, ...) {\
    tval *err = tval_err((s), fmt, ##__VA_ARGS__);\
    printf("THROW: %p (err tval*)\n", err);\
    TET_THROWRAW((s), err);\
    }

// Helpers
#define GC_HEADER() tmark mark;

//   ____ _____  _  _____ _____
//  / ___|_   _|/ \|_   _| ____|
//  \___ \ | | / _ \ | | |  _|
//   ___) || |/ ___ \| | | |___
//  |____/ |_/_/   \_\_| |_____|
//
struct tstate {
    GC_HEADER();

    tenv *env;
    tframe *frame;

    // A small 'jump stack' used for (nested) error handling.
    struct {
        void *val;
        jmp_buf buf;
    } jmps[TET_STATE_JMPS_LEN];
    tmark jmpi;

    // All pointers to non-garbage-collectable objects
    // (used to free memory if object construction fails halfway).
    void *ptrs[TET_STATE_PTRS_LEN];
    tmark ptri;

    // All known garbage-collectable objects.
    tobj **objs;
    tsize obji;
    tsize objl;
};

// Generic GC-able struct
struct tobj {
    GC_HEADER();
};

tstate *tstate_new();
void tstate_del(tstate *s);
tsize tstate_mark(tstate *s, tmark m);

tsize tstate_gc(tstate *s);
void tstate_gc_obj(tstate *s, tobj *o);

void tstate_track(tstate *s, tobj *o);
void tstate_untrack(tstate *s, tobj *o);

//   _____ _   ___     __
//  | ____| \ | \ \   / /
//  |  _| |  \| |\ \ / /
//  | |___| |\  | \ V /
//  |_____|_| \_|  \_/
//
struct tenv {
    GC_HEADER();
    tstate *state;

    tenv *prev;
    tval *vars;
};

tenv *tenv_new(tstate *s);
void tenv_del(tenv *e);
tsize tenv_mark(tenv *e, tmark m);

tval *tenv_get(tenv *e, tval *k);
tval *tenv_getpair(tenv *e, tval *k);
tval *tenv_set(tenv *e, tval *k, tval *v);
tval *tenv_put(tenv *e, tval *k, tval *v);

//   _____ ____     _    __  __ _____
//  |  ___|  _ \   / \  |  \/  | ____|
//  | |_  | |_) | / _ \ | |\/| |  _|
//  |  _| |  _ < / ___ \| |  | | |___
//  |_|   |_| \_\_/   \_\_|  |_|_____|
//
struct tframe {
    GC_HEADER();

    tframe *orig; // stack that resumed this stack
    tframe *prev; // previous frame in this stack
    tenv *env;
    tval *ip;
    tval *vp;

    tval **objs;
    tsize obji;
    tsize objl;
};

tframe *tframe_new(tenv *e);
void tframe_del(tframe *f);
tsize tframe_mark(tframe *f, tmark m);

void tframe_push(tframe *f, tval *v);
tval *tframe_pop(tframe *f);
tval *tframe_peek(tframe *f);
tval *tframe_get(tframe *f, tsize i);
tsize tframe_size(tframe *f);

// Stack get*-functions.
tsize tet_getn(tframe *f);
tval *tet_gettype(tframe *f, tsize i, tvaltype t);


// Stack functions.

#define TET_STACKFUNCD(r, n) \
    r tet_get##n(tframe *f, tsize i);\
    bool tet_is##n(tframe *f, tsize i);\
    void tet_push##n(tframe *f, r v);\
    r tet_pop##n(tframe *f);

#define TET_STACKFUNC(r, n, c, t, g) \
    r tet_get##n(tframe *f, tsize i) {\
        return tet_gettype(f, i, (t))->g;\
    }\
    bool tet_is##n(tframe *f, tsize i) {\
        return tframe_get(f, i)->type == (t);\
    }\
    void tet_push##n(tframe *f, r v) {\
        tframe_push(f, tval_##c(f->env->state, v));\
    }\
    r tet_pop##n(tframe *f) {\
        tval *v = tet_get##n(f, f->obji - 1); \
        --f->obji; \
        return v;\
    }

#define TET_STACKFUNCD_DUAL(r, n, t1, t2) \
    r tet_get##n(tframe *f, tsize i);\
    bool tet_is##n(tframe *f, tsize i);\
    void tet_push##n(tframe *f, t1 a, t2 b);\
    r tet_pop##n(tframe *f);

#define TET_STACKFUNC_DUAL(r, n, c, t, t1, t2) \
    r tet_get##n(tframe *f, tsize i) {\
        return tet_gettype(f, i, (t));\
    }\
    bool tet_is##n(tframe *f, tsize i) {\
        return tframe_get(f, i)->type == (t);\
    }\
    void tet_push##n(tframe *f, t1 a, t2 b) {\
        tframe_push(f, tval_##c(f->env->state, a, b));\
    }\
    r tet_pop##n(tframe *f) {\
        tval *v = tet_get##n(f, f->obji - 1); \
        --f->obji; \
        return v;\
    }

TET_STACKFUNCD(char*, error);
TET_STACKFUNCD(tnum, number);
TET_STACKFUNCD(char*, symbol);
TET_STACKFUNCD_DUAL(tval*, sexpr, tval*, tval*);
TET_STACKFUNCD_DUAL(tval*, qexpr, tval*, tval*);
TET_STACKFUNCD(tbuiltin, builtin);
TET_STACKFUNCD_DUAL(tval*, lambda, tval*, tval*);

//  __     ___    _    _   _ _____
//  \ \   / / \  | |  | | | | ____|
//   \ \ / / _ \ | |  | | | |  _|
//    \ V / ___ \| |___ |_| | |___
//     \_/_/   \_\_____\___/|_____|
//
struct tval {
    GC_HEADER();

    tvaltype type;
    union {
        char *err; // ERROR
        tnum num; // NUMBER
        char *sym; // SYMBOL
        char *str; // STRING
        struct {
            tval *car;
            tval *cdr;
        }; // SEXPR / QEXPR
        tenv *env; // ENV
        tframe *frame; // FRAME
        tbuiltin builtin; // BUILTIN
        struct {
            tval *pars;
            tval *body;
        }; // LAMBDA;
    };
};

tval *tval_new(tstate *s, tvaltype t);
void tval_del(tstate *s, tval *v);
tsize tval_mark(tval *v, tmark m);

tval *tval_err(tstate *s, char *fmt, ...);
tval *tval_num(tstate *s, tnum num);
tval *tval_sym(tstate *s, char *sym);
tval *tval_str(tstate *s, char *str);
tval *tval_sexpr(tstate *s, tval *car, tval *cdr);
tval *tval_qexpr(tstate *s, tval *car, tval *cdr);
tval *tval_builtin(tstate *s, tbuiltin builtin);
tval *tval_lambda(tstate *s, tval *pars, tval *body);

void tval_print(tval *v);

//   _______     ___    _
//  | ____\ \   / / \  | |
//  |  _|  \ \ / / _ \ | |
//  | |___  \ V / ___ \| |___
//  |_____|  \_/_/   \_\_____|
//

#define DIGITP(c) ((c) >= '0' && (c) <= '9')
#define BLANKP(c) ((c) == '\n' || (c) == '\r' || (c) == ' ')
#define CLOSEP(c) ((c) == ')' || (c) == '}')
#define EOFP(c) ((c) == '\0')

tval *tet_eval(tstate *s, tframe *f);
tframe *tet_read(tstate *s, char *in);

tval *tet_parse(tstate *s, char *in, tsize *i);
tval *tet_parse_num(tstate *s, char *in, tsize *i);
tval *tet_parse_sym(tstate *s, char *in, tsize *i);
tval *tet_parse_str(tstate *s, char *in, tsize *i);
tval *tet_parse_sexpr(tstate *s, char *in, tsize *i);
tval *tet_parse_qexpr(tstate *s, char *in, tsize *i);

//   ____  _   _ ___ _   _____ ___ _   _ ____
//  | __ )| | | |_ _| | |_   _|_ _| \ | / ___|
//  |  _ \| | | || || |   | |  | ||  \| \___ \
//  | |_) | |_| || || |___| |  | || |\  |___) |
//  |____/ \___/|___|_____|_| |___|_| \_|____/
//

tsize builtin_car(tframe *f);
tsize builtin_cdr(tframe *f);
tsize builtin_lambda(tframe *f);
tsize builtin_add(tframe *f);
tsize builtin_sub(tframe *f);
tsize builtin_mul(tframe *f);
tsize builtin_div(tframe *f);

#endif //TET_H
