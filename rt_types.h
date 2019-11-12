/* Copyright 2012-2018 Dustin DeWeese
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

#ifndef __RT_TYPES__
#define __RT_TYPES__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#include "startle/macros.h"
#include "startle/types.h"
#include "macros.h"

typedef unsigned int uint;

typedef uint16_t csize_t;

typedef enum __attribute__((packed)) type_t {
  T_ANY = 0, /**< type variable */
  T_INT,
  T_LIST, /**< a list/quote */
  T_SYMBOL,
  T_MAP,
  T_STRING,
  T_RETURN, /**< a list of return values, trace ONLY */
  T_FLOAT,
  T_OPAQUE,
  T_MODULE, /**< module definition ONLY */
  T_BOTTOM, /**< the uninhabited type, or unknown */
  T_FAIL // TODO use T_BOTTOM
} type_t;

typedef union cell cell_t;
typedef struct expr expr_t;
typedef struct value value_t;
typedef struct tok_list tok_list_t;
typedef struct entry entry_t;
typedef struct mem mem_t;
typedef struct context context_t;
typedef struct tcell tcell_t;

typedef uintptr_t alt_set_t;
typedef int16_t refcount_t;
typedef intptr_t val_t;

#ifdef __clang__
#pragma clang diagnostic ignored "-Warray-bounds"
#pragma clang diagnostic ignored "-Wzero-length-array"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wgnu-folding-constant"
#pragma clang diagnostic ignored "-Wgnu-empty-initializer"
#endif

typedef struct qsize {
  csize_t in, out;
} qsize_t;


// define the file_id enum
#define FILE_ID(id, str)                        \
  FILE_ID_##id,

typedef enum __attribute__((packed)) file_id {
  FILE_ID_NONE = 0,
  #include "file_ids.h"
  FILE_ID_COUNT
} file_id_t;

typedef union location {
  uintptr_t raw;
  struct {
    file_id_t file;
    uint16_t line;
  };
} location_t;

#undef FILE_ID
static_assert(sizeof(location_t) == sizeof_field(location_t, raw), "location_t wrong size");

struct context {
  context_t *up;
  cell_t *src;
  range_t bound;
  alt_set_t alt_set;
  int priority;
  location_t loc;
  qsize_t s;
  type_t t;
  uint8_t pos;
  uint8_t depth;
  bool retry;
  bool inv;
};

typedef enum response {
  SUCCESS = 0,
  DELAY,
  RETRY,
  FAIL
} response;

#define TR_FINAL 0x01

typedef union tr {
  cell_t *ptr;
  int entry;
  struct __attribute__((packed)) {
    int16_t index;
    uint8_t flags;
  };
} tr;

#define FLAG_expr (expr, EXPR)
#define EXPR_NEEDS_ARG 0x02
#define EXPR_TRACE     0x04
#define EXPR_PARTIAL   0x10
#define EXPR_RECURSIVE 0x20 // exec
#define EXPR_NO_UNIFY  0x40 // exec
#define EXPR_ROW       0x80 // placeholder

/* unevaluated expression */
struct __attribute__((packed)) expr {
  uint8_t out, flags;
  union {
    cell_t *arg[2];
    val_t idx[2];
    struct {
      cell_t *arg0; // padding
      union {
        alt_set_t alt_set;
        val_t symbol;
      };
    };
  };
};

// value flags
#define FLAG_value (value, VALUE)
#define VALUE_ABBREV     0x01
#define VALUE_TRACED     0x02
#define VALUE_INLINE     0x04
#define VALUE_DEP        0x08
#define VALUE_BOUNDED    0x10
#define VALUE_LINEAR     0x20
#define VALUE_ROW        0x40
#define VALUE_VAR        0x80

/* reduced value */
struct __attribute__((packed)) value {
  type_t type;
  uint8_t flags;
  alt_set_t alt_set;
  union {
    struct {
      tcell_t *var; /* variable */
      union {
        val_t integer;  /* integer */
        double flt;     /* float   */
        char str[0];    /* string  */
        cell_t *ptr[2]; /* list    */
        range_t range;  /* range   */
        struct {        /* symbol | opaque */
          val_t symbol;
          union {
            val_t id;
            void *opaque;
          };
        };
      };
    };
    pair_t map[1]; /* map */
  };
};

/* token list */
struct __attribute__((packed)) tok_list {
  csize_t length;
  const char *location, *line;
  cell_t *next;
};

/* unallocated memory */
struct __attribute__((packed)) mem {
  csize_t __padding;
  cell_t *prev, *next;
};

#define FLAG_entry (entry, ENTRY)
#define ENTRY_PRIMITIVE     0x0001
#define ENTRY_TRACE         0x0002
#define ENTRY_SYNC          0x0004
#define ENTRY_QUOTE         0x0008
#define ENTRY_PARTIAL       0x0010
#define ENTRY_RAM           0x0020
#define ENTRY_BLOCK         0x0040
#define ENTRY_COMPLETE      0x0080
#define ENTRY_RECURSIVE     0x0100
#define ENTRY_MUTUAL        0x0200
#define ENTRY_FORCED_INLINE 0x0400
#define ENTRY_STACK         0x0800
#define ENTRY_RETURN_ADDR   0x1000

typedef struct wrap_data {
  cell_t *initial;
  cell_t *expand;
  tcell_t *entry;
  uintptr_t dep_mask;
} wrap_data;

/* word entry */
struct __attribute__((packed)) entry {
  uint16_t flags;
  uint8_t alts, sub_id;
  csize_t in, out, len;
  tcell_t *parent;
  union {
    wrap_data *wrap;
    tcell_t *compact;
  };
};

typedef enum char_class_t {
  CC_NONE,
  CC_NUMERIC,
  CC_FLOAT,
  CC_ALPHA,
  CC_SYMBOL,
  CC_BRACKET,
  CC_VAR,
  CC_COMMENT,
  CC_DOT,
  CC_STRING
} char_class_t;


// define the op enum
#define OP__ITEM(file, line, name)              \
  OP_##name,

typedef enum __attribute__((packed)) op {
  OP__ITEM(FILEBASE, __LINE__, null)
  #include "op_list.h"
  OP_COUNT
} op;

#undef OP__ITEM

union cell {
  /* op indicates the type:
   * OP_null      -> mem
   * OP_value     -> value
   * otherwise    -> expr
   */
  uintptr_t c[8];
#define CELL_STRUCT_CONTENTS                                            \
  struct __attribute__((packed)) {                                      \
    union {                                                             \
      cell_t *alt;                                                      \
      const char *word_name; /* entry */                                \
    };                                                                  \
    union {                                                             \
      cell_t *tmp;                                                      \
      val_t tmp_val;                                                    \
      const char *module_name; /* entry */                              \
      char_class_t char_class; /* tok_list */                           \
    };                                                                  \
    enum op op;                                                         \
    union {                                                             \
      uint8_t pos;                                                      \
      uint8_t priority; /* for use in func_list() & delay_branch() */   \
    };                                                                  \
    refcount_t n;                                                       \
    csize_t size;                                                       \
    union {                                                             \
      expr_t expr;                                                      \
      value_t value;                                                    \
      tok_list_t tok_list;                                              \
      entry_t entry;                                                    \
      mem_t mem;                                                        \
    };                                                                  \
  }
/* end of CELL_STRUCT_CONTENTS */
  CELL_STRUCT_CONTENTS;
};

#define VALUE_OFFSET(f) ((offsetof(cell_t, value.f) - offsetof(cell_t, expr.arg)) / sizeof(cell_t *))

#ifndef EMSCRIPTEN
static_assert(sizeof(cell_t) == sizeof_field(cell_t, c), "cell_t wrong size");
#endif

static_assert(sizeof(tr) == sizeof_field(cell_t, expr.arg[0]), "tr wrong size");

#define ASSERT_ALIAS(s, f0, f1) \
  static_assert(offsetof(s, f0) == offsetof(s, f1), #f0 " should alias " #f1 " in " #s)

#define ASSERT_VALUE_OFFSET(f) ASSERT_ALIAS(cell_t, expr.arg[VALUE_OFFSET(f)], value.f)

ASSERT_VALUE_OFFSET(integer);
ASSERT_VALUE_OFFSET(flt);
ASSERT_VALUE_OFFSET(str);
ASSERT_VALUE_OFFSET(ptr);
ASSERT_VALUE_OFFSET(map);

ASSERT_ALIAS(cell_t, expr.flags, value.flags);
ASSERT_ALIAS(cell_t, expr.arg[1], expr.alt_set);

static_assert(offsetof(cell_t, c) == 0, "offset of cell_t.c should be 0");

typedef struct stats_t {
  int reduce_cnt, fail_cnt, alloc_cnt, max_alloc_cnt, trace_cnt;
  clock_t start, stop;
  uint8_t alt_cnt;
} stats_t;

#ifdef EMSCRIPTEN
#define strnlen(s, n) strlen(s)
#endif

// Maximum number of alts
#define AS_SIZE (sizeof(alt_set_t) * 4)
#define AS_MASK ((alt_set_t)0x5555555555555555)
#define ALT_SET_IDS AS_SIZE

#define SYM_False     0
#define SYM_True      1
#define SYM_IO        2
#define SYM_Dict      3
#define SYM_Something 4
#define SYM_Array     5
#define SYM_File      6

#define PERSISTENT ((refcount_t)-15)

#define PRIMITIVE_MODULE_PREFIX __primitive
#define PRIMITIVE_MODULE_NAME STRINGIFY(PRIMITIVE_MODULE_PREFIX)

typedef struct list_iterator {
  cell_t **array;
  csize_t index, size;
  bool row;
} list_iterator_t;

void breakpoint();

#define COMMAND(name, desc) void command_##name(UNUSED cell_t *rest)

// trace flags
#define FLAG_trace (trace, TRACE)
#define TRACE_INCOMPLETE 0x0001
#define TRACE_TRACED     0x0002
#define TRACE_USED       0x0004
#define TRACE_IMMEDIATE  0x0008
#define TRACE_CHANGES    0x0010
#define TRACE_DECL       0x0020
#define TRACE_NO_SKIP    0x0040
#define TRACE_JUMP       0x0080

typedef struct trace {
  union {
    range_t range;
    uint8_t symbol_set[sizeof(range_t)];
  };
  alt_set_t range_as;
  uint16_t flags;
  union {
    csize_t prev_cells;
    csize_t extension;
  };
  uint32_t hash;
  type_t type;
} trace_t;

struct tcell {
  trace_t trace;
  union {
    CELL_STRUCT_CONTENTS;
    cell_t c;
  };
};

typedef struct scratch {
  range_t range;
} scratch_t;

ASSERT_ALIAS(tcell_t, c.alt, alt);
ASSERT_ALIAS(tcell_t, c.tmp, tmp);
ASSERT_ALIAS(tcell_t, c.op, op);
ASSERT_ALIAS(tcell_t, c.pos, pos);
ASSERT_ALIAS(tcell_t, c.n, n);
ASSERT_ALIAS(tcell_t, c.size, size);
ASSERT_ALIAS(tcell_t, c.expr, expr);

#endif
