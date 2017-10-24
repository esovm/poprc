/* Copyright 2012-2017 Dustin DeWeese
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

#include "rt_types.h"
#include <string.h>
#include <inttypes.h>

#if INTERFACE
#include <stdio.h>
#endif

#include "gen/error.h"
#include "gen/cells.h"
#include "gen/rt.h"
#include "gen/primitive.h"
#include "gen/special.h"
#include "gen/test.h"
#include "gen/support.h"
#include "gen/trace.h"
#include "gen/map.h"
#include "gen/parse.h"
#include "gen/print.h"
#include "gen/module.h"
#include "gen/user_func.h"
#include "gen/list.h"
#include "gen/log.h"

static BITSET_INDEX(visited, cells);
static BITSET_INDEX(marked, cells);

void mark_cell(cell_t *c) {
  if(is_cell(c)) {
    set_bit(marked, CELL_INDEX(c));
  }
}

char const *show_alt_set(uintptr_t as) {
  static char out[sizeof(as)*4+1];
  char *p = out;
  unsigned int n = sizeof(as)*4;
  const unsigned int shift = sizeof(as) * 8 - 2;
  uintptr_t mask = ((uintptr_t)3) << shift;

  while(!(as & mask) && n) {
    as <<= 2;
    n--;
  }
  while(n--) {
    switch(as >> shift) {
    case 0: *p++ = 'X'; break;
    case 1: *p++ = '0'; break;
    case 2: *p++ = '1'; break;
    case 3: *p++ = 'E'; break;
    }
    as <<= 2;
  }
  *p++ = '\0';
  return out;
}

// only valid on primitives
char const *entry_function_name(cell_t *e) {
  if(is_user_func(e)) return NULL;
  const char *s = e->word_name;
  while(*s++);
  return s;
}

char const *function_name(reduce_t *f) {
#define CASE(x) if(f == func_##x) return #x
  CASE(value);
  CASE(fail);
  CASE(dep);
  CASE(dep_entered);
  CASE(ap);
  CASE(exec);
#undef CASE
  const char *s = NULL;
  FORMAP(i, primitive_module) {
    cell_t *e = (cell_t *)primitive_module[i].second;
    if(e->func == f) {
      s = entry_function_name(e);
      break;
    }
  }
  assert_error(s);
  return s;
}

void get_name(const cell_t *c, const char **module_name, const char **word_name) {
  if(is_user_func(c)) {
    cell_t *e = c->expr.arg[closure_in(c)];
    if(is_trace_cell(e)) {
      *module_name = e->module_name;
      *word_name = e->word_name;
    } else {
      *module_name = "null";
      *word_name = "null";
    }
  } else {
    *module_name = PRIMITIVE_MODULE_NAME;
    *word_name = function_name(c->func);
  }
}

char const *function_token(const cell_t *c) {
  static char ap_str[] = "ap00";
  reduce_t *f = c->func;
  if(f == func_ap) {
    csize_t
      in = closure_in(c),
      out = closure_out(c),
      n = closure_args(c);

    if(n == 2) {
      if(out) {
        return "popr";
      } else {
        return "pushl";
      }
    } else {
      ap_str[2] = in <= 10 ? '0' + in - 1 : 'X';
      ap_str[3] = out <= 9 ? '0' + out : 'X';
      return ap_str;
    }
  }

  FORMAP(i, primitive_module) {
    pair_t *p = &primitive_module[i];
    cell_t *e = (cell_t *)p->second;
    if(e && e->func == f)
      return (char *)p->first;
  }
  return NULL;
}

/* Graphviz graph generation */
void make_graph(char const *path, cell_t const *c) {
  FILE *f = fopen(path, "w");
  fprintf(f, "digraph g {\n"
             "label=\"%s\";\n"
             "labelloc=bottom\n"
             "labeljust=right\n"
             "graph [\n"
             "rankdir = \"RL\"\n"
             "];\n", path);
  zero(visited);
  graph_cell(f, c);
  fprintf(f, "}\n");
  fclose(f);
}

void make_graph_all(char const *path) {
  static char autopath[16];
  static unsigned int autopath_count = 0;
  if(!path && autopath_count < 1000) {
    snprintf(autopath, sizeof(autopath), "graph%03d.dot", autopath_count++);
    path = autopath;
  }
  char label[sizeof(tag_t) + 1];
  label[sizeof(tag_t)] = 0;
  get_tag(label);
  FILE *f = fopen(path, "w");
  fprintf(f, "digraph g {\n"
             "label=\"%s %s\";\n"
             "labelloc=bottom\n"
             "labeljust=right\n"
             "graph [\n"
             "rankdir = \"RL\"\n"
             "];\n", path, label);
  zero(visited);
  FOREACH(i, cells) {
    graph_cell(f, &cells[i]);
  }
  fprintf(f, "}\n");
  fclose(f);
}

void print_cell_pointer(FILE *f, cell_t *p) {
  if(p == &fail_cell) {
    fprintf(f, "<font color=\"red\">&amp;fail_cell</font>");
  } else if(p == &nil_cell) {
    fprintf(f, "<font color=\"gray70\">&amp;nil_cell</font>");
  } else if(is_cell(p)) {
    fprintf(f, "<font color=\"gray70\">&amp;cells[%d]</font>", CELL_INDEX(p));
  } else {
    fprintf(f, "<font color=\"gray70\">%p</font>", (void *)p);
  }
}

void graph_cell(FILE *f, cell_t const *c) {
  c = clear_ptr(c);
  if(!is_closure(c) || !is_cell(c)) return;
  int node = CELL_INDEX(c);
  int border = check_bit(marked, node) ? 4 : 0;
  clear_bit(marked, node);
  if(check_bit(visited, node)) return;
  set_bit(visited, node);
  csize_t n = closure_args(c);
  csize_t s = calculate_cells(n);

  COUNTUP(i, s) {
    set_bit(visited, node+i);
  }

  if(c->n == PERSISTENT || is_map(c)) return; // HACK

  /* print node attributes */
  fprintf(f, "node%d [\nlabel =<", node);

  const char *module_name, *word_name;
  get_name(c, &module_name, &word_name);

  fprintf(f, "<table border=\"%d\" cellborder=\"1\" cellspacing=\"0\"><tr><td port=\"top\" bgcolor=\"black\"><font color=\"white\"><b>(%d) ",
          border,
          node);
  if(is_user_func(c)) {
    fprintf(f, "%s.", module_name);
  }
  fprintf(f, "%s%s ",
          word_name,
          closure_is_ready(c) ? "" : "*");
  if(is_value(c)) {
    fprintf(f, "%s ", show_type_all_short(c->value.type));
  }

  if(is_root(c)) {
    fprintf(f, "(x%u)", (unsigned int)c->n + 1);
  } else {
    fprintf(f, "x%u", (unsigned int)c->n + 1);
  }

  fprintf(f, "</b></font></td></tr>");
  if(c->alt) {
    fprintf(f, "<tr><td port=\"alt\">alt: ");
    print_cell_pointer(f, c->alt);
    fprintf(f, "</td></tr>");
  }
  if(is_value(c)) {
    if(c->value.alt_set) {
      fprintf(f, "<tr><td>alt_set: X%s</td></tr>",
              show_alt_set(c->value.alt_set));
    }
    if(is_list(c)) {
      csize_t n = list_size(c);
      if(n && (FLAG(c->value.type, T_ROW))) {
        n--;
        fprintf(f, "<tr><td port=\"ptr%u\" bgcolor=\"gray90\" >row: ", (unsigned int)n);
        print_cell_pointer(f, c->value.ptr[n]);
        fprintf(f, "</td></tr>");
      }
      while(n--) {
        fprintf(f, "<tr><td port=\"ptr%u\">ptr: ", (unsigned int)n);
        print_cell_pointer(f, c->value.ptr[n]);
        fprintf(f, "</td></tr>");
      }
    } else if(is_fail(c)) {
      fprintf(f, "<tr><td bgcolor=\"red\">FAIL</td></tr>");
    } else if(is_var(c)) {
      if(c->value.tc.entry) {
        fprintf(f, "<tr><td bgcolor=\"orange\">trace: %d.%d",
                entry_number(c->value.tc.entry),
                (int)c->value.tc.index);
        if(FLAG(c->value.type, T_DEP)) {
          fprintf(f, "[%d]", (int)c->pos);
        }
        fprintf(f, "</td></tr>");
      }
    } else {
      int n = val_size(c);
      while(n--)
        fprintf(f, "<tr><td bgcolor=\"yellow\">val: %" PRIdPTR "</td></tr>", c->value.integer[n]);
    }
  } else {
    COUNTUP(i, closure_in(c)) {
      fprintf(f, "<tr><td port=\"arg%u\">", (unsigned int)i);
      print_cell_pointer(f, c->expr.arg[i]);
      fprintf(f, "</td></tr>");
    }
    RANGEUP(i, closure_args(c) - closure_out(c), closure_args(c)) {
      fprintf(f, "<tr><td port=\"arg%u\">", (unsigned int)i);
      fprintf(f, "out: ");
      print_cell_pointer(f, c->expr.arg[i]);
      fprintf(f, "</td></tr>");
    }
    if(c->func == func_id && c->expr.arg[1]) {
      fprintf(f, "<tr><td>alt_set: X%s</td></tr>",
              show_alt_set((alt_set_t)c->expr.arg[1]));
    }
    if(c->pos) {
      fprintf(f, "<tr><td bgcolor=\"orange\">trace: %d</td></tr>",
              entry_number(trace_expr_entry(c->pos)));
    }
  }
  fprintf(f, "</table>>\nshape = \"none\"\n];\n");

  /* print edges */
  if(is_cell(c->alt)) {
    cell_t *alt = clear_ptr(c->alt);
    fprintf(f, "node%d:alt -> node%d:top;\n",
            node, CELL_INDEX(alt));
    graph_cell(f, c->alt);
  }
  if(is_value(c)) {
    if(is_list(c)) {
      csize_t n = list_size(c);
      while(n--) {
        if(is_cell(c->value.ptr[n])) {
          fprintf(f, "node%d:ptr%u -> node%d:top;\n",
                  node, (unsigned int)n, CELL_INDEX(c->value.ptr[n]));
          graph_cell(f, c->value.ptr[n]);
        }
      }
    }
  } else {
    csize_t start_out = closure_args(c) - closure_out(c);
    COUNTUP(i, start_out) {
      cell_t *arg = clear_ptr(c->expr.arg[i]);
      if(is_cell(arg)) {
        fprintf(f, "node%d:arg%d -> node%d:top;\n",
                node, (unsigned int)i, CELL_INDEX(arg));
        graph_cell(f, arg);
      }
    }
    RANGEUP(i, start_out, n) {
      cell_t *arg = clear_ptr(c->expr.arg[i]);
      if(is_cell(arg)) {
        fprintf(f, "node%d:arg%d -> node%d:top [color=lightgray];\n",
                node, (unsigned int)i, CELL_INDEX(arg));
        graph_cell(f, arg);
      }
    }
  }
}

void show_int(cell_t const *c) {
  assert_error(c && type_match(T_INT, c));
  int n = val_size(c);
  switch(n) {
  case 0: printf(" ()"); break;
  case 1: printf(" %" PRIdPTR, c->value.integer[0]); break;
  default:
    printf(" (");
    while(n--) printf(" %" PRIdPTR, c->value.integer[n]);
    printf(" )");
    break;
  }
}

void show_float(cell_t const *c) {
  assert_error(c && type_match(T_FLOAT, c));
  printf(" %g", c->value.flt[0]);
}

bool any_alt_overlap(cell_t const * const *p, csize_t size) {
  uintptr_t  mask = 0;
  while(size--) {
    if(is_value(*p)) {
      alt_set_t m = as_mask((*p)->value.alt_set);
      if(mask & m) return true;
      mask |= m;
    }
    p++;
  }
  return false;
}

csize_t any_conflicts(cell_t const * const *p, csize_t size) {
  uintptr_t as = 0;
  COUNTUP(i, size) {
    if(is_value(*p)) {
      as |= (*p)->value.alt_set;
      if(as_conflict(as)) {
        return size - i;
      }
    }
    p++;
  }
  return 0;
}

void show_list_elements(cell_t const *c) {
  csize_t n = list_size(c);
  if(!n) return;
  if(is_row_list(c)) {
    show_list_elements(c->value.ptr[--n]);
  }
  COUNTDOWN(i, n) {
    show_one(c->value.ptr[i]);
  }
}

void show_list(cell_t const *c) {
  assert_error(c && is_list(c));
  csize_t n = list_size(c);
  if(!n) {
    printf(" []");
  } else {
    printf(" [");
    show_list_elements(c);
    printf(" ]");
  }
}

void show_func(cell_t const *c) {
  int n = closure_args(c);
  char const *s = function_token(c);
  if(!s) return;
  if(is_placeholder(c)) printf(" ?%d =", CELL_INDEX(c));
  COUNTUP(i, n) {
    cell_t *arg = c->expr.arg[i];
    if(is_closure(arg)) {
      show_one(arg);
    }
  }
  if(c->func != func_id) { // to reduce noise and allow diff'ing test output
    printf(" %s", s);
  }
}

void show_var(cell_t const *c) {
  assert_error(is_var(c));
  if(is_list(c)) {
    show_list(c);
  } else {
    printf(" ?%c%d", type_char(c->value.type), CELL_INDEX(c));
  }
}

void show_one(cell_t const *c) {
  if(!c) {
    printf(" []");
  } else if(!is_closure(c)) {
    printf(" ?");
  } else if(!is_value(c)) {
    show_func(c);
  } else if(is_fail(c)) {
    printf(" {}");
  } else if(is_var(c)) {
    show_var(c);
  } else if(type_match(T_INT, c)) {
    show_int(c);
  } else if(type_match(T_FLOAT, c)) {
    show_float(c);
  } else if(type_match(T_LIST, c)) {
    show_list(c);
  } else if(type_match(T_SYMBOL, c)) {
    val_t x = c->value.integer[0];
    const char *str = symbol_string(x);
    if(str) {
      printf(" %s", str);
    } else {
      printf(" UnknownSymbol");
    }
  } else {
    printf(" ?");
  }
}

void show_alts(cell_t const *c) {
  cell_t const *p = c;
  while(p) {
    putchar(' ');
    show_list_elements(p);
    putchar('\n');
    p = p->alt;
  }
}

char *show_type(type_t t) {
#define _case(x) case x: return #x
  switch(t.exclusive) {
  _case(T_ANY);
  _case(T_INT);
  _case(T_IO);
  _case(T_LIST);
  _case(T_SYMBOL);
  _case(T_MAP);
  _case(T_STRING);
  _case(T_RETURN);
  _case(T_FUNCTION);
  _case(T_BOTTOM);
  default: return "???";
  }
#undef case
}

// unsafe
char *show_type_all(type_t t) {
  const static char *type_flag_name[] = {
    "T_VAR",
    "T_FAIL",
    "T_TRACED",
    "T_ROW"
  };
  static char buf[64];
  char *p = buf;
  p += sprintf(p, "%s", show_type(t));
  FOREACH(i, type_flag_name) {
    if(FLAG(t, 0x80 >> i)) {
      p += sprintf(p, "|%s", type_flag_name[i]);
    }
  }
  return buf;
}

char type_char(type_t t) {
  switch(t.exclusive) {
  case T_ANY: return 'a';
  case T_INT: return 'i';
  case T_IO: return 'w';
  case T_LIST: return 'l';
  case T_SYMBOL: return 's';
  case T_MAP: return 'm';
  case T_STRING: return 'S';
  case T_RETURN: return 'r';
  case T_FUNCTION: return 'f';
  case T_BOTTOM: return '0';
  }
  return 'x';
}

// unsafe
char *show_type_all_short(type_t t) {
  const static char type_flag_char[] = {
    '?',
    '!',
    '.',
    '@'
  };
  static char buf[LENGTH(type_flag_char) + 1];

  char *p = buf;

  FOREACH(i, type_flag_char) {
    if(FLAG(t, 0x80 >> i)) {
      *p++ = type_flag_char[i];
    }
  }

  *p++ = type_char(t);
  *p = 0;
  return buf;
}
