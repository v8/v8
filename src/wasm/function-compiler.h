// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_FUNCTION_COMPILER_H_
#define V8_WASM_FUNCTION_COMPILER_H_

#include "src/wasm/function-body-decoder.h"

namespace v8 {
namespace internal {

namespace compiler {
class TurbofanWasmCompilationUnit;
}  // namespace compiler

namespace wasm {

class LiftoffCompilationUnit;
struct ModuleWireBytes;
class NativeModule;
class WasmCode;
class WasmEngine;
struct WasmFunction;

enum RuntimeExceptionSupport : bool {
  kRuntimeExceptionSupport = true,
  kNoRuntimeExceptionSupport = false
};

enum UseTrapHandler : bool { kUseTrapHandler = true, kNoTrapHandler = false };

enum LowerSimd : bool { kLowerSimd = true, kNoLowerSimd = false };

// The {ModuleEnv} encapsulates the module data that is used during compilation.
// ModuleEnvs are shareable across multiple compilations.
struct ModuleEnv {
  // A pointer to the decoded module's static representation.
  const WasmModule* const module;

  // True if trap handling should be used in compiled code, rather than
  // compiling in bounds checks for each memory access.
  const UseTrapHandler use_trap_handler;

  // If the runtime doesn't support exception propagation,
  // we won't generate stack checks, and trap handling will also
  // be generated differently.
  const RuntimeExceptionSupport runtime_exception_support;

  const LowerSimd lower_simd;

  constexpr ModuleEnv(const WasmModule* module, UseTrapHandler use_trap_handler,
                      RuntimeExceptionSupport runtime_exception_support,
                      LowerSimd lower_simd = kNoLowerSimd)
      : module(module),
        use_trap_handler(use_trap_handler),
        runtime_exception_support(runtime_exception_support),
        lower_simd(lower_simd) {}
};

class WasmCompilationUnit final {
 public:
  enum class CompilationMode : uint8_t { kLiftoff, kTurbofan };
  static CompilationMode GetDefaultCompilationMode();

  // If constructing from a background thread, pass in a Counters*, and ensure
  // that the Counters live at least as long as this compilation unit (which
  // typically means to hold a std::shared_ptr<Counters>).
  // If used exclusively from a foreground thread, Isolate::counters() may be
  // used by callers to pass Counters.
  WasmCompilationUnit(WasmEngine* wasm_engine, ModuleEnv*, wasm::NativeModule*,
                      wasm::FunctionBody, wasm::WasmName, int index, Counters*,
                      CompilationMode = GetDefaultCompilationMode());

  ~WasmCompilationUnit();

  void ExecuteCompilation();
  wasm::WasmCode* FinishCompilation(wasm::ErrorThrower* thrower);

  static wasm::WasmCode* CompileWasmFunction(
      wasm::NativeModule* native_module, wasm::ErrorThrower* thrower,
      Isolate* isolate, ModuleEnv* env, const wasm::WasmFunction* function,
      CompilationMode = GetDefaultCompilationMode());

  wasm::NativeModule* native_module() const { return native_module_; }
  CompilationMode mode() const { return mode_; }

 private:
  friend class LiftoffCompilationUnit;
  friend class compiler::TurbofanWasmCompilationUnit;

  ModuleEnv* env_;
  WasmEngine* wasm_engine_;
  wasm::FunctionBody func_body_;
  wasm::WasmName func_name_;
  Counters* counters_;
  int func_index_;
  wasm::NativeModule* native_module_;
  CompilationMode mode_;
  // LiftoffCompilationUnit, set if {mode_ == kLiftoff}.
  std::unique_ptr<LiftoffCompilationUnit> liftoff_unit_;
  // TurbofanWasmCompilationUnit, set if {mode_ == kTurbofan}.
  std::unique_ptr<compiler::TurbofanWasmCompilationUnit> turbofan_unit_;

  void SwitchMode(CompilationMode new_mode);

  DISALLOW_COPY_AND_ASSIGN(WasmCompilationUnit);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_FUNCTION_COMPILER_H_
