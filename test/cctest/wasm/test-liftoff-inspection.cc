// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/baseline/liftoff-compiler.h"
#include "src/wasm/wasm-debug.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

class LiftoffCompileEnvironment {
 public:
  LiftoffCompileEnvironment()
      : isolate_(CcTest::InitIsolateOnce()),
        handle_scope_(isolate_),
        zone_(isolate_->allocator(), ZONE_NAME),
        module_builder_(&zone_, nullptr, ExecutionTier::kLiftoff,
                        kRuntimeExceptionSupport, kNoLowerSimd) {
    // Add a table of length 1, for indirect calls.
    module_builder_.AddIndirectFunctionTable(nullptr, 1);
  }

  struct TestFunction {
    OwnedVector<uint8_t> body_bytes;
    WasmFunction* function;
    FunctionBody body;
  };

  void CheckDeterministicCompilation(
      std::initializer_list<ValueType> return_types,
      std::initializer_list<ValueType> param_types,
      std::initializer_list<uint8_t> raw_function_bytes) {
    auto test_func = AddFunction(return_types, param_types, raw_function_bytes);

    // Now compile the function with Liftoff two times.
    CompilationEnv env = module_builder_.CreateCompilationEnv();
    WasmFeatures detected1;
    WasmFeatures detected2;
    WasmCompilationResult result1 = ExecuteLiftoffCompilation(
        isolate_->allocator(), &env, test_func.body,
        test_func.function->func_index, isolate_->counters(), &detected1);
    WasmCompilationResult result2 = ExecuteLiftoffCompilation(
        isolate_->allocator(), &env, test_func.body,
        test_func.function->func_index, isolate_->counters(), &detected2);

    CHECK(result1.succeeded());
    CHECK(result2.succeeded());

    // Check that the generated code matches.
    auto code1 =
        VectorOf(result1.code_desc.buffer, result1.code_desc.instr_size);
    auto code2 =
        VectorOf(result2.code_desc.buffer, result2.code_desc.instr_size);
    CHECK_EQ(code1, code2);
    CHECK_EQ(detected1, detected2);
  }

  DebugSideTable GenerateDebugSideTable(
      std::initializer_list<ValueType> return_types,
      std::initializer_list<ValueType> param_types,
      std::initializer_list<uint8_t> raw_function_bytes) {
    auto test_func = AddFunction(return_types, param_types, raw_function_bytes);

    CompilationEnv env = module_builder_.CreateCompilationEnv();
    return GenerateLiftoffDebugSideTable(CcTest::i_isolate()->allocator(), &env,
                                         test_func.body);
  }

 private:
  OwnedVector<uint8_t> GenerateFunctionBody(
      std::initializer_list<uint8_t> raw_function_bytes) {
    // Build the function bytes by prepending the locals decl and appending an
    // "end" opcode.
    OwnedVector<uint8_t> function_bytes =
        OwnedVector<uint8_t>::New(raw_function_bytes.size() + 2);
    function_bytes[0] = WASM_NO_LOCALS;
    std::copy(raw_function_bytes.begin(), raw_function_bytes.end(),
              &function_bytes[1]);
    function_bytes[raw_function_bytes.size() + 1] = WASM_END;
    return function_bytes;
  }

  FunctionSig* AddSig(std::initializer_list<ValueType> return_types,
                      std::initializer_list<ValueType> param_types) {
    ValueType* storage =
        zone_.NewArray<ValueType>(return_types.size() + param_types.size());
    std::copy(return_types.begin(), return_types.end(), storage);
    std::copy(param_types.begin(), param_types.end(),
              storage + return_types.size());
    FunctionSig* sig = new (&zone_)
        FunctionSig{return_types.size(), param_types.size(), storage};
    module_builder_.AddSignature(sig);
    return sig;
  }

  TestFunction AddFunction(std::initializer_list<ValueType> return_types,
                           std::initializer_list<ValueType> param_types,
                           std::initializer_list<uint8_t> raw_function_bytes) {
    OwnedVector<uint8_t> function_bytes =
        GenerateFunctionBody(raw_function_bytes);
    FunctionSig* sig = AddSig(return_types, param_types);
    int func_index =
        module_builder_.AddFunction(sig, "f", TestingModuleBuilder::kWasm);
    WasmFunction* function = module_builder_.GetFunctionAt(func_index);
    function->code = {module_builder_.AddBytes(function_bytes.as_vector()),
                      static_cast<uint32_t>(function_bytes.size())};
    FunctionBody body{function->sig, 0, function_bytes.begin(),
                      function_bytes.end()};
    return {std::move(function_bytes), function, body};
  }

  Isolate* isolate_;
  HandleScope handle_scope_;
  Zone zone_;
  TestingModuleBuilder module_builder_;
};
}  // namespace

TEST(Liftoff_deterministic_simple) {
  LiftoffCompileEnvironment env;
  env.CheckDeterministicCompilation(
      {kWasmI32}, {kWasmI32, kWasmI32},
      {WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1))});
}

TEST(Liftoff_deterministic_call) {
  LiftoffCompileEnvironment env;
  env.CheckDeterministicCompilation(
      {kWasmI32}, {kWasmI32},
      {WASM_I32_ADD(WASM_CALL_FUNCTION(0, WASM_GET_LOCAL(0)),
                    WASM_GET_LOCAL(0))});
}

TEST(Liftoff_deterministic_indirect_call) {
  LiftoffCompileEnvironment env;
  env.CheckDeterministicCompilation(
      {kWasmI32}, {kWasmI32},
      {WASM_I32_ADD(WASM_CALL_INDIRECT(0, WASM_GET_LOCAL(0), WASM_I32V_1(47)),
                    WASM_GET_LOCAL(0))});
}

TEST(Liftoff_deterministic_loop) {
  LiftoffCompileEnvironment env;
  env.CheckDeterministicCompilation(
      {kWasmI32}, {kWasmI32},
      {WASM_LOOP(WASM_BR_IF(0, WASM_GET_LOCAL(0))), WASM_GET_LOCAL(0)});
}

TEST(Liftoff_deterministic_trap) {
  LiftoffCompileEnvironment env;
  env.CheckDeterministicCompilation(
      {kWasmI32}, {kWasmI32, kWasmI32},
      {WASM_I32_DIVS(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1))});
}

TEST(Liftoff_debug_side_table_simple) {
  LiftoffCompileEnvironment env;
  auto debug_side_table = env.GenerateDebugSideTable(
      {kWasmI32}, {kWasmI32, kWasmI32},
      {WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1))});
  // 1 entry for the stack check.
  CHECK_EQ(1, debug_side_table.num_entries());
}

TEST(Liftoff_debug_side_table_call) {
  LiftoffCompileEnvironment env;
  auto debug_side_table = env.GenerateDebugSideTable(
      {kWasmI32}, {kWasmI32},
      {WASM_I32_ADD(WASM_CALL_FUNCTION(0, WASM_GET_LOCAL(0)),
                    WASM_GET_LOCAL(0))});
  // 2 entries:
  // - stack check
  // - call
  CHECK_EQ(2, debug_side_table.num_entries());
}

TEST(Liftoff_debug_side_table_indirect_call) {
  LiftoffCompileEnvironment env;
  auto debug_side_table = env.GenerateDebugSideTable(
      {kWasmI32}, {kWasmI32},
      {WASM_I32_ADD(WASM_CALL_INDIRECT(0, WASM_I32V_1(47), WASM_GET_LOCAL(0)),
                    WASM_GET_LOCAL(0))});
  // 4 entries:
  // - stack check
  // - trap for invalid function index
  // - trap for signature mismatch
  // - indirect call
  CHECK_EQ(4, debug_side_table.num_entries());
}

TEST(Liftoff_debug_side_table_loop) {
  LiftoffCompileEnvironment env;
  auto debug_side_table = env.GenerateDebugSideTable(
      {kWasmI32}, {kWasmI32},
      {WASM_LOOP(WASM_BR_IF(0, WASM_GET_LOCAL(0))), WASM_GET_LOCAL(0)});
  // 2 entries:
  // - stack check at function entry
  // - stack check in loop header
  CHECK_EQ(2, debug_side_table.num_entries());
}

TEST(Liftoff_debug_side_table_trap) {
  LiftoffCompileEnvironment env;
  auto debug_side_table = env.GenerateDebugSideTable(
      {kWasmI32}, {kWasmI32, kWasmI32},
      {WASM_I32_DIVS(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1))});
  // 3 entries:
  // - stack check
  // - trap for division by zero
  // - trap for division result unrepresentable
  CHECK_EQ(3, debug_side_table.num_entries());
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
