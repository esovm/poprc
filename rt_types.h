#ifndef __RT_TYPES__
#define __RT_TYPES__

typedef enum {
  T_INT = 0,
  T_PTR,
  T_FAIL
} type_t;

typedef struct cell cell_t;
typedef struct stack_frame stack_frame_t;

typedef struct stack_frame {
  stack_frame_t *up;
  cell_t *cell;
} stack_frame_t;

typedef bool (reduce_t)(cell_t *cell);
//#define FUNC(x) bool func_##x(cell_t *c)
struct __attribute__((packed)) cell {
  union {
    reduce_t *func;
    cell_t *prev;
  };
  cell_t *alt;
  uint32_t n;
  union {
    /* unevaluated */
    cell_t *arg[4];
    /* reduced */
    struct {
      intptr_t alt_set;
      intptr_t type;
      cell_t *next;
      union {
	intptr_t val;
	cell_t *ptr;
      };
    };
  };
};

typedef struct word_entry_t {
  char name[16];
  reduce_t *func;
  unsigned int in, out;
} word_entry_t;

typedef struct parse_tok_t {
  cell_t *c;
  unsigned int out;
} parse_tok_t;

#define MK_APPEND(fname, field)			\
  cell_t *fname(cell_t *a, cell_t *b);

typedef enum char_class_t {
  CC_NONE,
  CC_NUMERIC,
  CC_ALPHA,
  CC_SYMBOL
} char_class_t;

#endif
