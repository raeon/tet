//
// Created by joris on 6/23/18.
//

#include <stdio.h>
#include "tet.h"

int main() {

    printf("\nstate initializing\n");
    tstate *s = tstate_new();
    tenv *e = s->env;

    printf("state initialized\n");
    printf("s->ptri: %u\n", s->ptri);
    printf("\n");

    printf("tenv initializing\n");
    tenv_put(e, tval_sym(s, "car"), tval_builtin(s, builtin_car));
    tenv_put(e, tval_sym(s, "cdr"), tval_builtin(s, builtin_cdr));
    tenv_put(e, tval_sym(s, "lambda"), tval_builtin(s, builtin_lambda));
    tenv_put(e, tval_sym(s, "+"), tval_builtin(s, builtin_add));
    tenv_put(e, tval_sym(s, "-"), tval_builtin(s, builtin_sub));
    tenv_put(e, tval_sym(s, "*"), tval_builtin(s, builtin_mul));
    tenv_put(e, tval_sym(s, "/"), tval_builtin(s, builtin_div));
    printf("tenv initialized\n\n");

    printf("evaluating\n");
    tframe *f = tet_read(s, "{1 2 3}");
    printf("input: ");
    tval_print(f->vp);
    printf("\noutput:\n");
    tval *r = tet_eval(s, f);
    if (r) {
        printf("\nERROR: %s\n", r->err);
        tstate_gc(s);
        return 1;
    }
    r = tframe_pop(f);
    tval_print(r);
    printf("\nevaluating finished\n\n");

    printf("used: %zu\n", s->obji);
    printf("gc: %zu\n", tstate_gc(s));

    printf("used: %zu\n", s->obji);
    printf("gc: %zu\n", tstate_gc(s));

    tstate_del(s);

    return 0;
}
