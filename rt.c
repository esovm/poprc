/* Copyright 2012 Dustin DeWeese
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

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "rt_types.h"
#include "rt.h"

// make sure &cells > 255
cell_t cells[1<<10];
cell_t *cells_ptr;
cell_t *nil = 0;

// #define CHECK_CYCLE

#define show(x) printf(#x " = %d\n", x)
#define LENGTH(_a) (sizeof(_a) / sizeof((_a)[0]))

cell_t *nul;

bool is_cell(void *p) {
  return p >= (void *)&cells && p < (void *)(&cells+1);
}

bool is_closure(void *p) {
  return is_cell(p) && !is_cell(((cell_t *)p)->func);
}

bool closure_is_ready(cell_t *c) {
  assert(is_closure(c));
  return !((intptr_t)c->func & 0x1);
}

void closure_set_ready(cell_t *c, bool r) {
  assert(is_closure(c));
  c->func = (reduce_t *)(((intptr_t)c->func & ~0x1) | !r);
}

void *nptr(void *c) {
  return (void *)~(intptr_t)c;
}

/* propagate alternatives down to root of expression tree */
cell_t *closure_split(cell_t *c) {
  //printf("__closure_split:");
  //print_sexpr(c);
  assert(is_closure(c) && closure_is_ready(c));
  unsigned int i, j, s = closure_args(c);
  unsigned int alt_mask = 0;
  unsigned int alts = 0;

  i = s; while(i--) {
    alt_mask <<= 1;
    if(c->arg[i] != 0 && c->arg[i]->alt != 0) {
      alts++;
      alt_mask |= 1;
    }
  }

  for(i = 0; i < s; i++) {
    if(c->arg[i]->alt) {
      refn(c->arg[i], (1 << (alts-1)) - 1);
      refn(c->arg[i]->alt, 1 << (alts-1));
      c->arg[i]->alt = 0;
    } else {
      refn(c->arg[i], (1 << alts) - 1);
    }
  }

  cell_t *p = 0, *n;
  for(i = alt_mask;
      i != 0;
      i = i - 1 & alt_mask) {
    n = closure_alloc(s);
    for(j = 0; j < s; j++) {
      n->arg[j] = (1<<j) & i ? c->arg[j]->alt : c->arg[j];
    }
    n->altn = nptr(p);
    p = n;
  }

  return p;
}

bool __reduce(reduce_t f, cell_t *c) {
  cell_t *t, *p = c;
  //print_sexpr(c);
  while(p) {
    if(f(p)) {
      if(p != c) {
	c->alt = nptr(p->altn);
	p->func = c->func;
	deref(p);
      }
      return true;
    }
    t = p;
    p = (cell_t *)~(intptr_t)p->altn;
    t->func = c->func;
    if(t != c) deref(t);
  }
  c->alt = 0;
  return false;
}

bool reduce(cell_t *c) {
  return c && c->func(c);
}

bool reducef(reduce_t *f, cell_t *c) {
  if(!c) return false;
  cell_t *a = c->altn;
  bool r = f(c);
  c->alt = conc_alt(c->alt, a);
  return r;
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
  
  // zero the cells
  bzero(&cells, sizeof(cells));

  // cells[0] is the nil cell
  //cells[0].func = func_nil;

  // set up doubly-linked pointer ring
  for(i = 0; i < LENGTH(cells); i++) {
    cells[i].prev = &cells[i-1];
    cells[i].next = &cells[i+1];
  }
  cells[0].prev = &cells[LENGTH(cells)-1];
  cells[LENGTH(cells)-1].next = &cells[0];

  cells_ptr = &cells[0];
  assert(check_cycle());
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
  assert(check_cycle());
}

cell_t *closure_alloc(int size) {
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

int calculate_cells(int args) {
  return 1 + ((sizeof(cell_t *) * (args - LENGTH(((cell_t *)0)->arg)) + sizeof(cell_t) - 1) / sizeof(cell_t));
}

int closure_cells(cell_t *c) {
  return calculate_cells(closure_args(c));
}

void closure_shrink(cell_t *c, int s) {
  int i, size = closure_cells(c);
  //printf("closure_shrink(&cells[%d], %d)\n", (unsigned int)(c - &cells[0]), s);
  if(size > s) {
    assert(is_closure(c));
    for(i = s; i < size; i++) {
      c[i].prev = &c[i-1];
      c[i].next = &c[i+1];
    }
    c[s].prev = cells_ptr->prev;
    cells_ptr->prev->next = &c[s];
    c[size-1].next = cells_ptr;
    cells_ptr->prev = &c[size-1];
    assert(check_cycle());
  }
}

void closure_free(cell_t *c) {
  //printf("closure_free(&cells[%d])\n", (unsigned int)(c - &cells[0]));
  closure_shrink(c, 0);
}

FUNC(nil) {
  return false;
}
/*
bool func_val(cell_t *c, void *r) {
  assert(is_closure(c));
  intptr_t val = (intptr_t)c->arg[0];
  *(intptr_t *)r = val;
  to_ref(c, val);
  return true;
}
*/
FUNC(ref_reduced) {
  assert(is_closure(c));
  return true;
}

FUNC(ref_failed) {
  return false;
}

FUNC(ref) {
  intptr_t val;
  cell_t *r = c->ptr;
  bool ret = reduce(r);
  if(ret) {
    memcpy(c, r, sizeof(cell_t));
    deref(r);
  } else {
     c->func = func_ref_failed;
  }
  return ret;
}

cell_t *val(intptr_t x) {
  cell_t *c = closure_alloc(1);
  c->func = func_ref_reduced;
  c->n = 1;
  c->val = x;
  return c;
}
/*
bool is_val(cell_t *c) {
  return c->func == func_val;
}
*/
bool is_ref(cell_t *c) {
  return c->func == func_ref ||
         c->func == func_ref_reduced ||
         c->func == func_ref_failed;
}

// max offset is 255
bool is_offset(cell_t *c) {
  return !((intptr_t)c & ~0xff);
}

// args must be >= 1
cell_t *func(reduce_t *f, int args) {
  assert(args >= 0);
  int size = calculate_cells(args);
  cell_t *c = closure_alloc(size);
  assert(c->func == 0);
  c->func = f;
  c->arg[0] = (cell_t *)(intptr_t)(args - 1);
  closure_set_ready(c, false);
  return c;
}

FUNC(add) {
  FUNC(f) {
    intptr_t z;
    bool s = reduce(c->arg[0]) && reduce(c->arg[1]);
    if(s) {
      cell_t *alt = closure_split(c);
      z = c->arg[0]->val + c->arg[1]->val;
      deref(c->arg[0]);
      deref(c->arg[1]);
      to_ref(c, z, alt, 0);
    } else {
      deref(c->arg[0]);
      deref(c->arg[1]);
    }
    return s;
  }
  return __reduce(func_f, c);
}

int closure_args(cell_t *c) {
  assert(is_closure(c));
  //if(is_val(c)) return 1;
  if(is_ref(c)) return 2;
  cell_t **p = c->arg;
  int n = 0;
  if(is_offset(*p)) {
    intptr_t o = (intptr_t)(*p) + 1;
    p += o;
    n += o;
  }
  while(is_closure(*p)) {
    p++;
    n++;
  }
  return n;
}

int closure_next_child(cell_t *c) {
  assert(is_closure(c));
  return is_offset(c->arg[0]) ? (intptr_t)c->arg[0] : 0;
}

void arg(cell_t *c, cell_t *a) {
  assert(is_closure(c) && is_closure(a));
  assert(!closure_is_ready(c));
  int i = closure_next_child(c);
  if(!is_cell(c->arg[i])) {
    c->arg[0] = (cell_t *)(intptr_t)(i - (closure_is_ready(a) ? 1 : 0));
    c->arg[i] = a;
    if(i == 0) closure_set_ready(c, closure_is_ready(a));
  } else {
    arg(c->arg[i], a);
    if(closure_is_ready(c->arg[i])) {
      if(i == 0) closure_set_ready(c, true);
      else --*(intptr_t *)c->arg; // decrement offset
    }
  }
}

cell_t *copy(cell_t *c) {
  int size = closure_cells(c);
  cell_t *new = closure_alloc(size);
  memcpy(new, c, size * sizeof(cell_t));
  return new;
}

cell_t *ref_args(cell_t *c) {
  int n = closure_args(c);
  while(n--)
    if(is_closure(c->arg[n]))
      c->arg[n] = ref(c->arg[n]);
  return c;
}

void to_ref(cell_t *c, intptr_t x, cell_t *n, cell_t *a) {
  if(is_ref(c)) return;
  closure_shrink(c, 1);
  c->func = func_ref_reduced;
  c->n = 1;
  c->val = x;
  c->next = n;
  c->alt = a;
}

void to_fail(cell_t *c) {
  closure_shrink(c,1);
  c->func = func_ref_failed;
  c->n = 1;
  c->val = 0;
  c->next = 0;
  c->alt = 0;
}

cell_t *ref(cell_t *c) {
  assert(is_closure(c));
  if(is_ref(c)) {
    ++c->n;
  } else {
    /* copy closure to a new location,         */
    /* then convert the closure to a reference */
    cell_t *new = copy(c);
    closure_shrink(c, 1);
    c->func = func_ref;
    c->n = 2;
    c->ptr = new;
  }
  return c;
}

cell_t *refn(cell_t *c, int n) {
  assert(is_closure(c));
  if(n <= 0) return c;
  if(is_ref(c)) {
    c->n += n;
  } else {
    /* copy closure to a new location,         */
    /* then convert the closure to a reference */
    cell_t *new = copy(c);
    closure_shrink(c, 1);
    c->func = func_ref;
    c->n = n+1;
    c->ptr = new;
  }
  return c;
}

cell_t *dup(cell_t *c) {
  assert(is_closure(c));
  if(closure_is_ready(c)) return ref(c);
  int args = closure_args(c);
  cell_t *tmp = copy(c);
  // c->arg[<i] are empty and c->arg[>i] are ready, so no need to copy
  // c->arg[i] only needs copied if filled
  int i = closure_next_child(c);
  if(is_closure(c->arg[i])) tmp->arg[i] = dup(c->arg[i]);
  while(++i < args) tmp->arg[i] = ref(c->arg[i]);
  return tmp;
}

bool is_nil(cell_t *c) {
  return !c;
}

bool is_cons(cell_t *c) {
  return c->func == func_cons;
}

cell_t *cons(cell_t *h, cell_t *t) {
  assert(is_nil(t) || is_cons(t));
  cell_t *c = closure_alloc(1);
  c->func = func_cons;
  c->arg[0] = h;
  c->arg[1] = t;
  return c;
}

void deref(cell_t *c) {
  if(!c /*is_closure(c)*/) return;
  assert(is_closure(c));
  if(is_ref(c)) {
    //printf("DEREF(%d) to %d\n", (int)(c - &cells[0]), (int)*(intptr_t *)&c->arg[0]-1);
    if(!--*(intptr_t *)&c->n) {
      if(c->func == func_ref)
	deref(c->ptr);
      closure_free(c);
    }
  } else {
    //printf("DEREF(%d) ", (int)(c - &cells[0]));
    //print_sexpr(c);
    closure_free(c);
  }
}

void drop(cell_t *c) {
  if(!is_closure(c)) return;
  if(is_ref(c)) {
    if(!--*(intptr_t *)&c->arg[0]) {
      if(c->func == func_ref)
	drop(c->ptr);
      closure_free(c);
    }
    /*
  } else if(is_val(c)) {
    closure_free(c);
    */
  } else {
    int i = closure_args(c) - 1;
    while(i--) drop(c->arg[i]);
    closure_free(c);
  }
}

// to do: reduce arg
FUNC(head) {
  FUNC(f) {
    cell_t *x = c->arg[0];
    if (!is_cons(x)) return false;
    drop(x->arg[1]);
    c->ptr = x->arg[0];
    return true;
  }
  return __reduce(func_f, c);
}

FUNC(tail) {
  FUNC(f) {
    cell_t *x = c->arg[0];
    if(!is_cons(x)) return false;
    drop(x->arg[0]);
    c->ptr = x->arg[1];
    return true;
  }
  return __reduce(func_f, c);
}

cell_t *pushl(cell_t *a, cell_t *b) {
  assert(is_closure(a) && is_closure(b));
  assert(is_cons(b) || is_nil(b));
  if(is_nil(b)) {
    return cons(a, b);
  }
  cell_t *h = b->arg[0];
  if(closure_is_ready(h)) {
    return cons(a, b);
  } else {
    arg(h, a);
    return b;
  }
}

FUNC(pushl) {
  bool ret = reduce(c->arg[1]);
  if(ret) {
    cell_t *alt = closure_split(c);
    cell_t *v = pushl(c->arg[0], c->arg[1]->ptr);
    to_ref(c, (intptr_t)v, 0, alt);
  }
  deref(c->arg[1]);
  return ret;
}

FUNC(quot) {
  to_ref(c, (intptr_t)cons(c->arg[0], nil), 0, 0);
  return true;
}

FUNC(cons) {
  to_ref(c, (intptr_t)cons(c->arg[0], c->arg[1]), 0, 0);
  return true;
}

bool popr(cell_t *c) {
  FUNC(f) {
    bool ret = reduce(c->arg[0]);
    if(ret) {
      intptr_t v = c->arg[0]->val;
      cell_t *alt = closure_split(c);
      deref(c->arg[0]);
      //to_val(c, v, 0, alt);
    } else {
      deref(c->arg[0]);
    }
    return ret;
  }
  return __reduce(func_f, c);
}

FUNC(popr) {
  FUNC(f) {
    cell_t *x, *tmp;
    if(!reduce(c->arg[0])) goto fail;
    if(!popr(c->arg[0]->ptr)) goto fail;
    if(c->arg[0]->ptr->alt) {
      conc_alt(c->arg[0], val((intptr_t)c->arg[0]->ptr->alt));
    }
    closure_split(c);
    deref(c->arg[0]->ptr);
    deref(c->arg[0]);
    return true;
  fail:
    deref(c->arg[0]);
    return false;
  }
  return __reduce(func_f, c);
}

FUNC(alt) {
  cell_t *p = c->arg[0];
  bool ret = reduce(p);
  if(ret) {
    cell_t *alt = func((reduce_t *)~(intptr_t)0, 2);
    if(p->alt) {
      alt->arg[0] = p->alt;
      alt->arg[1] = c->arg[1];
    } else {
      alt->arg[0] = c->arg[1];
      alt->arg[1] = 0;
    }
    //conc_alt(p->alt, c->arg[1]);
    to_ref(c, p->val, p->next, alt);
  }
  deref(p);
  return ret;
}

cell_t *compose(cell_t *a, cell_t *b) {
  assert(is_closure(a) && is_closure(b));
  assert((is_cons(a) || is_nil(a)) &&
	 (is_cons(b) || is_nil(b)));
  if(is_nil(a)) return b;
  if(is_nil(b)) return a;
  cell_t *h = a->arg[0];
  cell_t *bp = compose(a->arg[1], b);
  return pushl(h, bp);
}

FUNC(compose) {
  c->ptr = compose(c->arg[0], c->arg[1]);
  return true;
}

cell_t *conc_alt(cell_t *a, cell_t *b) {
  if(!a) return nptr(b);
  if(b == (void *)~0) return a;

  cell_t *p = a, *r;
  while(r = nptr(p->alt)) p = r;
  p->alt = b;
  return a;
}

FUNC(assert) {
  FUNC(f) {
    bool ret = reduce(c->arg[0]);
    intptr_t v = c->val;
    deref(c->arg[0]);
    return ret && v;
  }
  return __reduce(func_f, c);
}

void alloc_test() {
  int i, j;
  cell_t *a[30];
  for(j = 0; j < 50; j++) {
    for(i = 0; i < LENGTH(a); i++) {
      a[i] = func(func_add, 9);
    }
    for(i = 0; i < LENGTH(a); i++) {
      closure_free(a[i]);
    }
  }
}

#define addr(x) printf("addr(" #x ") = %d\n", (int)(x - &cells[0]))

void test() {
  cell_t *a, *b, *c, *d, *e, *f, *g, *h, *i, *j;

  g = func(func_add, 2);
  e = func(func_add, 2);
  c = func(func_add, 2);

  h = val(10);
  b = val(2);

  a = val(1);

  arg(e, c);
  arg(e, a);
  arg(e, b);
  show(closure_args(e));
  f = dup(e);
  d = val(4);
  arg(e, d);
  arg(f, dup(a));
  arg(g, h);
  arg(g, f);

  /* leakiness below */
  i = func(func_quot, 1);
  arg(i, g);
  j = func(func_popr, 1);
  arg(j, i);

  intptr_t ret;
  reduce(e);
  show((int)e->val);
  deref(e);
  reduce(j);
  show((int)j->val);
  deref(j);
}

void print_sexpr_help(cell_t *);

void print_list_help(cell_t *c) {
  if(is_nil(c)) {
    printf(" ] ");
    return;
  }
  assert(is_cons(c));
  print_sexpr_help(c->arg[0]);
  print_list_help(c->arg[1]); 
}

void print_list(cell_t *c) {
  printf(" [");
  print_list_help(c);
}

void print_sexpr_help(cell_t *r) {
  if(!is_closure(r)) {
    printf(" ?");
    return;
  } else if(is_cons(r) || is_nil(r)) {
    print_list(r);
    return;
  } else if(r->func == func_ref) {
    //print_sexpr_help(r->arg[1]);
    printf(" (ref");
    print_sexpr_help((cell_t *)r->val);
    printf(")");
    return;
  } else if(r->func == func_ref_reduced) {
    printf(" %d", (int)r->val);
    return;
  } else if(r->func == func_ref_failed) {
    printf(" (ref FAIL)");
  } else if(r->func == func_nil) {
    printf(" nil");
    return;
  }

  intptr_t f = ((intptr_t)r->func) & ~1;
  char *n;
  int i;
  int args = closure_args(r);
  /*
  if(is_val(r)) {
    printf(" %d", (int)(intptr_t)r->arg[0]);
    return;
  }
  */
# define CASE(x) if(f == (intptr_t)func_##x) n = #x

  CASE(add);
  CASE(pushl);
  CASE(popr);
  CASE(assert);

# undef CASE

  printf(" (%s", n);

  for(i = 0; i < args; i++) {
    print_sexpr_help(r->arg[i]);
  }

  printf(")");
}

void print_sexpr(cell_t *r) {
  print_sexpr_help(r);
  printf("\n");
}

void test2() {
  int cnt;
  intptr_t x;
  cell_t *a, *b, *c, *d, *e, *p, *t, *z, *y;
  cell_t *a_, *b_, *c_, *e_;
  // 1 [2 +] pushl popr
  a = func(func_add, 2);
  e = val(2);
  arg(a, e);
  b = cons(a, nil);

  a_ = func(func_add, 2);
  e_ = func(func_alt, 2);
  arg(e_, val(3));
  arg(e_, val(7));
  y = func(func_alt, 2);
  arg(y, e_);
  arg(y, val(20));
  arg(a_, y);
  b_ = cons(a_, nil);
 
  z = func(func_alt, 2);
  arg(z, b_);
  arg(z, b);
 
  c = func(func_pushl, 2);
  arg(c, z);
  arg(c, val(1));

  d = func(func_popr, 1);
  arg(d, c);
  //print_sexpr(d);

  p = d;
  cnt = 0;
  while(p && reduce(p)) {
    t = p;
    show((int)p->val);
    p = p->alt;
    deref(t);
    cnt++;
  }
  deref(p);
  show(cnt);
}

void test3() {
  int cnt;
  intptr_t x;
  cell_t *a, *b, *c, *d, *e, *f, *g, *h, *i, *k, *l, *m, *n, *p, *t;

  a = func(func_add, 2);
  b = val(2);
  //e = func(func_assert, 1);
  //arg(e, val(0));
  e = val(23);
  k = func(func_alt, 2);
  arg(k, b);
  arg(k, e);
  c = val(3);
  d = val(7);
  l = func(func_alt, 2);
  arg(l, c);
  arg(l, d);
  arg(a, k);
  arg(a, l);
  f = func(func_add, 2);
  g = val(31);
  h = val(63);
  i = val(127);
  m = func(func_alt, 2);
  arg(m, g);
  arg(m, h);
  n = func(func_alt, 2);
  arg(n, i);
  arg(n, m);
  arg(f, n);
  arg(f, a);
  
  addr(a);
  addr(b);
  addr(c);
  addr(d);
  addr(e);
  addr(f);
  addr(g);
  addr(h);
  addr(i);
  addr(k);
  addr(l);
  addr(m);
  addr(n);
  
  p = f;
  cnt = 0;
  while(p && reduce(p)) {
    t = p;
    show((int)p->val);
    p = p->alt;
    deref(t);
    cnt++;
  }
  deref(p);
  show(cnt);
}

void test4() {
  cell_t *a, *b, *c, *d;
  a = val(2);
  b = val(5);
  c = func(func_add, 2);
  arg(c, a);
  arg(c, b);
  d = ref_args(copy(c));
  intptr_t x, y;
  reduce(c);
  show((int)c->val);
  deref(c);
  reduce(d);
  show((int)d->val);
  deref(d);
}

void test5() {
  int cnt;
  intptr_t x;
  cell_t *a, *b, *c, *d, *e, *f, *g, *p, *t;

  a = val(1);
  b = val(2);
  
  c = func(func_alt, 2);
  arg(c, a);
  arg(c, b);
      
  d = val(10);
  f = func(func_alt, 2);
  arg(f, val(20));
  arg(f, val(30));
  e = func(func_alt, 2);
  arg(e, d);
  arg(e, f);

  g = func(func_add, 2);
  arg(g, c);
  arg(g, e);
  
  p = g;
  cnt = 0;
  while(p && reduce(p)) {
    t = p;
    show((int)p->val);
    p = p->alt;
    deref(t);
    cnt++;
  }
  deref(p);
  show(cnt);

}

FUNC(id) {
  cell_t *p = c->arg[0];
  bool ret = reduce(p);
  to_ref(c, p->val, p->next, p->alt);
  deref(p);
  return true;
}

void test6() {
  cell_t *p = func(func_id, 1);
  intptr_t x;
# define V(x) arg(p, val(x));
# define P arg(p, func(func_add, 2));

  // 5 40 + 300 + 2000 10000 + +
  // + -> P, x -> V(x), reverse order
  P P V(10000) V(2000) P V(300) P V(40) V(5) 

    reduce(p);
  show((int)p->val);
  deref(p);
}

void test7() {
  cell_t *a, *b, *c, *p, *t;
  reduce_t *f;
  a = func(func_alt, 2);
  arg(a, val(4));
  arg(a, val(3));
  /*
  b = func(func_alt, 2);
  arg(b, val(2));
  arg(b, val(1));
  c = func(func_alt, 2);
  arg(c, b);
  arg(c, a);
  */
  p = a;

  t = p;
  f = p->func;
  reduce(t);
  show((int)t->val);
  p = t->alt;
  deref(t);

  while(p) {
    t = p;
    if(reducef(f, t))
      show((int)t->val);
    p = t->alt;
    deref(t);
  }
}

void check_free() {
  int i;
  for(i = 1; i < LENGTH(cells); i++) {
    if(is_closure(&cells[i]))
      printf("LEAK: %d\n", i);
  }
}

int main() {
  cells_init();
  //alloc_test();
  //test();
  //test2();
  //test3();
  //test4();
  //test5();
  //test6();
  test7();
  check_free();
  return 0;
}
