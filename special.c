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

#include <assert.h>
#include "rt_types.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/special.h"
#include "gen/byte_compile.h"

bool func_value(cell_t **cp, type_t t) {
  cell_t *c = clear_ptr(*cp); // TODO remove clear_ptr
  assert(is_closure(c));
  measure.reduce_cnt--;
  if(c->value.type != T_FAIL &&
     type_match(t, c)) {
    if(is_any(c)) {
      /* create placeholder */
      if((t & T_EXCLUSIVE) == T_LIST) {
        cell_t *ph = func(func_placeholder, 1, 1);
        ph->expr.arg[0] = var_create(T_FUNCTION, c->value.ptr[0]);
        c->value.ptr[0] = ph;
        c->size = 2;
      }
      c->value.type |= t;
      trace(c, c, tt_update);
    }
    return true;
  }
  else {
    fail(cp, t);
    return false;
  }
}

cell_t *int_val(val_t x) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type = T_INT;
  c->value.integer[0] = x;
  return c;
}

cell_t *float_val(double x) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type = T_FLOAT;
  c->value.flt[0] = x;
  return c;
}

cell_t *symbol(val_t sym) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type = T_SYMBOL;
  c->value.integer[0] = sym;
  return c;
}

bool is_value(cell_t const *c) {
  return c && c->func == func_value;
}

cell_t *var_create(type_t t, cell_t *tc) {
  cell_t *c;
  if((t & T_EXCLUSIVE) == T_LIST) {
    c = make_list(1);
    cell_t *ph = func(func_placeholder, 1, 1);
    ph->expr.arg[0] = var_create(T_FUNCTION, tc);
    c->value.ptr[0] = ph;
  } else {
    c = closure_alloc(1);
    c->func = func_value;
    c->size = 2;
    c->value.ptr[0] = tc;
  }
  c->value.type = T_VAR | t;
  return c;
}

cell_t *var(type_t t, cell_t *c) {
  return var_create(t, trace_alloc(c ? c->size : 2));
}

bool is_var(cell_t const *c) {
  return c && is_value(c) && (c->value.type & T_VAR) != 0;
}

cell_t *vector(csize_t n) {
  cell_t *c = closure_alloc(n+1);
  c->func = func_value;
  c->value.type = T_ANY;
  return c;
}

cell_t *quote(cell_t *x) {
  cell_t *c = closure_alloc(2);
  c->func = func_value;
  c->value.type = T_LIST;
  c->value.ptr[0] = x;
  return c;
}

cell_t *empty_list() {
  cell_t *c = closure_alloc(1);
  c->func = func_value;
  c->value.type = T_LIST;
  return c;
}

cell_t *make_list(csize_t n) {
  if(n == 0) return &nil_cell;
  cell_t *c = closure_alloc(n + 1);
  c->func = func_value;
  c->value.type = T_LIST;
  return c;
}

bool is_list(cell_t const *c) {
  return c && is_value(c) && (c->value.type & T_EXCLUSIVE) == T_LIST;
}

cell_t *make_map(csize_t s) {
  csize_t cs = calculate_map_size(s);
  cell_t *c = closure_alloc_cells(cs);
  uintptr_t size = (sizeof(cell_t) * cs - offset(cell_t, value.map)) / sizeof(pair_t) - 1;
  c->func = func_value;
  c->size = 2 * (size + 1) + 1;
  c->value.type = T_MAP;
  c->value.map[0].first = size;
  c->value.map[0].second = 0;
  return c;
}

bool is_map(cell_t const *c) {
  return c && is_value(c) && (c->value.type & T_EXCLUSIVE) == T_MAP;
}

cell_t *make_string(seg_t s) {
  cell_t *c = closure_alloc(1);
  c->func = func_value;
  c->value.type = T_STRING;
  c->value.str = s;
  return c;
}

bool is_string(cell_t const *c) {
  return c && is_value(c) && (c->value.type & T_EXCLUSIVE) == T_STRING;
}

bool is_dep_of(cell_t *d, cell_t *c) {
  bool ret = false;
  traverse(c, {
      if(*p == d) ret = true;
    }, ARGS_OUT);
  return ret;
}

/* todo: propagate types here */
bool func_dep(cell_t **cp, UNUSED type_t t) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  /* rely on another cell for reduction */
  /* don't need to drop arg, handled by other function */
  /* must temporarily reference to avoid replacement of p which is referenced elsewhere */
  cell_t *p = ref(c->expr.arg[0]);
  assert(is_dep_of(c, p));
  insert_root(&p);
  c->func = func_dep_entered;
  reduce_dep(&p);
  assert(c->func != func_dep_entered);
  remove_root(&p);
  drop(p);
  return false;
}

bool func_dep_entered(cell_t **cp, UNUSED type_t t) {
  // shouldn't happen; circular dependency
  assert(false);
  fail(cp, t);
  return false;
}

cell_t *dep(cell_t *c) {
  cell_t *n = closure_alloc(1);
  n->func = func_dep;
  n->expr.arg[0] = c;
  return n;
}

bool is_dep(cell_t const *c) {
  return c->func == func_dep || c->func == func_dep_entered;
}

// this shouldn't reduced directly, but is called through reduce_partial from func_dep
// WORD("_", placeholder, 0, 1)
bool func_placeholder(cell_t **cp, UNUSED type_t t) {
  cell_t *c = *cp;
  assert(!is_marked(c));
  csize_t in = closure_in(c), n = closure_args(c);
  for(csize_t i = 0; i < in; ++i) {
    if(!reduce(&c->expr.arg[i], T_ANY)) goto fail; // TODO why not reduce_arg?
  }
  cell_t *res = var(t, c);
  for(csize_t i = in; i < n; ++i) {
    cell_t *d = c->expr.arg[i];
    if(d && is_dep(d)) {
      drop(c);
      d->expr.arg[0] = res;
      store_var(d, 0);
    }
  }
  store_reduced(cp, res);
  return true;

 fail:
  fail(cp, t);
  return false;
}

/*
bool func_self(cell_t **cp, UNUSED type_t t) {
  func_placeholder(cp, t);
  store_reduced(cp, var(t));
  return true;
}
*/

bool is_placeholder(cell_t const *c) {
  return c && clear_ptr(c->func) == (void *)func_placeholder;
}

bool func_fail(cell_t **cp, UNUSED type_t t) {
  cell_t *c = clear_ptr(*cp); // TODO remove clear_ptr
  assert(is_closure(c));
  measure.reduce_cnt--;
  fail(cp, t);
  return false;
}
