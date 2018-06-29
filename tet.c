//
// Created by joris on 6/23/18.
//

#include <stdarg.h>
#include <stdlib.h>
#include <memory.h>
#include <stdio.h>
#include <err.h>
#include "tet.h"

//    ____ _     ___  ____    _    _     ____
//   / ___| |   / _ \| __ )  / \  | |   / ___|
//  | |  _| |  | | | |  _ \ / _ \ | |   \___ \
//  | |_| | |___ |_| | |_) / ___ \| |___ ___) |
//   \____|_____\___/|____/_/   \_\_____|____/
//

// @formatter:off because CLion wants to remove the indents
static tval tet_memerr = {
    .type = TVAL_ERROR,
    .err = "out of memory"
};
// @formatter:on

// Primitive memory functions.
inline void *talloc(tsize l) {
    return malloc(l);
}

inline void *trealloc(void *p, tsize l) {
    return realloc(p, l);
}

inline void tfree(void *p) {
    return free(p);
}

// Error-checking memory functions.
void *tealloc(tstate *s, tsize l) {
    void *p = talloc(l);
    if (!p) {
        TET_THROWRAW(s, &tet_memerr);
    }
    return p;
}

void *terealloc(tstate *s, void *p, tsize l) {
    void *n = trealloc(p, l);
    if (!n) {
        TET_THROWRAW(s, &tet_memerr);
    }
    return n;
}

// Registering and error-throwing memory functions.
void *tralloc(tstate *s, tsize l) {
    void *p = tealloc(s, l);
    s->ptrs[s->ptri++] = p;
    return p;
}

void trclean(tstate *s) {
    while (s->ptri) {
        tfree(s->ptrs[--s->ptri]);
    }
}

inline void trforget(tstate *s, tsize l) {
    s->ptri -= l;
}

// Otherwise uncategorized functions.
char *tvaltype_print(tvaltype t) {
    switch (t) {
        case TVAL_ERROR:
            return "ERROR";
        case TVAL_NUMBER:
            return "NUMBER";
        case TVAL_SYMBOL:
            return "SYMBOL";
        case TVAL_STRING:
            return "STRING";
        case TVAL_SEXPR:
            return "SEXPR";
        case TVAL_QEXPR:
            return "QEXPR";
        case TVAL_ENV:
            return "ENV";
        case TVAL_FRAME:
            return "FRAME";
        case TVAL_BUILTIN:
            return "BUILTIN";
        case TVAL_LAMBDA:
            return "LAMBDA";
    }
    return 0;
}

//   ____ _____  _  _____ _____
//  / ___|_   _|/ \|_   _| ____|
//  \___ \ | | / _ \ | | |  _|
//   ___) || |/ ___ \| | | |___
//  |____/ |_/_/   \_\_| |_____|
//

tstate *tstate_new() {
    // The only allocation that does not jmp on failure within tet.
    tstate *s = talloc(sizeof(tstate));
    if (!s) {
        return NULL;
    }

    // Properly null-initialize critical fields.
    s->env = NULL;
    s->frame = NULL;
    s->jmpi = 0;
    s->ptri = 0;

    // Once we've allocated the tstate struct, we can now use it to store our pointers to
    // non-collected memory. Then, should an error occur (e.g. allocation failure),
    // we can easily free the memory again. This is done by the TET_CATCH macro.
    TET_CATCH(s, e, {
        tfree(s); // Primitive free(), the only case of this happening.
        return NULL;
    });

    // List of garbage-collectable objects.
    s->objs = tralloc(s, TET_STATE_OBJS_LEN * sizeof(tval *));
    s->obji = 0;
    s->objl = TET_STATE_OBJS_LEN;

    // Somewhat pointless as nothing will try to GC this object (would it be garbage
    // collecting itself?), but for correctness we will include this.
    SETMARKTYPE(s, TMARK_STATE);

    s->env = tenv_new(s);
    s->frame = NULL;

    // Beyond this point we could not return 'into' this stack, so we remove our error
    // handler here. It is the users' responsibility to specify a top-level error handler
    // if they perform any unsafe operations (e.g. defining builtins, parsing, ...).
    TET_UNCATCH(s);
    trforget(s, 1); // s->objs
    return s;
}

void tstate_del(tstate *s) {
    // Delete every object (which can all be garbage-collected!) known to this tstate.
    // This actually includes s->frame and s->env.
    while (s->obji) {
        tstate_gc_obj(s, s->objs[0]);
    }

    // Free the only remaining array, and lastly the tstate itself.
    tfree(s->objs);
    tfree(s);
}

tsize tstate_mark(tstate *s, tmark m) {
    if (!s) return 0;
    if (GETMARK(s) == m) {
        return 0;
    }

    // Update our mark without interfering with the type bits.
    SETMARK(s, m);

    // Mark our env and active frame, if any.
    tsize c = 1;
    if (s->env) {
        c += tenv_mark(s->env, m);
    }
    if (s->frame) {
        c += tframe_mark(s->frame, m);
    }

    return c;
}

tsize tstate_gc(tstate *s) {

    // Pick the new mark.
    tmark nm = (tmark) (GETMARK(s) + (tmark) 1) & TOBJ_MARK_VALUE;

    // Mark objects.
    tsize c = tstate_mark(s, nm);

    // If we marked as many as we expected, then we need not sweep.
    if (c == s->obji + 1) {
        return 0;
    }

    // Otherwise, sweep.
    c = 0;
    for (tsize i = 0; i < s->obji; i++) {
        tobj *o = s->objs[i];
        tmark om = GETMARK(o);
        if (om == nm) continue;
        tstate_gc_obj(s, o);
        c++;
        i--;
    }

    printf("gc: %zu\n", c);
    return c;
}

void tstate_gc_obj(tstate *s, tobj *o) {
    tmark t = GETMARKTYPE(o);
    switch (t) {
        case TMARK_ENV:
            tenv_del((tenv *) o);
            tstate_untrack(s, o);
            break;
        case TMARK_FRAME:
            tframe_del((tframe *) o);
            tstate_untrack(s, o);
            break;
        case TMARK_VALUE:
            tval_del(s, (tval *) o);
            tstate_untrack(s, o);
            break;
        default: TET_THROW(s, "bad marker type: %u", t);
    }
}

void tstate_track(tstate *s, tobj *o) {

    // Grow the array if necessary. Will throw an error if it fails.
    if (s->obji >= s->objl) {
        s->objs = terealloc(s, s->objs, TET_STATE_OBJS_GROW(s->objl) * sizeof(tobj *));
        s->objl *= 2;
    }

    // Insert into the array.
    s->objs[s->obji++] = o;
}

void tstate_untrack(tstate *s, tobj *o) {
    tsize half;
    tsize quarter;

    // Loop the array until we find o.
    for (tsize i = 0; i < s->obji; i++) {
        if (s->objs[i] == o) {
            // Swap the last item into this slot, and decrement
            // the index pointer. This way we don't get gaps.
            s->objs[i] = s->objs[--s->obji];
            goto shrink;
        }
    }

    // We don't really care if we didn't find it.
    return;

    // We may want to shrink. We do this if we're using <= two shrinks (assuming default
    // growth/shrink multipliers) of our allocated space, assuming that one shrink is
    // greater than or equal to the initial capacity.
    shrink:
    half = TET_STATE_OBJS_SHRINK(s->objl);
    quarter = TET_STATE_OBJS_SHRINK(half);
    if (half >= TET_STATE_OBJS_LEN && quarter >= s->obji) {
        s->objs = terealloc(s, s->objs, half * sizeof(tobj *));
        s->objl = half;
    }
}


//   _____ _   ___     __
//  | ____| \ | \ \   / /
//  |  _| |  \| |\ \ / /
//  | |___| |\  | \ V /
//  |_____|_| \_|  \_/
//

tenv *tenv_new(tstate *s) {
    tenv *e = tralloc(s, sizeof(tenv));

    SETMARKTYPE(e, TMARK_ENV);
    e->state = s;
    e->prev = NULL;
    e->vars = NULL;

    tstate_track(s, (tobj *) e);
    trforget(s, 1); // e
    return e;
}

void tenv_del(tenv *e) {
    // TODO: Check if this is correct
    tstate_untrack(e->state, (tobj *) e);
    tfree(e);
}

tsize tenv_mark(tenv *e, tmark m) {
    if (!e) return 0;
    if (GETMARK(e) == m) {
        return 0;
    }

    SETMARK(e, m);
    tmark c = 0;
    while (e) {
        if (e->vars) {
            c += tval_mark(e->vars, m);
        }
        e = e->prev;
        c++;
    }
    return c;
}

tval *tenv_get(tenv *e, tval *k) {
    tval *p = tenv_getpair(e, k);
    if (!p) {
        return tval_err(e->state, "undefined symbol: %s", k->sym);
    }
    return p->cdr;
}

tval *tenv_getpair(tenv *e, tval *k) {
    while (e) {
        for (tval *c = e->vars; c != NULL; c = c->cdr) {
            tval *kv = c->car;
            if (strcmp(kv->car->sym, k->sym) == 0) {
                return kv;
            }
        }
        e = e->prev;
    }
    return NULL;
}

tval *tenv_set(tenv *e, tval *k, tval *v) {
    // Find a pair to modify in ANY env.
    tval *p = tenv_getpair(e, k);
    if (p) {
        p->car->cdr = v;
        return v;
    }

    // Create a new pair locally otherwise.
    return tenv_put(e, k, v);
}

tval *tenv_put(tenv *e, tval *k, tval *v) {

    // Overwrite if a pair in this env exists.
    for (tval *c = e->vars; c != NULL; c = c->cdr) {
        tval *kv = c->car;
        if (strcmp(kv->car->sym, k->sym) == 0) {
            kv->cdr = v;
            return v;
        }
    }

    // Otherwise, prepend a pair.
    tval *kv = tval_sexpr(e->state, k, v);
    tval *p = tval_sexpr(e->state, kv, e->vars);
    e->vars = p;

    return v;
}


//   _____ ____     _    __  __ _____
//  |  ___|  _ \   / \  |  \/  | ____|
//  | |_  | |_) | / _ \ | |\/| |  _|
//  |  _| |  _ < / ___ \| |  | | |___
//  |_|   |_| \_\_/   \_\_|  |_|_____|
//

tframe *tframe_new(tenv *e) {
    tframe *f = tralloc(e->state, sizeof(tframe));

    SETMARKTYPE(f, TMARK_FRAME);
    f->orig = NULL;
    f->prev = NULL;
    f->env = e;
    f->ip = NULL;
    f->vp = NULL;

    // Stack
    f->objs = tralloc(e->state, TET_FRAME_STACK_LEN * sizeof(tval *));
    f->obji = 0;
    f->objl = TET_FRAME_STACK_LEN;

    tstate_track(e->state, (tobj *) f);
    trforget(e->state, 2); // f, f->stack
    return f;
}

void tframe_del(tframe *f) {
    tstate_untrack(f->env->state, (tobj *) f);
    tfree(f->objs);
    tfree(f);
}

tsize tframe_mark(tframe *f, tmark m) {
    if (!f) return 0;
    tsize c = 0;
    while (f) {
        // No repetition please!
        if (GETMARK(f) == m) {
            return c;
        }

        c++;
        SETMARK(f, m);

        // Mark all values stored in this stack frame.
        for (tsize i = 0; i < f->obji; i++) {
            c += tval_mark(f->objs[i], m);
        }

        // If we have an initiating stack frame, mark that too.
        if (f->orig) {
            c += tframe_mark(f->orig, m);
        }

        // And loop down our stackframes.
        f = f->prev;
    }
    return c;
}

void tframe_push(tframe *f, tval *v) {

    // Grow the array if neccessary.
    if (f->obji >= f->objl) {
        tval **ns = terealloc(f->env->state, f->objs,
                              TET_FRAME_STACK_GROW(f->objl) * sizeof(tval *));
        f->objs = ns;
        f->objl *= 2;
    }

    // Insert item at the end.
    f->objs[f->obji++] = v;
}

tval *tframe_pop(tframe *f) {
    if (f->obji == 0) {
        return tval_err(f->env->state, "cannot pop tval from empty stackframe");
    }
    return f->objs[--f->obji];
}

tval *tframe_peek(tframe *f) {
    if (f->obji == 0) {
        return tval_err(f->env->state, "cannot peek tval from empty stackframe");
    }
    return f->objs[f->obji - 1];
}

tval *tframe_get(tframe *f, tsize i) {
    if (i >= f->obji) {
        TET_THROW(f->env->state, "attempt to access stack out of bounds: i=%zu, len=%zu",
                  i, f->obji);
    }
    return f->objs[i];
}

tsize tframe_size(tframe *f) {
    return f->obji;
}

tsize tet_getn(tframe *f) {
    return f->obji;
}

tval *tet_gettype(tframe *f, tsize i, tvaltype t) {
    tval *v = tframe_get(f, i);
    if (v->type != t) {
        TET_THROW(f->env->state, "type mismatch, got %s but expected %s",
                  tvaltype_print(v->type), tvaltype_print(t));
    }

    return v;
}

// Stack is*, get*, push* and pop* functions.
TET_STACKFUNC(char*, error, err, TVAL_ERROR, err);

TET_STACKFUNC(tnum, number, num, TVAL_NUMBER, num);

TET_STACKFUNC(char*, symbol, sym, TVAL_SYMBOL, sym);

TET_STACKFUNC_DUAL(tval*, sexpr, sexpr, TVAL_SEXPR, tval*, tval*);

TET_STACKFUNC_DUAL(tval*, qexpr, qexpr, TVAL_QEXPR, tval*, tval*);

TET_STACKFUNC(tbuiltin, builtin, builtin, TVAL_BUILTIN, builtin);

TET_STACKFUNC_DUAL(tval*, lambda, lambda, TVAL_LAMBDA, tval*, tval*);

//  __     ___    _    _   _ _____
//  \ \   / / \  | |  | | | | ____|
//   \ \ / / _ \ | |  | | | |  _|
//    \ V / ___ \| |___ |_| | |___
//     \_/_/   \_\_____\___/|_____|
//

tval *tval_new(tstate *s, tvaltype t) {
    tval *v = tralloc(s, sizeof(tval));

    SETMARKTYPE(v, TMARK_VALUE);
    v->type = t;
    v->car = NULL; // Biggest union is
    v->cdr = NULL; // a sexpr/qexpr.
    tstate_track(s, (tobj *) v);

    trforget(s, 1); // v
    return v;
}

void tval_del(tstate *s, tval *v) {
    tstate_untrack(s, (tobj *) v);
    switch (v->type) {
        case TVAL_STRING:
            tfree(v->str);
            break;
        case TVAL_ERROR:
            // Don't delete the memory error! TODO Determine if this check is necessary
            // if (v == &tet_memerr) return;
            tfree(v->err);
            break;
        case TVAL_SYMBOL:
            tfree(v->sym);
            break;
        default:
            break;
    }
    tfree(v);
}

tsize tval_mark(tval *v, tmark m) {
    if (!v) return 0;
    if (GETMARK(v) == m) {
        return 0;
    }

    SETMARK(v, m);
    switch (v->type) {
        case TVAL_SEXPR:
        case TVAL_QEXPR:
            return 1 + tval_mark(v->car, m) + tval_mark(v->cdr, m);
        case TVAL_ENV:
            return 1 + tenv_mark(v->env, m);
        case TVAL_FRAME:
            return 1 + tframe_mark(v->frame, m);
        case TVAL_LAMBDA:
            return 1 + tval_mark(v->pars, m) + tval_mark(v->body, m);
        default:
            return 1;
    }
}

tval *tval_err(tstate *s, char *fmt, ...) {
    tval *v = tval_new(s, TVAL_ERROR);

    // Format the string.
    va_list va;
    va_start(va, fmt);
    vsnprintf(tet_strbuf, TET_STRBUF_LEN - 1, fmt, va);
    va_end(va);

    // Copy the formatted error string.
    char *err = tealloc(s, strlen(tet_strbuf) + 1);
    strcpy(err, tet_strbuf);
    v->err = err;

    return v;
}

tval *tval_num(tstate *s, tnum num) {
    tval *v = tval_new(s, TVAL_NUMBER);
    v->num = num;
    return v;
}

tval *tval_sym(tstate *s, char *sym) {
    tval *v = tval_new(s, TVAL_SYMBOL);
    char *c = tealloc(s, strlen(sym) + 1);
    strcpy(c, sym);
    v->sym = c;
    return v;
}

tval *tval_str(tstate *s, char *str) {
    tval *v = tval_new(s, TVAL_STRING);
    char *c = tealloc(s, strlen(str) + 1);
    strcpy(c, str);
    v->str = c;
    return v;
}

tval *tval_sexpr(tstate *s, tval *car, tval *cdr) {
    tval *v = tval_new(s, TVAL_SEXPR);
    v->car = car;
    v->cdr = cdr;
    return v;
}

tval *tval_qexpr(tstate *s, tval *car, tval *cdr) {
    tval *v = tval_new(s, TVAL_QEXPR);
    v->car = car;
    v->cdr = cdr;
    return v;
}

tval *tval_builtin(tstate *s, tbuiltin builtin) {
    tval *v = tval_new(s, TVAL_BUILTIN);
    v->builtin = builtin;
    return v;
}

tval *tval_lambda(tstate *s, tval *pars, tval *body) {
    tval *v = tval_new(s, TVAL_LAMBDA);
    v->pars = pars;
    v->body = body;
    return v;
}

void tval_print(tval *v) {
    if (!v) {
        printf("nil");
        return;
    }

    switch (v->type) {
        case TVAL_SYMBOL:
        case TVAL_ERROR:
            printf("%s", v->str);
            break;
        case TVAL_STRING:
            printf("\"%s\"", v->str);
            break;
        case TVAL_NUMBER:
            printf("%u", v->num);
            break;
        case TVAL_SEXPR:
            printf("(");
            while (v) {
                tval_print(v->car);
                v = v->cdr;
                if (v) printf(" ");
            }
            printf(")");
            break;
        case TVAL_QEXPR:
            printf("{");
            while (v) {
                tval_print(v->car);
                v = v->cdr;
                if (v) printf(" ");
            }
            printf("}");
            break;
        case TVAL_BUILTIN:
            printf("<builtin>");
            break;
        case TVAL_LAMBDA:
            printf("<lambda ");
            tval_print(v->pars);
            printf(" ");
            tval_print(v->body);
            printf(">");
            break;
        default:
            break;
    }
}

//   _______     ___    _
//  | ____\ \   / / \  | |
//  |  _|  \ \ / / _ \ | |
//  | |___  \ V / ___ \| |___
//  |_____|  \_/_/   \_\_____|
//

tval *tet_eval(tstate *s, tframe *f) {

    // Catch any errors that may arise.
    TET_CATCH(s, err, {

        printf("caught %p\n", err);

        // Since we've left half-way through, we want to clean up our mess first.
        // The TET_CATCH macro already handles 'dangling memory' for us. Garbage collect!
        //tstate_gc(s);

        // TET_THROW always throws tvals of type TVAL_ERROR. We just return those.
        return err;
    });

    // Predeclare some intermediary variables. Nothing persistent.
    tenv *ne;
    tframe *nf;
    tval *nvr;
    tval *nvc;

    // Keep going at the current stack until it's completely unwound.
    while (f) {

        // Evaluate all values in this instruction.
        while (f->vp) {
            tval *v = f->vp->car;
            printf("evaluating: ");
            tval_print(v);
            printf("\n");

            // Throw an error if the SEXPR is malformed.
            if (!v) {
                TET_THROW(s, "nil in car of sexpr during eval");
            }

            // Evaluate the value.
            switch (v->type) {
                case TVAL_ERROR:
                    // If we somehow encounter an ERROR object, throw all protocol out the
                    // window and straight up return it. TODO Clean error handling
                    return v;

                case TVAL_NUMBER:
                case TVAL_STRING:
                case TVAL_BUILTIN:
                case TVAL_LAMBDA:
                    // Values that evaluate to themselves. Very boring indeed.
                    tframe_push(f, v);
                    break;

                case TVAL_SYMBOL:
                    // Symbols are to be resolved in the current environment.
                    tframe_push(f, tenv_get(f->env, v));
                    break;

                case TVAL_SEXPR:
                    // When we encounter a SEXPR, we go one stackframe deeper.

                    // Create the new stackframe.
                    nf = tframe_new(f->env);
                    nf->prev = f;
                    nf->vp = v; // Set the stackframe to evaluate this SEXPR.

                    // Move our value pointer and switch to the new stackframe.
                    f->vp = f->vp->cdr;
                    f = nf;
                    continue;

                case TVAL_QEXPR:
                    // When we encounter a QEXPR, we simply iterate over the entire list
                    // and convert each part from QEXPR to SEXPR. We don't even look at
                    // the contents, since we don't care (it's quoted!). We push the new
                    // SEXPR on the value stack.

                    // Loop over the QEXPR converting every part to SEXPR.
                    nvr = tval_sexpr(s, v->car, NULL);
                    nvc = nvr;
                    for (tval *c = v->cdr; c != NULL; c = c->cdr) {
                        nvc->cdr = tval_sexpr(s, c->car, NULL);
                        nvc = nvc->cdr;
                    }

                    // Push it on the stack et voila!
                    tframe_push(f, nvr);
                    break;

                default: TET_THROW(s, "illegal type: %u", v->type);
            }

            // Move to the next value.
            f->vp = f->vp->cdr;
        }

        // Once all values in this stackframe have been evaluated, we can start invoking
        // the builtin/lambda that is located at index 0 in the stack.

        tval *fn = f->obji ? f->objs[0] : NULL;

        // If we don't have a function (this was an empty list) then we return an empty
        // list to the callee.
        if (!fn) {
            if (!f->prev) {
                f->objs[0] = tval_sexpr(s, NULL, NULL);
                f->obji = 1;
            } else {
                tet_pushsexpr(f->prev, NULL, NULL);
            }
            continue;
        }

        if (fn->type == TVAL_BUILTIN) {
            // Fetch and invoke the builtin.
            tsize c = fn->builtin(f);

            // Check if there actually are 'c' values to return.
            if (f->obji < c) {
                TET_THROW(s, "builtin wants to return %zu values, but there are only "
                             "%zu values on the stack", c, f->obji);
            }

            // Return values if necessary.
            if (c) {
                if (!f->prev) {
                    // If there's no previous frame to push the return values on, then we
                    // will just use the current frame instead. We do this by moving the
                    // returned values to the front of the stack, overwriting the
                    // function and its arguments.
                    for (tsize i = 0; i < c; i++) {
                        f->objs[i] = f->objs[f->obji - c + i];
                    }
                    f->obji = c;
                } else {
                    // If there *is* a previous frame, then we happily push our results
                    // onto their value stack.
                    for (tsize i = f->obji - c; i < f->obji; i++) {
                        tframe_push(f->prev, f->objs[i]);
                    }
                }
            }
        } else if (fn->type == TVAL_LAMBDA) {
            // Fetch the lambda.
            tval *pars = fn->pars;
            tval *body = fn->body;

            // Lambda invocation requires a little more work. We need to create an env
            // and bind the arguments to the parameter names in it. This differs from the
            // builtin invocation, where only the stack is used to communicate values.
            ne = tenv_new(s);
            ne->prev = f->env;

            // With the environment created, we need to bind the arguments. We don't check
            // if there are too few or too many values provided for the number of expected
            // arguments. We just bind values to parameters until we run out of either.
            // NOTE: We start at index 1 to exclude the lambda itself.
            tval *par = pars;
            for (tsize i = 1; i < f->obji; i++) {
                tval *k = par->car; // Grab the key.
                tenv_put(ne, k, f->objs[i]); // Locally define it in the env.
                par = par->cdr; // Move to the next parameter.
                if (!par) break; // Break if we run out of parameter names.
            }

            // Now that we're all set up, we create the new stackframe and set that as
            // the active one. BUT! This is special! Instead of going down one level
            // into the scope of the lambda, we SUBSTITUTE the current frame with the
            // lambda frame. This frame, which *calls* the lambda, will otherwise
            // be the receiver of the return values, which should be propagated upwards
            // to the callee. It is therefore much simpler to just replace the frame.
            nf = tframe_new(ne);
            nf->prev = f->prev;
            nf->vp = body;
            f = nf;

            // This is important, since just below this if/else block we would normally
            // go up one stack frame to the previous frame.
            continue;
        } else {
            TET_THROW(s, "not invocable type: %s", tvaltype_print(f->objs[0]->type));
        }

        // Move up one frame and keep going. If no frame remains, the while loop breaks
        // and we simply return NULL (which indicates success!).
        f = f->prev;
    }

    // Remove our error handler again.
    TET_UNCATCH(s);

    // Return victiously! (NULL on success, error tvals otherwise.)
    return NULL;
}

tframe *tet_read(tstate *s, char *in) {
    tframe *f = tframe_new(s->env);

    tsize i = 0;
    tval *v = tet_parse(s, in, &i);
    f->vp = v;
    return f;
}

tval *tet_parse(tstate *s, char *in, tsize *i) {
    tval *v = NULL;
    while (!EOFP(in[*i])) {
        char c = in[*i];
        switch (c) {
            case ' ':
            case '\n':
            case '\r':
                (*i)++;
                break;
            case '"':
                (*i)++;
                v = tet_parse_str(s, in, i);
                break;
            case '(':
                (*i)++;
                v = tet_parse_sexpr(s, in, i);
                break;
            case '{':
                (*i)++;
                v = tet_parse_qexpr(s, in, i);
                break;
            default:
                if (DIGITP(in[*i])) {
                    v = tet_parse_num(s, in, i);
                } else {
                    v = tet_parse_sym(s, in, i);
                }
                break;
        }
        if (v) break;
    }
    return v;
}


tval *tet_parse_num(tstate *s, char *in, tsize *i) {
    tnum n = 0;
    while (DIGITP(in[*i])) {
        n = (n * 10) + (in[(*i)++] - '0');
    }
    return tval_num(s, n);
}

tval *tet_parse_sym(tstate *s, char *in, tsize *i) {
    tsize b = *i;
    while (!EOFP(in[*i]) && !BLANKP(in[*i]) && !CLOSEP(in[*i])) {
        (*i)++;
    }

    char *str = tralloc(s, *i - b + 1);
    strncpy(str, in + b, *i - b);
    str[*i - b] = '\0';

    tval *v = tval_sym(s, str);
    trforget(s, 1); // *str
    return v;
}

tval *tet_parse_str(tstate *s, char *in, tsize *i) {
    tsize b = *i;
    bool quoted = false;
    while (!EOFP(in[*i])) {
        if (!quoted) {
            if (in[*i] == '"') {
                break;
            } else if (in[*i] == '\\') {
                quoted = true;
            }
        } else {
            quoted = false;
        }
        (*i)++;
    }

    char *str = tralloc(s, *i - b + 1);
    strncpy(str, in + b, *i - b);
    str[*i - b] = '\0';

    tval *v = tval_str(s, str);
    trforget(s, 1); // *str
    return v;
}

tval *tet_parse_sexpr(tstate *s, char *in, tsize *i) {
    tval *root = tval_sexpr(s, NULL, NULL);
    tval *cur = root;

    while (!EOFP(in[*i]) && in[*i] != ')') {
        tval *v = tet_parse(s, in, i);

        if (cur->car == NULL) {
            cur->car = v;
        } else {
            // Create a new pair and stick it to the end.
            tval *p = tval_sexpr(s, v, NULL);
            cur->cdr = p;
            cur = p;
        }
    }
    (*i)++;
    return root;
}

tval *tet_parse_qexpr(tstate *s, char *in, tsize *i) {
    tval *root = tval_qexpr(s, NULL, NULL);
    tval *cur = root;

    while (!EOFP(in[*i]) && in[*i] != '}') {
        tval *v = tet_parse(s, in, i);

        if (cur->car == NULL) {
            cur->car = v;
        } else {
            // Create a new pair and stick it to the end.
            tval *p = tval_qexpr(s, v, NULL);
            cur->cdr = p;
            cur = p;
        }
    }
    (*i)++;
    return root;
}


//   ____  _   _ ___ _   _____ ___ _   _ ____
//  | __ )| | | |_ _| | |_   _|_ _| \ | / ___|
//  |  _ \| | | || || |   | |  | ||  \| \___ \
//  | |_) | |_| || || |___| |  | || |\  |___) |
//  |____/ \___/|___|_____|_| |___|_| \_|____/
//

tsize builtin_car(tframe *f) {
    tval *v = tet_popsexpr(f);
    tframe_push(f, v->car);
    return 1;
}

tsize builtin_cdr(tframe *f) {
    tval *v = tet_popsexpr(f);
    tframe_push(f, v->cdr);
    return 1;
}

tsize builtin_lambda(tframe *f) {
    tval *body = tet_popsexpr(f);
    tval *pars = tet_popsexpr(f);
    tet_pushlambda(f, pars, body);
    return 1;
}

tsize builtin_add(tframe *f) {
    tsize c = tframe_size(f);
    tnum sum = 0;
    for (tsize i = 0; i < c - 1; i++) {
        sum += tet_popnumber(f);
    }
    tet_pushnumber(f, sum);
    return 1;
}

tsize builtin_sub(tframe *f) {}

tsize builtin_mul(tframe *f) {}

tsize builtin_div(tframe *f) {}
