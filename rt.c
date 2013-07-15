/* Copyright 2012-2013 Dustin DeWeese
   This file is part of pegc.

    pegc is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    pegc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with pegc.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <assert.h>
#include "rt_types.h"
#include "gen/rt.h"
#include "gen/primitive.h"

// make sure &cells > 255
cell_t cells[1<<16];
cell_t *cells_ptr;
uint8_t alt_cnt = 0;
cell_t fail_cell = {
  .func = func_reduced,
  .type = T_FAIL
};
cell_t _hole;
cell_t *hole = &_hole;

measure_t measure, saved_measure;

// #define CHECK_CYCLE

extern char __data_start;

bool is_data(void *p) {
  return p >= (void *)&__data_start;
}

bool is_cell(void *p) {
  return p >= (void *)&cells && p < (void *)(&cells+1);
}

bool is_closure(void *p) {
  return is_data(p) && !is_hole(p) && ((cell_t *)p)->func;
}

bool closure_is_ready(cell_t *c) {
  assert(is_closure(c));
  return !is_marked(c->func, 1);
}

void closure_set_ready(cell_t *c, bool r) {
  assert(is_closure(c));
  c->func = mark_ptr(clear_ptr(c->func, 1), r ? 0 : 1);
}

/* propagate alternatives down to root of expression tree */
/* [TODO] distribute splitting into args */
/* or combine reduce and split, so each arg is reduced */
/* just before split, and alts are stored then zeroed */
cell_t *closure_split(cell_t *c, unsigned int s) {
  assert(is_closure(c) && closure_is_ready(c));
  unsigned int i, j, n = 0;
  unsigned int alt_mask = 0;

  cell_t *cn, *p = c->alt;

  /* calculate a mask for args with alts */
  i = s;
  alt_mask = 0;
  while(i--) {
    alt_mask <<= 1;
    if(is_marked(c->arg[i], 1)) {
      /* clear marks as we go */
      c->arg[i] = clear_ptr(c->arg[i], 1);
    } else if(c->arg[i] &&
	      c->arg[i]->alt) {
      alt_mask |= 1;
      n++;
    }
  }

  if(n == 0) return p;

  p = c->alt;

  for(i = alt_mask;
      i != 0;
      i = (i - 1) & alt_mask) {
    cn = closure_alloc(s);
    cn->func = c->func;
    for(j = 0; j < s; j++) {
      cn->arg[j] = (1<<j) & i ?
	c->arg[j]->alt :
	mark_ptr(c->arg[j], 1);
    }
    cn->alt = p;
    p = cn;
  }

  unsigned int nref_alt_alt = 1 << (n-1);
  unsigned int nref_alt = nref_alt_alt - 1;
  unsigned int nref_noalt = (1 << n) - 1;
  for(i = 0; i < s; i++) {
    if((1<<i) & alt_mask) {
      c->arg[i]->alt->n += nref_alt_alt;
      c->arg[i]->n += nref_alt;
    } else {
      c->arg[i]->n += nref_noalt;
    }
  }

  return p;
}

cell_t *closure_split1(cell_t *c, int n) {
  if(!c->arg[n]->alt) return c->alt;
  cell_t *a = copy(c);
  a->arg[n] = 0;
  traverse_ref(a, ARGS);
  a->arg[n] = ref(c->arg[n]->alt);
  a->n = 0;
  a->alt = c->alt;
  return a;
}

bool reduce(cell_t **cp) {
  cell_t *c;
 retry:
  c = clear_ptr(*cp, 1);
  if(!c || !closure_is_ready(c)) return false;
  assert(is_closure(c) &&
	 closure_is_ready(c));
  measure.reduce_cnt++;
  switch(c->func(cp)) {
  case r_fail: return false;
  case r_success: return true;
  case r_retry:
    goto retry;
  default: return false;
  }
}

bool reduce_partial(cell_t **cp) {
  cell_t *c;
  c = clear_ptr(*cp, 1);
  if(!c || !closure_is_ready(c)) return false;
  assert(is_closure(c) &&
	 closure_is_ready(c));
  measure.reduce_cnt++;
  return c->func(cp) != r_fail;
}

cell_t *cells_next() {
  cell_t *p = cells_ptr;
  assert(is_cell(p) && !is_closure(p) && is_cell(cells_ptr->next));
  cells_ptr = cells_ptr->next;
  return p;
}

#ifdef CHECK_CYCLE
bool check_cycle() {
  int i = 0;
  cell_t *start = cells_ptr, *ptr = start;
  while(ptr->next != start) {
    if(i > LENGTH(cells)) return false;
    i++;
    assert(is_cell(ptr->next->next));
    ptr = ptr->next;
  }
  return true;
}
#else
bool check_cycle() {
  return true;
}
#endif

void cells_init() {
  int i;
  const unsigned int n = LENGTH(cells)-1;

  // zero the cells
  bzero(&cells, sizeof(cells));

  // set up doubly-linked pointer ring
  for(i = 0; i < n; i++) {
    cells[i].prev = &cells[i-1];
    cells[i].next = &cells[i+1];
  }
  cells[0].prev = &cells[n-1];
  cells[n-1].next = &cells[0];

  cells_ptr = &cells[0];
  assert(check_cycle());
  alt_cnt = 0;
}

void cell_alloc(cell_t *c) {
  assert(is_cell(c) && !is_closure(c));
  cell_t *prev = c->prev;
  assert(is_cell(prev) && !is_closure(prev));
  cell_t *next = c->next;
  assert(is_cell(next) && !is_closure(next));
  if(cells_ptr == c) cells_next();
  prev->next = next;
  next->prev = prev;
  measure.alloc_cnt++;
  if(++measure.current_alloc_cnt > measure.max_alloc_cnt)
    measure.max_alloc_cnt = measure.current_alloc_cnt;
  assert(check_cycle());
}

cell_t *closure_alloc(int args) {
  return closure_alloc_cells(calculate_cells(args));
}

cell_t *closure_alloc_cells(int size) {
  cell_t *ptr = cells_next(), *c = ptr;
  cell_t *mark = ptr;
  int cnt = 0;

  // search for contiguous chunk
  while(cnt < size) {
    if(is_cell(ptr) && !is_closure(ptr)) {
      cnt++;
      ptr++;
    } else {
      cnt = 0;
      c = ptr = cells_next();
      assert(c != mark);
    }
  }

  // remove the found chunk
  int i;
  for(i = 0; i < size; i++) {
    cell_alloc(&c[i]);
  }

  bzero(c, sizeof(cell_t)*size);
  assert(check_cycle());
  return c;
}

#define calc_size(f, n)				\
  ((sizeof(cell_t)				\
    + ((intptr_t)&(((cell_t *)0)->f[n]) - 1))	\
   / sizeof(cell_t))

int calculate_cells(int n) {
  return calc_size(arg, n);
}

int calculate_list_size(int n) {
  return calc_size(ptr, n);
}

int calculate_val_size(int n) {
  return calc_size(val, n);
}

int closure_cells(cell_t *c) {
  return calculate_cells(closure_args(c));
}

void cell_free(cell_t *c) {
  c->func = 0;
  c->next = cells_ptr;
  c->prev = cells_ptr->prev;
  cells_ptr->prev = c;
  c->prev->next = c;
}

void closure_shrink(cell_t *c, int s) {
  if(!is_cell(c)) return;
  int i, size = closure_cells(c);
  if(size > s) {
    assert(is_closure(c));
    for(i = s; i < size; i++) {
      c[i].func = 0;
      c[i].prev = &c[i-1];
      c[i].next = &c[i+1];
    }
    c[s].prev = cells_ptr->prev;
    cells_ptr->prev->next = &c[s];
    c[size-1].next = cells_ptr;
    cells_ptr->prev = &c[size-1];
    assert(check_cycle());
    measure.current_alloc_cnt -= size - s;
  }
}

void closure_free(cell_t *c) {
  closure_shrink(c, 0);
}

result_t func_reduced(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  assert(is_closure(c));
  measure.reduce_cnt--;
  return c->type != T_FAIL ? r_success : r_fail;
}

cell_t *val(intptr_t x) {
  cell_t *c = closure_alloc(1);
  c->func = func_reduced;
  c->type = T_INT;
  c->val[0] = x;
  c->val_size = 1;
  return c;
}

cell_t *vector(uint32_t n) {
  cell_t *c = closure_alloc_cells(calculate_val_size(n));
  c->func = func_reduced;
  c->type = T_INT;
  c->val_size = n;
  return c;
}

cell_t *quote(cell_t *x) {
  cell_t *c = closure_alloc(1);
  c->func = func_reduced;
  c->ptr[0] = x;
  return c;
}

cell_t *empty_list() {
  return quote(0);
}

cell_t *ind(cell_t *x) {
  cell_t *c = val((intptr_t)x);
  c->type = T_INDIRECT;
  return c;
}

cell_t *append(cell_t *a, cell_t *b) {
  int n = list_size(b);
  int n_a = list_size(a);
  cell_t *e = expand(a, n);
  while(n--) e->ptr[n + n_a] = b->ptr[n];
  return e;
}

cell_t *expand(cell_t *c, unsigned int s) {
  if(!c) return 0;
  int n = closure_args(c);
  int cn_p = calculate_cells(n);
  int cn = calculate_cells(n + s);
  if(!c->n && cn == cn_p) {
    drop(c->alt);
    c->alt = 0;
    return c;
  } else {
    /* copy */
    cell_t *new = closure_alloc_cells(cn);
    memcpy(new, c, cn_p * sizeof(cell_t));
    new->n = 0;
    traverse_ref(new, PTRS);
    drop(c);
    return new;
  }
}

cell_t *compose_nd(cell_t *a, cell_t *b) {
  int n = list_size(b);
  int n_a = list_size(a);
  int i = 0;
  if(n && n_a) {
    cell_t *l;
    while(!closure_is_ready(l = b->ptr[n-1]) && i < n_a) {
      b = arg_nd(l, ref(a->ptr[i++]), b);
    }
  }
  cell_t *e = expand(b, n_a - i);
  int j;
  for(j = n; i < n_a; ++i, ++j) {
    e->ptr[j] = ref(a->ptr[i]);
  }
  drop(a);
  return e;
}

bool is_reduced(cell_t *c) {
  return c && c->func == func_reduced;
}

// max offset is 255
bool is_offset(cell_t *c) {
  return !((intptr_t)c & ~0xff);
}

// args must be >= 1
cell_t *func(reduce_t *f, int args) {
  assert(args >= 0);
  cell_t *c = closure_alloc(args);
  assert(c->func == 0);
  c->func = f;
  c->arg[0] = (cell_t *)(intptr_t)(args - 1);
  closure_set_ready(c, false);
  return c;
}

#define cell_offset(f) ((cell_t **)&(((cell_t *)0)->f))

int list_size(cell_t *c) {
  int i = 0;
  cell_t **p = c->ptr;
  while(*p && is_data(*p)) {
    ++p;
    ++i;
  }
  return i;
}

int val_size(cell_t *c) {
  return c->val_size;
}

int closure_args(cell_t *c) {
  assert(is_closure(c));
  cell_t **p = c->arg;
  int n = 0;

  /* funcs with hidden args */
  if(c->func == func_id ||
     c->func == func_dep) return 1;
  if(c->func == func_alt) return 2;

  if(is_reduced(c)) {
    if(is_list(c))
      return list_size(c) +
	cell_offset(ptr[0]) -
	cell_offset(arg[0]);
    else return val_size(c) +
	   cell_offset(val[0]) -
	   cell_offset(arg[0]);
  } else if(is_offset(*p)) {
    intptr_t o = (intptr_t)(*p) + 1;
    p += o;
    n += o;
  }
  /* args must be data ptrs */
  while(is_data(*p)) {
    p++;
    n++;
  }
  return n;
}

int closure_next_child(cell_t *c) {
  assert(is_closure(c));
  return is_offset(c->arg[0]) ? (intptr_t)c->arg[0] : 0;
}

/* protect against destructive modification */
cell_t *protect(cell_t *c) {
  if(c->n) {
    --c->n;
    c = dup(c);
    c->n = 0;
  }
  return c;
}

/* arg is destructive to c */
void arg(cell_t *c, cell_t *a) {
  assert(is_closure(c) && is_closure(a));
  assert(!closure_is_ready(c));
  int i = closure_next_child(c);
  if(!is_data(c->arg[i])) {
    c->arg[0] = (cell_t *)(intptr_t)(i - (closure_is_ready(a) ? 1 : 0));
    c->arg[i] = a;
    if(i == 0) closure_set_ready(c, closure_is_ready(a));
  } else {
    arg(c->arg[i], a);
    if(closure_is_ready(c->arg[i])) {
      if(i == 0) closure_set_ready(c, true);
      else --*(intptr_t *)&c->arg[0]; // decrement offset
    }
  }
}

bool is_indirect(cell_t *c) {
  return c->type == T_INDIRECT;
}

#define traverse(r, action, flags) 			\
  do {							\
    cell_t **p;						\
    if(is_reduced(r)) {					\
      if(is_indirect(r)) {				\
	p = (cell_t **)(r)->val;			\
        action						\
      } else if(((flags) & PTRS) &&			\
		is_list(r)) {				\
	int i, n = list_size(r);			\
	for(i = 0; i < n; ++i) {			\
	  p = (r)->ptr + i;				\
	  action					\
	}						\
      }							\
    } else if((flags) & ARGS) {				\
      int i, n = closure_args(r);			\
      for(i = closure_next_child(r); i < n; ++i) {	\
	if(!is_hole((r)->arg[i])) {			\
	  p = (r)->arg + i;				\
	  action					\
	}						\
      }							\
    }							\
    if((flags) & ALT) {					\
      p = &(r)->alt;					\
      action						\
    }							\
  } while(0)

void traverse_mark_alt(cell_t *c) {
  traverse(c, {
      if(*p && !is_marked((*p)->alt, 1)) {
	(*p)->alt = mark_ptr((*p)->alt, 1);
	traverse_mark_alt(*p);
      }
    }, ARGS | PTRS);
}

void traverse_clear_alt(cell_t *c) {
  traverse(c, {
      if(*p && is_marked((*p)->alt, 1)) {
	(*p)->alt = clear_ptr((*p)->alt, 1);
	traverse_clear_alt(*p);
      }
    }, ARGS | PTRS);
}

cell_t *arg_nd(cell_t *c, cell_t *a, cell_t *r) {
  cell_t *t = _arg_nd(c, a, r);
  zero_tmps(r);
  //check_tmps();
  return t;
}

cell_t *_arg_nd(cell_t *c, cell_t *a, cell_t *r) {
  cell_t *t;
  assert(is_closure(c) && is_closure(a));
  assert(!closure_is_ready(c));
  //assert(!is_marked(c->alt, 1));
  int i = closure_next_child(c);
  if(!is_data(c->arg[i])) {
    //traverse_clear_alt(a);
    t = modify_copy(c, r);
    cell_t *_c = clear_ptr(c->tmp, 3) ? clear_ptr(c->tmp, 3) : c;
    _c->arg[0] = (cell_t *)(intptr_t)(i - (closure_is_ready(a) ? 1 : 0));
    _c->arg[i] = a;
    if(i == 0) closure_set_ready(_c, closure_is_ready(a));
  } else {
    t = _arg_nd(c->arg[i], a, r);
    cell_t *_c = clear_ptr(c->tmp, 3) ? clear_ptr(c->tmp, 3) : c;
    if(closure_is_ready(_c->arg[i])) {
      if(i == 0) closure_set_ready(_c, true);
      else --*(intptr_t *)&_c->arg[0]; // decrement offset
    }
  }
  return t;
}

cell_t *copy(cell_t *c) {
  int size = closure_cells(c);
  cell_t *new = closure_alloc_cells(size);
  memcpy(new, c, size * sizeof(cell_t));
  return new;
}

cell_t *traverse_ref(cell_t *c, uint8_t flags) {
  traverse(c, {
      if(is_closure(*p)) *p = ref(*p);
    }, flags);
  return c;
}

void store_fail(cell_t *c, cell_t *alt) {
  closure_shrink(c, 1);
  int n = c->n;
  memset(c, 0, sizeof(cell_t));
  c->n = n;
  c->func = func_reduced;
  c->type = T_FAIL;
  c->alt = alt;
}

void store_reduced(cell_t *c, cell_t *r) {
  int n = c->n;
  r->func = func_reduced;
  int size = is_closure(r) ? closure_cells(r) : 0;
  if(size <= closure_cells(c)) {
    closure_shrink(c, size);
    memcpy(c, r, sizeof(cell_t) * size);
    if(is_cell(r)) traverse_ref(r, ALT | ARGS | PTRS);
    drop(r);
  } else { /* TODO: must copy if not cell */
    closure_shrink(c, 1);
    if(!is_cell(r)) {
      cell_t *t = closure_alloc_cells(size);
      memcpy(t, r, sizeof(cell_t) * size);
      r = t;
    }
    c->type = T_INDIRECT;
    c->alt = ref(r->alt);
    c->alt_set = r->alt_set;
    c->val[0] = (intptr_t)r;
  }
  c->n = n;
  c->func = func_reduced;
}

cell_t *ref(cell_t *c) {
  return(refn(c, 1));
}

cell_t *refn(cell_t *c, unsigned int n) {
  if(c) {
    assert(is_closure(c));
    c->n += n;
  }
  return c;
}

cell_t *dup(cell_t *c) {
  assert(is_closure(c));
  if(closure_is_ready(c)) {
    if(is_list(c)) {
      int n = list_size(c);
      cell_t *l = c->ptr[n-1];
      if(!closure_is_ready(l)) {
	cell_t *tmp = copy(c);
	int i;
	for(i = 0; i < n-1; ++i)
	  tmp->ptr[i] = ref(tmp->ptr[i]);
	tmp->ptr[n-1] = dup(tmp->ptr[n-1]);
	return tmp;
      }
    }
    return ref(c);
  }
  int args = closure_args(c);
  cell_t *tmp = copy(c);
  /* c->arg[<i] are empty and c->arg[>i] are ready, *
   * so no need to copy                             *
   * c->arg[i] only needs copied if filled          */
  int i = closure_next_child(c);
  if(is_closure(c->arg[i])) tmp->arg[i] = dup(c->arg[i]);
  while(++i < args) tmp->arg[i] = ref(c->arg[i]);
  return tmp;
}

bool is_nil(cell_t *c) {
  return !c;
}

bool is_list(cell_t *c) {
  return c->func == func_reduced &&
    (c->type == 0 || c->type > 255);
}

void shallow_unref(cell_t *c) {
  if(is_closure(c)) {
    if(c->n) --c->n;
    else closure_free(c);
  }
}

bool is_weak(cell_t *p, cell_t *c) {
  return c && is_dep(c) && (!c->arg[0] || c->arg[0] == p);
}

bool is_hole(cell_t *c) {
  return c == hole;
}

void drop(cell_t *c) {
  if(!is_cell(c) || !is_closure(c)) return;
  if(!c->n) {
    cell_t *p;
    traverse(c, {
	cell_t *x = clear_ptr(*p, 3);
	/* !is_marked condition needed */
	/* during _modify_copy2 */
	if(!is_marked(*p, 3) &&
	   !is_weak(c, x)) {
	  drop(x);
	}
      }, ALT | ARGS | PTRS);
    if(is_dep(c) && !is_reduced(p = c->arg[0]) && is_closure(p)) {
      /* mark dep arg as gone */
      int n = closure_args(p);
      while(n--) {
	if(p->arg[n] == c) {
	  p->arg[n] = hole;
	  break;
	}
      }
    }
    closure_free(c);
  } else {
    --c->n;
  }
}

#define APPEND(field)				\
  do {						\
    if(!a) return b;				\
    if(!b) return a;				\
						\
    cell_t *p = a, *r;				\
    while((r = p->field)) p = r;		\
    p->field = b;				\
    return a;					\
  } while(0)

cell_t *conc_alt(cell_t *a, cell_t *b) { APPEND(alt); }
/* append is destructive to a! */
//cell_t *append(cell_t *a, cell_t *b) { APPEND(next); }

cell_t *dep(cell_t *c) {
  cell_t *n = closure_alloc(1);
  n->func = func_dep;
  n->arg[0] = c;
  return n;
}

result_t func_dep(cell_t **cp) {
  cell_t *c = clear_ptr(*cp, 3);
  /* rely on another cell for reduction */
  /* don't need to drop arg, handled by other function */
  cell_t *p = ref(c->arg[0]);
  bool s = reduce_partial(&p);
  drop(p);
  return s ? r_retry : r_fail;
}

bool is_dep(cell_t *c) {
  return c->func == func_dep;
}

cell_t *data(cell_t *c) {
  if(!(c->type == T_INDIRECT))
    return c;
  else return (cell_t *)c->val[0];
}

// *** fix arg()'s
cell_t *compose_expand(cell_t *a, unsigned int n, cell_t *b) {
  assert(is_closure(a) &&
	 is_closure(b) && is_list(b));
  assert(n);
  int bs = list_size(b);
  if(bs) {
    cell_t *l = b->ptr[bs-1];
    while(n-1 && !closure_is_ready(l)) {
      cell_t *d = dep(ref(a));
      arg(l, d);
      arg(a, d);
      --n;
    }
    if(!closure_is_ready(l)) {
      arg(l, a);
      return b;
    }
  }

  b = expand(b, n);

  int i;
  for(i = 0; i < n-1; ++i) {
    cell_t *d = dep(ref(a));
    b->ptr[bs+i] = d;
    arg(a, d);
  }
  b->ptr[bs+n-1] = a;
  return b;
}

cell_t *pushl_val(intptr_t x, cell_t *c) {
  int n = c->val_size++;
  c = expand(c, 1);
  c->val[n] = x;
  return c;
}

/* b is a list, a is a closure */
cell_t *pushl(cell_t *a, cell_t *b) {
  assert(is_closure(a) &&
	 is_closure(b) && is_list(b));

  int n = list_size(b);
  if(n) {
    cell_t *l = b->ptr[n - 1];
    if(!closure_is_ready(l)) {
      arg(l, a);
      return b;
    }
  }

  cell_t *e = expand(b, 1);
  e->ptr[n] = a;
  return e;
}

/* b is a list, a is a closure */
cell_t *pushl_nd(cell_t *a, cell_t *b) {
  assert(is_closure(a) &&
	 is_closure(b) && is_list(b));

  int n = list_size(b);
  if(n) {
    cell_t *l = b->ptr[n-1];
    if(!closure_is_ready(l)) {
      return arg_nd(l, a, b);
    }
  }

  cell_t *e = expand(b, 1);
  e->ptr[n] = a;
  return e;
}

#define BM_SIZE (sizeof(intptr_t) * 4)

intptr_t bm(int k, int v) {
  assert(k < BM_SIZE);
  return ((intptr_t)1 << (k + BM_SIZE)) |
    (((intptr_t)v & 1) << k);
}

alt_set_t bm_intersect(alt_set_t a, alt_set_t b) {
  return a & b;
}

alt_set_t bm_union(alt_set_t a, alt_set_t b) {
  return a | b;
}

alt_set_t bm_conflict(alt_set_t a, alt_set_t b) {
  return ((a & b) >> BM_SIZE) &
    ((a ^ b) & (((intptr_t)1<<BM_SIZE)-1));
}

alt_set_t bm_overlap(alt_set_t a, alt_set_t b) {
  return a | (b & (a >> BM_SIZE));
}

void set_bit(uint8_t *m, unsigned int x) {
  m[x >> 3] |= 1 << (x & 7);
}

void clear_bit(uint8_t *m, unsigned int x) {
  m[x >> 3] &= ~(1 << (x & 7));
}

bool check_bit(uint8_t *m, unsigned int x) {
  return m[x >> 3] & (1 << (x & 7));
}
cell_t *collect;

/* reassemble a fragmented cell */
/*
bool func_collect(cell_t *c) {
  collect->alt = c->alt;
  collect->n = c->n;
  collect->func = (reduce_t *)c->arg[0];
  cell_t **dest = collect->arg;
  cell_t **src = c->arg+1;
  cell_t *prev = 0;
  int n = sizeof(c->arg)-1;
  do {
    while(--n > 0) {
      *dest++ = *src++;
    }
    src = (cell_t **)*src;
    cell_free(prev);
    prev = (cell_t *)src;
    n = sizeof(cell_t)-1;
    src++;
  } while(src);
  bool b = reduce(collect);
  return store_reduced(c, collect, b);
}
*/
/*
cell_t *func2(reduce_t *f, unsigned int args, unsigned int out) {
  assert(out >= 1);
  cell_t *c, *p, *n = func(f, args + out - 1);
  if(out > 1) {
    --out;
    c = p = dep(n);
    arg(n, ref(p));
    while(--out) {
      p->next = dep(ref(n));
      p = p->next;
      arg(n, ref(p));
    }
    p->next = n;
  } else {
    c = n;
  }
  return c;
}
*/
void *lookup(void *table, unsigned int width, unsigned int rows, const char *key) {
  unsigned int low = 0, high = rows, pivot;
  int c;
  void *entry, *ret = 0;
  int key_length = strnlen(key, width);
  while(high > low) {
    pivot = low + ((high - low) >> 1);
    entry = table + width * pivot;
    c = strncmp(key, entry, key_length);
    if(c == 0) {
      /* keep looking for a lower key */
      ret = entry;
      high = pivot;
    } else if(c < 0) high = pivot;
    else low = pivot + 1;
  }
  return ret;
}

cell_t *get(cell_t *c) {
  if(c->type == T_INDIRECT)
    return get((cell_t *)c->val[0]);
  else return c;
}

cell_t *modify_copy(cell_t *c, cell_t *r) {
  cell_t *new = _modify_copy1(c, r, true);
  if(new != r) {
    ref(new);
    drop(r);
  }
  if(new) {
    _modify_copy2(new);
    return new;
  } else return r;
}

void check_tmps() {
  unsigned int i = 0;
  cell_t *p;
  while(i < LENGTH(cells)) {
    p = &cells[i];
    if(is_closure(p)) {
      /*
      if(p->tmp) {
	printf("<<%d %d>>\n", i, (int)p->tmp);
	drop(clear_ptr(p->tmp, 3));
	p->tmp = 0;
      }
      */
      assert(!p->tmp);
      i += closure_cells(p);
    } else ++i;
  }
}

void zero_tmps(cell_t *r) {
  if(!r || !r->tmp) return;

  cell_t *t = clear_ptr(r->tmp, 3);
  r->tmp = 0;
  zero_tmps(t);
  drop(t);

  traverse(r, {
      zero_tmps(*p);
    }, ARGS | PTRS | ALT);
}

int nondep_n(cell_t *c) {
  if(!is_closure(c)) return 0;
  int nd = c->n;
  if(is_dep(c)) return nd;
  else if(!is_reduced(c)) {
    int n = closure_args(c);
    while(n-- &&
	  is_closure(c->arg[n]) &&
	  is_dep(c->arg[n]) &&
	  c->arg[n]->arg[0] == c) --nd;
  }
  return nd;
}

void _modify_new(cell_t *r, bool u) {
  cell_t *n;
  if(clear_ptr(r->tmp, 3)) return;
  else if(u) {
    n = ref(r);
  } else {
    n = copy(r);
    if(clear_ptr(r->func, 1) == func_alt)
      n->arg[2] = (cell_t *)(intptr_t)alt_cnt++;
    n->tmp = (cell_t *)3;
    n->n = 0;
  }
  r->tmp = mark_ptr(n, 3);
}

/* first sweep of modify_copy */
cell_t *_modify_copy1(cell_t *c, cell_t *r, bool up) {
  int nd = nondep_n(r);

  /* is r unique (okay to replace)? */
  bool u = up && !nd;

  if(!is_closure(r)) return 0;
  if(r->tmp) {
    assert(is_marked(r->tmp, 3));
    /* already been replaced */
    return clear_ptr(r->tmp, 3);
  } else r->tmp = (cell_t *)3;
  if(c == r) _modify_new(r, u);
  traverse(r, {
      if(_modify_copy1(c, *p, u))
	_modify_new(r, u);
    }, ARGS | PTRS | ALT);
  return clear_ptr(r->tmp, 3);
}

cell_t *get_mod(cell_t *r) {
  if(!r) return 0;
  cell_t *a = r->tmp;
  if(is_marked(a, 2)) return clear_ptr(a, 3);
  else return 0;
}

/* second sweep of modify copy */
void _modify_copy2(cell_t *r) {

  /* r is modified in place */
  bool s = r == clear_ptr(r->tmp, 3);

  if(!is_closure(r)) return;
  /* alread been here */
  if(!is_marked(r->tmp, 1)) return;
  r->tmp = clear_ptr(r->tmp, 1);
  traverse(r, {
      cell_t *u = clear_ptr(*p, 3);
      cell_t *t = get_mod(u);
      if(t) {
	if(!(s && t == u)) {
	  *p = ref(t);
	  if(s) drop(u);
	}
	_modify_copy2(t);
      } else if(!s) ref(u);
      if((!s || t != u) && is_weak(r, *p)) {
	--(*p)->n;
      }
    }, ARGS | PTRS | ALT);
}

cell_t *mod_alt(cell_t *c, cell_t *alt, alt_set_t alt_set) {
  cell_t *n;
  if(c->alt == alt &&
     c->alt_set == alt_set) return c;
  if(!c->n) {
    n = c;
    drop(c->alt);
  } else {
    int size = closure_cells(c);
    --c->n;
    if(size == 1) {
      n = copy(c);
      traverse_ref(n, ARGS | PTRS);
      n->n = 0;
    } else {
      n = ind(c);
    }
  }
  n->alt = alt;
  n->alt_set = alt_set;
  return n;
}
