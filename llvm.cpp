#include <llvm/Pass.h>
#include <llvm/PassManager.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/MathExtras.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>
#include <algorithm>
#include <iostream>
#include "llvm.h"

extern "C" {
#include "gen/rt.h"
#include "gen/eval.h"
#include "stdio.h"
}

using namespace llvm;

#define ZEXT 1
#define NOUNWIND 2

void setup_CallInst(CallInst *i, unsigned int attrs) {
  i->setCallingConv(CallingConv::C);
  i->setTailCall(false);
  if(attrs & ZEXT) i->addAttribute(0, Attribute::ZExt);
  if(attrs & NOUNWIND) i->addAttribute(-1, Attribute::NoUnwind);
}

StructType *StructTy_cell;
ArrayType* ArrayTy_types;
FunctionType* FuncTy_func_op2;
ArrayType* ArrayTy_arg;
PointerType* PointerTy_cell;
Function* func_function_preamble;
Function* func_val;
Function* func_function_epilogue;
Function* func_fail;
PointerType* PointerTy_i64;

void define_types_and_stuff(Module *mod) {
  // Type Definitions
  std::vector<Type*>FuncTy_func_op2_args;
  StructTy_cell = mod->getTypeByName("cell");
  if (!StructTy_cell) {
    StructTy_cell = StructType::create(mod->getContext(), "cell");
  }
  std::vector<Type*>StructTy_cell_fields;
  StructType *StructTy_cell_header = mod->getTypeByName("cell_header");
  if (!StructTy_cell_header) {
    StructTy_cell_header = StructType::create(mod->getContext(), "cell_header");
  }
  std::vector<Type*>StructTy_cell_header_fields;
  std::vector<Type*>StructTy_empty_fields;
  StructType *StructTy_empty = StructType::get(mod->getContext(), StructTy_empty_fields, /*isPacked=*/false);

  PointerType* PointerTy_empty = PointerType::get(StructTy_empty, 0);

  StructTy_cell_header_fields.push_back(PointerTy_empty);
  PointerTy_cell = PointerType::get(StructTy_cell, 0);

  StructTy_cell_header_fields.push_back(PointerTy_cell);
  StructTy_cell_header_fields.push_back(PointerTy_cell);
  if (StructTy_cell_header->isOpaque()) {
    StructTy_cell_header->setBody(StructTy_cell_header_fields, /*isPacked=*/true);
  }

  StructTy_cell_fields.push_back(StructTy_cell_header);
  StructTy_cell_fields.push_back(IntegerType::get(mod->getContext(), 64));
  StructType *StructTy_cell_args = mod->getTypeByName("cell_args");
  if (!StructTy_cell_args) {
    StructTy_cell_args = StructType::create(mod->getContext(), "cell_args");
  }
  std::vector<Type*>StructTy_cell_args_fields;
  ArrayType* ArrayTy_cell_ptr = ArrayType::get(PointerTy_cell, 3);

  StructTy_cell_args_fields.push_back(ArrayTy_cell_ptr);
  if (StructTy_cell_args->isOpaque()) {
    StructTy_cell_args->setBody(StructTy_cell_args_fields, /*isPacked=*/false);
  }

  StructTy_cell_fields.push_back(StructTy_cell_args);
  if (StructTy_cell->isOpaque()) {
    StructTy_cell->setBody(StructTy_cell_fields, /*isPacked=*/true);
  }


  PointerType* PointerTy_cell_ptr = PointerType::get(PointerTy_cell, 0);

  FuncTy_func_op2_args.push_back(PointerTy_cell_ptr);
  FuncTy_func_op2 = FunctionType::get(
					     /*Result=*/IntegerType::get(mod->getContext(), 1),
					     /*Params=*/FuncTy_func_op2_args,
					     /*isVarArg=*/false);

  PointerTy_i64 = PointerType::get(IntegerType::get(mod->getContext(), 64), 0);

  ArrayTy_arg = ArrayType::get(PointerTy_cell, 2);

  PointerType* PointerTy_i32 = PointerType::get(IntegerType::get(mod->getContext(), 32), 0);

  ArrayTy_types = ArrayType::get(IntegerType::get(mod->getContext(), 32), 2);

  std::vector<Type*>FuncTy_function_preamble_args;
  FuncTy_function_preamble_args.push_back(PointerTy_cell);
  FuncTy_function_preamble_args.push_back(PointerTy_i64);
  FuncTy_function_preamble_args.push_back(PointerTy_cell_ptr);
  FuncTy_function_preamble_args.push_back(PointerTy_i32);
  FuncTy_function_preamble_args.push_back(PointerTy_cell_ptr);
  FuncTy_function_preamble_args.push_back(IntegerType::get(mod->getContext(), 32));
  FunctionType* FuncTy_function_preamble = FunctionType::get(
					      /*Result=*/IntegerType::get(mod->getContext(), 1),
					      /*Params=*/FuncTy_function_preamble_args,
					      /*isVarArg=*/false);

  std::vector<Type*>FuncTy_val_args;
  FuncTy_val_args.push_back(IntegerType::get(mod->getContext(), 64));
  FunctionType* FuncTy_val = FunctionType::get(
					      /*Result=*/PointerTy_cell,
					      /*Params=*/FuncTy_val_args,
					      /*isVarArg=*/false);

  std::vector<Type*>FuncTy_function_epilogue_args;
  FuncTy_function_epilogue_args.push_back(PointerTy_cell);
  FuncTy_function_epilogue_args.push_back(IntegerType::get(mod->getContext(), 64));
  FuncTy_function_epilogue_args.push_back(PointerTy_cell);
  FuncTy_function_epilogue_args.push_back(IntegerType::get(mod->getContext(), 32));
  FunctionType* FuncTy_function_epilogue = FunctionType::get(
					      /*Result=*/Type::getVoidTy(mod->getContext()),
					      /*Params=*/FuncTy_function_epilogue_args,
					      /*isVarArg=*/false);

  std::vector<Type*>FuncTy_fail_args;
  FuncTy_fail_args.push_back(PointerTy_cell_ptr);
  FunctionType* FuncTy_fail = FunctionType::get(
					      /*Result=*/Type::getVoidTy(mod->getContext()),
					      /*Params=*/FuncTy_fail_args,
					      /*isVarArg=*/false);


  // Function Declarations

  func_function_preamble = mod->getFunction("function_preamble");
  if (!func_function_preamble) {
    func_function_preamble = Function::Create(
					      /*Type=*/FuncTy_function_preamble,
					      /*Linkage=*/GlobalValue::ExternalLinkage,
					      /*Name=*/"function_preamble", mod); // (external, no body)
    func_function_preamble->setCallingConv(CallingConv::C);
  }
  func_function_preamble->addAttribute(0, Attribute::ZExt);

  func_val = mod->getFunction("val");
  if (!func_val) {
    func_val = Function::Create(
				/*Type=*/FuncTy_val,
				/*Linkage=*/GlobalValue::ExternalLinkage,
				/*Name=*/"val", mod); // (external, no body)
    func_val->setCallingConv(CallingConv::C);
  }

  func_function_epilogue = mod->getFunction("function_epilogue");
  if (!func_function_epilogue) {
    func_function_epilogue = Function::Create(
					      /*Type=*/FuncTy_function_epilogue,
					      /*Linkage=*/GlobalValue::ExternalLinkage,
					      /*Name=*/"function_epilogue", mod); // (external, no body)
    func_function_epilogue->setCallingConv(CallingConv::C);
  }

  func_fail = mod->getFunction("fail");
  if (!func_fail) {
    func_fail = Function::Create(
				 /*Type=*/FuncTy_fail,
				 /*Linkage=*/GlobalValue::ExternalLinkage,
				 /*Name=*/"fail", mod); // (external, no body)
    func_fail->setCallingConv(CallingConv::C);
  }
}

void load_constants(LLVMContext &ctx, unsigned int bits, ConstantInt **arr, unsigned int n) {
  int i;
  for(i = 0; i < n; i++) {
    arr[i] = ConstantInt::get(ctx, APInt(bits, i));
  }
}

LoadInst *get_val(Instruction *ptr, BasicBlock *b) {
  LLVMContext &ctx = b->getContext();
  LoadInst* p = new LoadInst(ptr, "", false, b);
  p->setAlignment(16);
  std::vector<Value*> indices;
  indices.push_back(ConstantInt::get(ctx, APInt(64, 0)));
  indices.push_back(ConstantInt::get(ctx, APInt(32, 2)));
  indices.push_back(ConstantInt::get(ctx, APInt(32, 0)));
  indices.push_back(ConstantInt::get(ctx, APInt(64, 2)));
  Instruction* v = GetElementPtrInst::Create(p, indices, "", b);
  CastInst* q = new BitCastInst(v, PointerTy_i64, "", b);
  LoadInst* r = new LoadInst(q, "cell_val", false, b);
  r->setAlignment(1);
  return r;
}

Instruction *index(AllocaInst *ptr, int x, BasicBlock *b) {
  LLVMContext &ctx = b->getContext();
  std::vector<Value*> indices;
  indices.push_back(ConstantInt::get(ctx, APInt(64, 0)));
  indices.push_back(ConstantInt::get(ctx, APInt(64, x)));
  return GetElementPtrInst::Create(ptr, indices, "index", b);
}

Function* define_func_op2(Module *mod) {

  LLVMContext &ctx = mod->getContext();

  // Global Variable Declarations

  GlobalVariable* gvar_array_func_op2_types = new GlobalVariable(/*Module=*/*mod,
								 /*Type=*/ArrayTy_types,
								 /*isConstant=*/true,
								 /*Linkage=*/GlobalValue::InternalLinkage,
								 /*Initializer=*/0, // has initializer, specified below
								 /*Name=*/"func_op2.types");
  gvar_array_func_op2_types->setAlignment(4);

  // Constant Definitions

#define const_int(bits, n) \
ConstantInt* const_int##bits[n]; \
load_constants(ctx, bits, const_int##bits, n);

  const_int(64, 3);
  const_int(32, 4);
  const_int(1, 2);

  ConstantInt* const_int64_not_3 = ConstantInt::get(ctx, APInt(64, ~3));
  ConstantInt* const_int32_n = ConstantInt::get(ctx, APInt(32, 2));

  ConstantPointerNull* const_ptr_cell_null = ConstantPointerNull::get(PointerTy_cell);

  std::vector<Constant*> const_ptr_types_indices;
  const_ptr_types_indices.push_back(const_int64[0]);
  const_ptr_types_indices.push_back(const_int64[0]);
  Constant* const_ptr_types = ConstantExpr::getGetElementPtr(gvar_array_func_op2_types, const_ptr_types_indices);
  std::vector<Constant*> const_array_types_elems;
  const_array_types_elems.push_back(const_int32[T_INT]);
  const_array_types_elems.push_back(const_int32[T_INT]);
  Constant* const_array_types = ConstantArray::get(ArrayTy_types, const_array_types_elems);

  // Global Variable Definitions
  gvar_array_func_op2_types->setInitializer(const_array_types);

  Function* func_func_op2 = mod->getFunction("func_op2");
  if (!func_func_op2) {
    func_func_op2 = Function::Create(
				     /*Type=*/FuncTy_func_op2,
				     /*Linkage=*/GlobalValue::ExternalLinkage,
				     /*Name=*/"func_op2", mod);
    func_func_op2->setCallingConv(CallingConv::C);
  }
  func_func_op2->addAttribute(0, Attribute::ZExt);
  func_func_op2->addAttribute(-1, Attribute::NoUnwind);

  Function::arg_iterator args = func_func_op2->arg_begin();
  Value* ptr_cp = args++;
  ptr_cp->setName("cp");

  BasicBlock* label_entry = BasicBlock::Create(ctx, "entry",func_func_op2,0);
  BasicBlock* label_if_test = BasicBlock::Create(ctx, "if_test",func_func_op2,0);
  BasicBlock* label_comp = BasicBlock::Create(ctx, "comp",func_func_op2,0);
  BasicBlock* label_epilogue = BasicBlock::Create(ctx, "epilogue",func_func_op2,0);
  BasicBlock* label_fail = BasicBlock::Create(ctx, "fail",func_func_op2,0);
  BasicBlock* label_finish = BasicBlock::Create(ctx, "finish",func_func_op2,0);

  // Block  (label_entry)
  AllocaInst* ptr_res = new AllocaInst(PointerTy_cell, "res", label_entry);
  ptr_res->setAlignment(8);
  AllocaInst* ptr_alt_set = new AllocaInst(IntegerType::get(ctx, 64), "alt_set", label_entry);
  ptr_alt_set->setAlignment(8);
  AllocaInst* ptr_arg = new AllocaInst(ArrayTy_arg, "arg", label_entry);
  ptr_arg->setAlignment(16);
  (new StoreInst(const_ptr_cell_null, ptr_res, false, label_entry))->setAlignment(8);
  LoadInst* ptr_cp_deref = new LoadInst(ptr_cp, "", false, label_entry);
  ptr_cp_deref->setAlignment(8);
  CastInst* int64_c = new PtrToIntInst(ptr_cp_deref, IntegerType::get(ctx, 64), "", label_entry);
  BinaryOperator* int64_c_and_not_3 = BinaryOperator::Create(Instruction::And, int64_c, const_int64_not_3, "", label_entry);
  CastInst* ptr_c = new IntToPtrInst(int64_c_and_not_3, PointerTy_cell, "c", label_entry);
  (new StoreInst(const_int64[0], ptr_alt_set, false, label_entry))->setAlignment(8);
  Instruction* ptr_arg0 = index(ptr_arg, 0, label_entry);
  std::vector<Value*> int1_function_preamble_call_params;
  int1_function_preamble_call_params.push_back(ptr_c);
  int1_function_preamble_call_params.push_back(ptr_alt_set);
  int1_function_preamble_call_params.push_back(ptr_arg0);
  int1_function_preamble_call_params.push_back(const_ptr_types);
  int1_function_preamble_call_params.push_back(ptr_res);
  int1_function_preamble_call_params.push_back(const_int32_n);
  CallInst* int1_function_preamble_call = CallInst::Create(func_function_preamble, int1_function_preamble_call_params, "", label_entry);
  setup_CallInst(int1_function_preamble_call, ZEXT | NOUNWIND);

  BranchInst::Create(label_if_test, label_fail, int1_function_preamble_call, label_entry);

  // Block if_test (label_if_test)
  LoadInst* ptr_41 = new LoadInst(ptr_res, "", false, label_if_test);
  ptr_41->setAlignment(8);
  ICmpInst* int1_42 = new ICmpInst(*label_if_test, ICmpInst::ICMP_EQ, ptr_41, const_ptr_cell_null, "");
  BranchInst::Create(label_comp, label_epilogue, int1_42, label_if_test);

  // Block comp (label_comp)
  LoadInst* int64_arg0_val = get_val(ptr_arg0, label_comp);
  Instruction* ptr_arg1 = index(ptr_arg, 1, label_comp);
  LoadInst* int64_arg1_val = get_val(ptr_arg1, label_comp);
  BinaryOperator* int64_sum = BinaryOperator::Create(Instruction::Add, int64_arg0_val, int64_arg1_val, "sum", label_comp);
  CallInst* ptr_func_val_call = CallInst::Create(func_val, int64_sum, "", label_comp);
  setup_CallInst(ptr_func_val_call, NOUNWIND);
  (new StoreInst(ptr_func_val_call, ptr_res, false, label_comp))->setAlignment(8);
  BranchInst::Create(label_epilogue, label_comp);

  // Block epilogue (label_epilogue)
  LoadInst* int64_altset = new LoadInst(ptr_alt_set, "", false, label_epilogue);
  int64_altset->setAlignment(8);
  LoadInst* ptr_res_deref = new LoadInst(ptr_res, "", false, label_epilogue);
  ptr_res_deref->setAlignment(8);
  std::vector<Value*> void_function_epilogue_call_params;
  void_function_epilogue_call_params.push_back(ptr_c);
  void_function_epilogue_call_params.push_back(int64_altset);
  void_function_epilogue_call_params.push_back(ptr_res_deref);
  void_function_epilogue_call_params.push_back(const_int32_n);
  CallInst* void_function_epilogue_call = CallInst::Create(func_function_epilogue, void_function_epilogue_call_params, "", label_epilogue);
  setup_CallInst(void_function_epilogue_call, NOUNWIND);

  BranchInst::Create(label_finish, label_epilogue);

  // Block fail (label_fail)
  CallInst* void_fail_call = CallInst::Create(func_fail, ptr_cp, "", label_fail);
  setup_CallInst(void_fail_call, NOUNWIND);

  BranchInst::Create(label_finish, label_fail);

  // Block finish (label_finish)
  PHINode* int1__0 = PHINode::Create(IntegerType::get(ctx, 1), 2, ".0", label_finish);
  int1__0->addIncoming(const_int1[1], label_epilogue);
  int1__0->addIncoming(const_int1[0], label_fail);

  ReturnInst::Create(ctx, int1__0, label_finish);
  return func_func_op2;
}

void printModule(Module *mod) {
  verifyModule(*mod, PrintMessageAction);
  PassManager PM;
  PM.add(createPrintModulePass(&outs()));
  PM.run(*mod);
}

Module *makeModule(cell_t *p) {
  /* function :: (cell_t *)[] --> cell_t * */
  LLVMContext &ctx = getGlobalContext();
  Module *mod = new Module("test", ctx);
  define_types_and_stuff(mod);
  define_func_op2(mod);
  return mod;
}

void print_llvm_ir(cell_t *c) {
  Module *m = makeModule(c);
  printModule(m);
  free(m);
}

static ExecutionEngine *engine = 0;

void llvm_jit_test() {
  InitializeNativeTarget();
  LLVMContext &ctx = getGlobalContext();
  Module *mod = new Module("module", ctx);
  define_types_and_stuff(mod);
  Function *lf = define_func_op2(mod);
  std::string err = "";
  engine = EngineBuilder(mod)
    .setErrorStr(&err)
    .setEngineKind(EngineKind::JIT)
    .create();

  engine->addGlobalMapping(func_function_preamble, (void *)&function_preamble);
  engine->addGlobalMapping(func_val, (void *)&val);
  engine->addGlobalMapping(func_function_epilogue, (void *)&function_epilogue);
  engine->addGlobalMapping(func_fail, (void *)&fail);

  lf->dump();
  if(!engine) {
    std::cout << err << std::endl;
    return;
  }
  reduce_t *f = (reduce_t *)engine->getPointerToFunction(lf);
  cell_t *c = closure_alloc(2);
  c->func = f;
  c->arg[0] = val(1);
  c->arg[1] = val(2);
  reduce(&c);
  show_one(c);
  std::cout << std::endl;
}