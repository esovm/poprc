#ifndef __LLVM_H__
#define __LLVM_H__

#include "rt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

  void print_llvm_ir(cell_t *c);
  void llvm_jit_test();

#ifdef __cplusplus
}
#endif

#endif