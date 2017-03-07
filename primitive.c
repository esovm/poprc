/* Copyright 2012-2016 Dustin DeWeese
   This file is part of PoprC.

    PoprC is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    PoprC is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with PoprC.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "rt_types.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/primitive.h"
#include "gen/special.h"
#include "gen/print.h"
#include "gen/test.h"
#include "gen/byte_compile.h"
#include "gen/support.h"
#include "gen/list.h"

   /*-----------------------------------------------,
    |          VARIABLE NAME CONVENTIONS            |
    |-----------------------------------------------|
    |                                               |
    |  cell_t *c = closure being reduced            |
    |  cell_t *d = first dep                        |
    |    (second closure returned, lower on stack)  |
    |  cell_t *e = second dep                       |
    |  cell_t *f = ...                              |
    |  cell_t *p = expr.arg[0], leftmost arg        |
    |  cell_t *q = expr.arg[1]                      |
    |  bool s = success                             |
    |  cell_t *res = result to be stored in c       |
    |  cell_t *res_X = result to be stored in X     |
    |                                               |
    '-----------------------------------------------*/

cell_t *_op2(val_t (*op)(val_t, val_t), cell_t *x, cell_t *y) {
  csize_t size = min(val_size(x),
                     val_size(y));
  cell_t *res = vector(size);
  for(csize_t i = 0; i < size; ++i)
    res->value.integer[i] = op(x->value.integer[i],
                               y->value.integer[i]);
  res->size = size + 1;
  return res;
}

bool func_op2(cell_t **cp, type_request_t treq, int arg_type, int res_type, val_t (*op)(val_t, val_t)) {
  cell_t *c = *cp;
  cell_t *res = 0;
  assert(!is_marked(c));

  if(treq.t != T_ANY && treq.t != res_type) goto fail;

  alt_set_t alt_set = 0;
  type_request_t atr = req_simple(arg_type);
  if(!reduce_arg(c, 0, &alt_set, atr) ||
     !reduce_arg(c, 1, &alt_set, atr) ||
     as_conflict(alt_set)) goto fail;
  clear_flags(c);

  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  res = is_var(p) || is_var(q) ? var(treq.t, c) : _op2(op, p, q);
  res->value.type.exclusive = res_type;
  res->alt = c->alt;
  res->value.alt_set = alt_set;
  store_reduced(cp, res);
  return true;

 fail:
  fail(cp, treq);
  return false;
}

// WORD("+", add, 2, 1)
val_t add_op(val_t x, val_t y) { return x + y; }
bool func_add(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_INT, add_op); }


// WORD("*", mul, 2, 1)
val_t mul_op(val_t x, val_t y) { return x * y; }
bool func_mul(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_INT, mul_op); }

// WORD("-", sub, 2, 1)
val_t sub_op(val_t x, val_t y) { return x - y; }
bool func_sub(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_INT, sub_op); }

// WORD(">", gt, 2, 1)
val_t gt_op(val_t x, val_t y) { return x > y; }
bool func_gt(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_SYMBOL, gt_op); }

// WORD(">=", gte, 2, 1)
val_t gte_op(val_t x, val_t y) { return x >= y; }
bool func_gte(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_SYMBOL, gte_op); }

// WORD("<", lt, 2, 1)
val_t lt_op(val_t x, val_t y) { return x < y; }
bool func_lt(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_SYMBOL, lt_op); }

// WORD("<=", lte, 2, 1)
val_t lte_op(val_t x, val_t y) { return x <= y; }
bool func_lte(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_SYMBOL, lte_op); }

// WORD("==", eq, 2, 1)
// WORD("=:=", eq_s, 2, 1)
val_t eq_op(val_t x, val_t y) { return x == y; }
bool func_eq(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_SYMBOL, eq_op); }
bool func_eq_s(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_SYMBOL, T_SYMBOL, eq_op); }

// WORD("!=", neq, 2, 1)
// WORD("!:=", neq_s, 2, 1)
val_t neq_op(val_t x, val_t y) { return x != y; }
bool func_neq(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_INT, T_SYMBOL, neq_op); }
bool func_neq_s(cell_t **cp, type_request_t treq) { return func_op2(cp, treq, T_SYMBOL, T_SYMBOL, neq_op); }

// outputs required from the left operand given the rignt operand
csize_t function_compose_out(cell_t *c, csize_t out) {
  c = clear_ptr(c);
  return function_in(c) + out - min(out, function_out(c, true));
}

// inputs required from the right operand given the left operand
csize_t function_compose_in(cell_t *c, csize_t in) {
  c = clear_ptr(c);
  return function_out(c, true) + in - min(in, function_in(c));
}

// WORD(".", compose, 2, 1)
bool func_compose(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));

  alt_set_t alt_set = 0;
  if(!reduce_arg(c, 0, &alt_set, req_list(NULL, treq.in, 0)) ||
     !reduce_arg(c, 1, &alt_set, req_list(NULL, function_compose_in(c->expr.arg[0], treq.in), treq.out)) ||
     as_conflict(alt_set)) goto fail;
  clear_flags(c);
  placeholder_extend(&c->expr.arg[0], treq.in, function_compose_out(c->expr.arg[1], treq.out) - is_row_list(c->expr.arg[0]));

  cell_t
    *p = c->expr.arg[0],
    *q = c->expr.arg[1],
    *res;

  if(is_empty_list(q)) { res = ref(p); goto done; }
  if(is_empty_list(p)) { res = ref(q); goto done; }

  res = compose(list_begin(p), ref(q));

 done:
  store_reduced(cp, mod_alt(res, c->alt, alt_set));
  ASSERT_REF();
  return true;

 fail:
  fail(cp, treq);
  return false;
}

// WORD("pushr", pushr, 2, 1)
bool func_pushr(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));

  alt_set_t alt_set = 0;
  type_request_t atr = req_list(&treq, 0, -1);
  if(!reduce_arg(c, 0, &alt_set, atr)) goto fail;
  clear_flags(c);

  cell_t *res;
  if(is_empty_list(c->expr.arg[0])) {
    res = quote(ref(c->expr.arg[1]));
  } else {
    res = make_list(2);
    res->value.ptr[1] = ref(c->expr.arg[0]);
    res->value.ptr[0] = ref(c->expr.arg[1]);
    res->value.type.flags = T_ROW;
  }
  res->value.alt_set = alt_set;
  drop(res->alt);
  res->alt = c->alt;

  store_reduced(cp, res);
  ASSERT_REF();
  return true;

 fail:
  fail(cp, treq);
  return false;
}

// WORD("|", alt, 2, 1)
bool func_alt(cell_t **cp, UNUSED type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  uint8_t a = new_alt_id(1);
  cell_t *r0 = id(c->expr.arg[0], as_single(a, 0));
  cell_t *r1 = id(c->expr.arg[1], as_single(a, 1));
  r0->alt = r1;
  store_lazy(cp, c, r0, 0);
  return false;
}

// WORD_DISABLED("||", alt2, 2, 1)
bool func_alt2(cell_t **cp, UNUSED type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *r0 = id(ref(c->expr.arg[0]), 0);
  cell_t *r1 = ref(c->expr.arg[1]);
  r0->alt = r1;
  *cp = r0;
  drop(c);
  return false;
}


cell_t *map_assert(cell_t *c, cell_t *t, cell_t *v) {
  cell_t *nc;
  assert(is_list(c));
  if(~c->value.type.flags & T_ROW) {
    nc = copy_expand(c, 1);
    v->value.type.exclusive = T_FUNCTION;
    nc->value.ptr[list_size(nc) - 1] = 0;
  } else {
    nc = copy(c);
  }
  traverse(nc, {
      if(*p) {
        cell_t *np = closure_alloc(2);
        np->func = func_assert;
        np->expr.arg[0] = ref(*p);
        np->expr.arg[1] = ref(t);
        *p = np;
      }
    }, PTRS);
  if(~c->value.type.flags & T_ROW) {
    nc->value.ptr[list_size(nc) - 1] = v;
  }
  c->value.type.flags |= T_ROW;
  return nc;
}

// WORD("!", assert, 2, 1)
bool func_assert(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));

  cell_t *res = NULL;
  alt_set_t alt_set = 0;
  if(!reduce_arg(c, 1, &alt_set, req_symbol)) goto fail;
  cell_t *p = clear_ptr(c->expr.arg[1]);

  if(!(p->value.integer[0] == SYM_True || is_var(p))) goto fail;

  if(is_var(p)) res = var(treq.t != T_LIST ? treq.t : T_FUNCTION, c);

  if(!reduce_arg(c, 0, &alt_set, treq) ||
     as_conflict(alt_set)) goto fail;
  clear_flags(c);
  cell_t *q = c->expr.arg[0];
  if(is_var(p)) {
    if(is_list(q)) {
      trace_update(c, res);
      res = map_assert(q, p, res);
    } else {
      res->value.type = q->value.type;
      res->value.type.flags |= T_VAR;
    }
    res->value.alt_set = alt_set;
    res->alt = c->alt;
  } else if(is_var(q)) {
    res = var(q->value.type.exclusive, c);
    res->value.alt_set = alt_set;
    res->alt = c->alt;
  } else {
    res = mod_alt(ref(q), c->alt, alt_set);
  }
  store_reduced(cp, res);
  return true;
fail:
  drop(res);
  fail(cp, treq);
  return false;
}

// WORD("id", id, 1, 1)
bool func_id(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  alt_set_t alt_set = c->expr.alt_set;

  if(alt_set || c->alt) {
    if(!reduce_arg(c, 0, &alt_set, treq) ||
       as_conflict(alt_set)) goto fail;
    clear_flags(c);

    store_reduced(cp, mod_alt(ref(c->expr.arg[0]), c->alt, alt_set));
    return true;
  } else {
    *cp = ref(c->expr.arg[0]);
    drop(c);
    return false;
  }
 fail:
  fail(cp, treq);
  return false;
}

// WORD("drop", drop, 2, 1)
bool func_drop(cell_t **cp, UNUSED type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *p = ref(c->expr.arg[0]);
  drop(c);
  *cp = p;
  return false;
}

// WORD("swap", swap, 2, 2)
bool func_swap(cell_t **cp, UNUSED type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *d = c->expr.arg[2];
  store_lazy_dep(d, c->expr.arg[0], 0);
  store_lazy(cp, c, c->expr.arg[1], 0);
  return false;
}

cell_t *id(cell_t *c, alt_set_t as) {
  cell_t *i = closure_alloc(1);
  i->func = func_id;
  i->expr.arg[0] = c;
  i->expr.alt_set = as;
  return i;
}

// WORD("dup", dup, 1, 2)
bool func_dup(cell_t **cp, UNUSED type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  cell_t *d = c->expr.arg[1];
  store_lazy_dep(d, ref(c->expr.arg[0]), 0);
  store_lazy(cp, c, c->expr.arg[0], 0);
  return false;
}

// WORD("pushl", ap, 2, 1)
// WORD("popr", ap, 1, 2)
bool func_ap(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(c));

  const csize_t
    in = closure_in(c) - 1,
    n = closure_args(c),
    out = closure_out(c);

  alt_set_t alt_set = 0;
  type_request_t atr = req_list((out && treq.in) ? NULL : &treq, in, out);
  if(!reduce_arg(c, in, &alt_set, atr)) goto fail;
  clear_flags(c);

  placeholder_extend(&c->expr.arg[in], treq.in + in, treq.out + out);

  list_iterator_t it;

  reverse_ptrs((void **)c->expr.arg, in);
  it.array = c->expr.arg;
  it.index = 0;
  it.size = in;
  it.row = false;
  cell_t *l = compose(it, ref(c->expr.arg[in]));
  reverse_ptrs((void **)c->expr.arg, in);

  bool is_nil = c->expr.arg[in] == &nil_cell;
  if(!is_nil) insert_root(&c->expr.arg[in]);
  it = list_begin(l);
  COUNTUP(i, out) {
    cell_t **x = list_next(&it, false);
    if(!x) {
      drop(l);
      goto fail;
    }
    cell_t *d = c->expr.arg[n-1-i];
    store_lazy_dep(d, ref(*x), alt_set);
  }
  if(!is_nil) remove_root(&c->expr.arg[in]);

  cell_t *res = list_rest(it);
  drop(l);
  res->value.alt_set = alt_set;
  store_reduced(cp, mod_alt(res, c->alt, alt_set));
  ASSERT_REF();
  return true;
fail:
  fail(cp, treq);
  return false;
}

// WORD("print", print, 2, 1)
bool func_print(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  cell_t *res = 0;
  assert(!is_marked(c));

  if(treq.t != T_ANY && treq.t != T_SYMBOL) goto fail;

  alt_set_t alt_set = 0;
  if(!reduce_arg(c, 0, &alt_set, req_symbol) ||
     !reduce_arg(c, 1, &alt_set, req_any) ||
     as_conflict(alt_set)) goto fail;
  clear_flags(c);

  if(c->alt) {
    drop(c->alt);
    c->alt = 0;
  }

  cell_t *p = c->expr.arg[0], *q = c->expr.arg[1];
  if(is_var(p) || is_var(q)) {
    res = var(T_SYMBOL, c);
  } else if(p->value.integer[0] == SYM_IO) {
    show_one(q);
    res = ref(p);
  } else goto fail;
  store_reduced(cp, res);
  return true;

 fail:
  drop(res);
  fail(cp, treq);
  return false;
}

// WORD("is_nil", is_nil, 1, 1)
bool func_is_nil(cell_t **cp, type_request_t treq) {
  cell_t *c = *cp;
  assert(!is_marked(*cp));

  if(treq.t != T_ANY && treq.t != T_SYMBOL) goto fail;

  alt_set_t alt_set = 0;
  if(!reduce_arg(c, 0, &alt_set, req_simple(T_LIST))) goto fail;
  clear_flags(c);

  cell_t *p = c->expr.arg[0];
  cell_t *res;
  if(is_var(p)) {
    res = var(T_SYMBOL, c);
  } else {
    res = symbol(is_empty_list(p) ? SYM_True : SYM_False);
  }

  res->value.alt_set = alt_set;
  res->alt = c->alt;
  store_reduced(cp, res);
  return true;

 fail:
  fail(cp, treq);
  return false;
}
