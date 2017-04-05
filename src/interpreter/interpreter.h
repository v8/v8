// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_INTERPRETER_H_
#define V8_INTERPRETER_INTERPRETER_H_

#include <memory>

// Clients of this interface shouldn't depend on lots of interpreter internals.
// Do not include anything from src/interpreter other than
// src/interpreter/bytecodes.h here!
#include "src/base/macros.h"
#include "src/builtins/builtins.h"
#include "src/interpreter/bytecodes.h"
#include "src/parsing/token.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

class Isolate;
class Callable;
class CompilationInfo;
class CompilationJob;

namespace interpreter {

class InterpreterAssembler;

class Interpreter {
 public:
  explicit Interpreter(Isolate* isolate);
  virtual ~Interpreter() {}

  // Initializes the interpreter dispatch table.
  void Initialize();

  // Returns the interrupt budget which should be used for the profiler counter.
  static int InterruptBudget();

  // Creates a compilation job which will generate bytecode for |info|.
  static CompilationJob* NewCompilationJob(CompilationInfo* info);

  // Return bytecode handler for |bytecode|.
  Code* GetBytecodeHandler(Bytecode bytecode, OperandScale operand_scale);

  // GC support.
  void IterateDispatchTable(ObjectVisitor* v);

  // Disassembler support (only useful with ENABLE_DISASSEMBLER defined).
  const char* LookupNameOfBytecodeHandler(Code* code);

  V8_EXPORT_PRIVATE Local<v8::Object> GetDispatchCountersObject();

  Address dispatch_table_address() {
    return reinterpret_cast<Address>(&dispatch_table_[0]);
  }

  Address bytecode_dispatch_counters_table() {
    return reinterpret_cast<Address>(bytecode_dispatch_counters_table_.get());
  }

  // TODO(ignition): Tune code size multiplier.
  static const int kCodeSizeMultiplier = 24;

 private:
  // In the case of bytecodes that share handler implementations, copy the code
  // into the bytecode's dispatcher table entry and return true.
  bool ReuseExistingHandler(Bytecode bytecode, OperandScale operand_scale);

  // Generates handler for given |bytecode| and |operand_scale|
  // and installs it into the dispatch table.
  void InstallBytecodeHandler(Isolate* isolate, Bytecode bytecode,
                              OperandScale operand_scale);

  uintptr_t GetDispatchCounter(Bytecode from, Bytecode to) const;

  // Get dispatch table index of bytecode.
  static size_t GetDispatchTableIndex(Bytecode bytecode,
                                      OperandScale operand_scale);

  bool IsDispatchTableInitialized();
  bool ShouldInitializeDispatchTable();

  static const int kNumberOfWideVariants = 3;
  static const int kDispatchTableSize = kNumberOfWideVariants * (kMaxUInt8 + 1);
  static const int kNumberOfBytecodes = static_cast<int>(Bytecode::kLast) + 1;

  Isolate* isolate_;
  Address dispatch_table_[kDispatchTableSize];
  std::unique_ptr<uintptr_t[]> bytecode_dispatch_counters_table_;

  DISALLOW_COPY_AND_ASSIGN(Interpreter);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_INTERPRETER_H_
