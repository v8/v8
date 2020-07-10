// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "src/utils/utils.h"
#include "src/utils/vector.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/wasm-arguments.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_gc {

using F = std::pair<ValueType, bool>;

class WasmGCTester {
 public:
  WasmGCTester()
      : flag_gc(&v8::internal::FLAG_experimental_wasm_gc, true),
        flag_reftypes(&v8::internal::FLAG_experimental_wasm_reftypes, true),
        flag_typedfuns(&v8::internal::FLAG_experimental_wasm_typed_funcref,
                       true),
        zone(&allocator, ZONE_NAME),
        builder_(&zone),
        isolate_(CcTest::InitIsolateOnce()),
        scope(isolate_),
        thrower(isolate_, "Test wasm GC") {
    testing::SetupIsolateForWasmModule(isolate_);
  }

  uint32_t AddGlobal(ValueType type, bool mutability, WasmInitExpr init) {
    return builder_.AddGlobal(type, mutability, std::move(init));
  }

  uint32_t DefineFunction(FunctionSig* sig,
                          std::initializer_list<ValueType> locals,
                          std::initializer_list<byte> code) {
    WasmFunctionBuilder* fun = builder_.AddFunction(sig);
    for (ValueType local : locals) {
      fun->AddLocal(local);
    }
    fun->EmitCode(code.begin(), static_cast<uint32_t>(code.size()));
    return fun->func_index();
  }

  uint32_t DefineStruct(std::initializer_list<F> fields) {
    StructType::Builder type_builder(&zone,
                                     static_cast<uint32_t>(fields.size()));
    for (F field : fields) {
      type_builder.AddField(field.first, field.second);
    }
    return builder_.AddStructType(type_builder.Build());
  }

  uint32_t DefineArray(ValueType element_type, bool mutability) {
    return builder_.AddArrayType(zone.New<ArrayType>(element_type, mutability));
  }

  void CompileModule() {
    ZoneBuffer buffer(&zone);
    builder_.WriteTo(&buffer);
    MaybeHandle<WasmInstanceObject> maybe_instance =
        testing::CompileAndInstantiateForTesting(
            isolate_, &thrower, ModuleWireBytes(buffer.begin(), buffer.end()));
    if (thrower.error()) FATAL("%s", thrower.error_msg());
    instance_ = maybe_instance.ToHandleChecked();
  }

  void CallFunctionImpl(uint32_t function_index, const FunctionSig* sig,
                        CWasmArgumentsPacker* packer) {
    WasmCodeRefScope scope;
    NativeModule* module = instance_->module_object().native_module();
    WasmCode* code = module->GetCode(function_index);
    Address wasm_call_target = code->instruction_start();
    Handle<Object> object_ref = instance_;
    Handle<Code> c_wasm_entry = compiler::CompileCWasmEntry(isolate_, sig);
    Execution::CallWasm(isolate_, c_wasm_entry, wasm_call_target, object_ref,
                        packer->argv());
  }

  void CheckResult(uint32_t function_index, int32_t expected) {
    FunctionSig* sig = sigs.i_v();
    DCHECK(*sig == *instance_->module()->functions[function_index].sig);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    CallFunctionImpl(function_index, sig, &packer);
    packer.Reset();
    CHECK_EQ(expected, packer.Pop<int32_t>());
  }

  void CheckResult(uint32_t function_index, int32_t expected, int32_t arg) {
    FunctionSig* sig = sigs.i_i();
    DCHECK(*sig == *instance_->module()->functions[function_index].sig);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    packer.Push(arg);
    CallFunctionImpl(function_index, sig, &packer);
    packer.Reset();
    CHECK_EQ(expected, packer.Pop<int32_t>());
  }

  MaybeHandle<Object> GetResultObject(uint32_t function_index) {
    const FunctionSig* sig = instance_->module()->functions[function_index].sig;
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    CallFunctionImpl(function_index, sig, &packer);
    packer.Reset();
    return Handle<Object>(Object(packer.Pop<Address>()), isolate_);
  }

  void CheckHasThrown(uint32_t function_index, int32_t arg) {
    FunctionSig* sig = sigs.i_i();
    DCHECK(*sig == *instance_->module()->functions[function_index].sig);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    packer.Push(arg);
    CallFunctionImpl(function_index, sig, &packer);
    CHECK(isolate_->has_pending_exception());
    isolate_->clear_pending_exception();
  }

  Handle<WasmInstanceObject> instance() { return instance_; }
  Isolate* isolate() { return isolate_; }
  WasmModuleBuilder* builder() { return &builder_; }

  TestSignatures sigs;

 private:
  const FlagScope<bool> flag_gc;
  const FlagScope<bool> flag_reftypes;
  const FlagScope<bool> flag_typedfuns;

  v8::internal::AccountingAllocator allocator;
  Zone zone;
  WasmModuleBuilder builder_;

  Isolate* const isolate_;
  const HandleScope scope;
  Handle<WasmInstanceObject> instance_;
  ErrorThrower thrower;
};

ValueType ref(uint32_t type_index) {
  return ValueType::Ref(type_index, kNonNullable);
}
ValueType optref(uint32_t type_index) {
  return ValueType::Ref(type_index, kNullable);
}

// TODO(7748): Use WASM_EXEC_TEST once interpreter and liftoff are supported.
TEST(WasmBasicStruct) {
  WasmGCTester tester;
  uint32_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kOptRefType = optref(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  // Test struct.new and struct.get.
  const uint32_t kGet1 = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_STRUCT_GET(
           type_index, 0,
           WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
       kExprEnd});

  // Test struct.new and struct.get.
  const uint32_t kGet2 = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_STRUCT_GET(
           type_index, 1,
           WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
       kExprEnd});

  // Test struct.new, returning struct reference.
  const uint32_t kGetStruct = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64)), kExprEnd});

  // Test struct.set, struct refs types in locals.
  uint32_t j_local_index = 0;
  uint32_t j_field_index = 0;
  const uint32_t kSet = tester.DefineFunction(
      tester.sigs.i_v(), {kOptRefType},
      {WASM_SET_LOCAL(j_local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(42),
                                                     WASM_I32V(64))),
       WASM_STRUCT_SET(type_index, j_field_index, WASM_GET_LOCAL(j_local_index),
                       WASM_I32V(-99)),
       WASM_STRUCT_GET(type_index, j_field_index,
                       WASM_GET_LOCAL(j_local_index)),
       kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kGet1, 42);
  tester.CheckResult(kGet2, 64);
  CHECK(tester.GetResultObject(kGetStruct).ToHandleChecked()->IsWasmStruct());
  tester.CheckResult(kSet, -99);
}

// Test struct.set, ref.as_non_null,
// struct refs types in globals and if-results.
TEST(WasmRefAsNonNull) {
  WasmGCTester tester;
  uint32_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kOptRefType = optref(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  uint32_t global_index =
      tester.AddGlobal(kOptRefType, true,
                       WasmInitExpr::RefNullConst(
                           static_cast<HeapType::Representation>(type_index)));
  uint32_t field_index = 0;
  const uint32_t kFunc = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_SET_GLOBAL(global_index, WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                     WASM_I32V(66))),
       WASM_STRUCT_GET(
           type_index, field_index,
           WASM_REF_AS_NON_NULL(WASM_IF_ELSE_R(
               kOptRefType, WASM_I32V(1), WASM_GET_GLOBAL(global_index),
               WASM_REF_NULL(static_cast<byte>(type_index))))),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kFunc, 55);
}

TEST(WasmBrOnNull) {
  WasmGCTester tester;
  uint32_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kOptRefType = optref(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);
  uint32_t l_local_index = 0;
  const uint32_t kTaken = tester.DefineFunction(
      tester.sigs.i_v(), {kOptRefType},
      {WASM_BLOCK_I(WASM_I32V(42),
                    // Branch will be taken.
                    // 42 left on stack outside the block (not 52).
                    WASM_BR_ON_NULL(0, WASM_GET_LOCAL(l_local_index)),
                    WASM_I32V(52), WASM_BR(0)),
       kExprEnd});

  uint32_t m_field_index = 0;
  const uint32_t kNotTaken = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_I(
           WASM_I32V(42),
           WASM_STRUCT_GET(
               type_index, m_field_index,
               // Branch will not be taken.
               // 52 left on stack outside the block (not 42).
               WASM_BR_ON_NULL(0, WASM_STRUCT_NEW(type_index, WASM_I32V(52),
                                                  WASM_I32V(62)))),
           WASM_BR(0)),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kTaken, 42);
  tester.CheckResult(kNotTaken, 52);
}

TEST(WasmRefEq) {
  WasmGCTester tester;
  byte type_index = static_cast<byte>(
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)}));
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kOptRefType = optref(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  byte local_index = 0;
  const uint32_t kFunc = tester.DefineFunction(
      tester.sigs.i_v(), {kOptRefType},
      {WASM_SET_LOCAL(local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                   WASM_I32V(66))),
       WASM_I32_ADD(
           WASM_I32_SHL(
               WASM_REF_EQ(  // true
                   WASM_GET_LOCAL(local_index), WASM_GET_LOCAL(local_index)),
               WASM_I32V(0)),
           WASM_I32_ADD(
               WASM_I32_SHL(WASM_REF_EQ(  // false
                                WASM_GET_LOCAL(local_index),
                                WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                WASM_I32V(66))),
                            WASM_I32V(1)),
               WASM_I32_ADD(WASM_I32_SHL(  // false
                                WASM_REF_EQ(WASM_GET_LOCAL(local_index),
                                            WASM_REF_NULL(type_index)),
                                WASM_I32V(2)),
                            WASM_I32_SHL(WASM_REF_EQ(  // true
                                             WASM_REF_NULL(type_index),
                                             WASM_REF_NULL(type_index)),
                                         WASM_I32V(3))))),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kFunc, 0b1001);
}

TEST(WasmPackedStructU) {
  WasmGCTester tester;

  uint32_t type_index = tester.DefineStruct(
      {F(kWasmI8, true), F(kWasmI16, true), F(kWasmI32, true)});
  ValueType struct_type = optref(type_index);

  uint32_t local_index = 0;

  int32_t expected_output_0 = 0x1234;
  int32_t expected_output_1 = -1;

  const uint32_t kF0 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(local_index,
                      WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                                      WASM_I32V(expected_output_1),
                                      WASM_I32V(0x12345678))),
       WASM_STRUCT_GET_U(type_index, 0, WASM_GET_LOCAL(local_index)),
       kExprEnd});

  const uint32_t kF1 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(local_index,
                      WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                                      WASM_I32V(expected_output_1),
                                      WASM_I32V(0x12345678))),
       WASM_STRUCT_GET_U(type_index, 1, WASM_GET_LOCAL(local_index)),
       kExprEnd});
  tester.CompileModule();

  tester.CheckResult(kF0, static_cast<uint8_t>(expected_output_0));
  tester.CheckResult(kF1, static_cast<uint16_t>(expected_output_1));
}

TEST(WasmPackedStructS) {
  WasmGCTester tester;

  uint32_t type_index = tester.DefineStruct(
      {F(kWasmI8, true), F(kWasmI16, true), F(kWasmI32, true)});
  ValueType struct_type = optref(type_index);

  uint32_t local_index = 0;

  int32_t expected_output_0 = 0x80;
  int32_t expected_output_1 = 42;

  const uint32_t kF0 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(
           local_index,
           WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                           WASM_I32V(expected_output_1), WASM_I32V(0))),
       WASM_STRUCT_GET_S(type_index, 0, WASM_GET_LOCAL(local_index)),
       kExprEnd});

  const uint32_t kF1 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(0x80),
                                                   WASM_I32V(expected_output_1),
                                                   WASM_I32V(0))),
       WASM_STRUCT_GET_S(type_index, 1, WASM_GET_LOCAL(local_index)),
       kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kF0, static_cast<int8_t>(expected_output_0));
  tester.CheckResult(kF1, static_cast<int16_t>(expected_output_1));
}

TEST(WasmLetInstruction) {
  WasmGCTester tester;
  uint32_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});

  uint32_t let_local_index = 0;
  uint32_t let_field_index = 0;
  const uint32_t kLetTest1 = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_LET_1_I(WASM_SEQ(kLocalRef, static_cast<byte>(type_index)),
                    WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(52)),
                    WASM_STRUCT_GET(type_index, let_field_index,
                                    WASM_GET_LOCAL(let_local_index))),
       kExprEnd});

  uint32_t let_2_field_index = 0;
  const uint32_t kLetTest2 = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_LET_2_I(kLocalI32, WASM_I32_ADD(WASM_I32V(42), WASM_I32V(-32)),
                    WASM_SEQ(kLocalRef, static_cast<byte>(type_index)),
                    WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(52)),
                    WASM_I32_MUL(WASM_STRUCT_GET(type_index, let_2_field_index,
                                                 WASM_GET_LOCAL(1)),
                                 WASM_GET_LOCAL(0))),
       kExprEnd});

  const uint32_t kLetTestLocals = tester.DefineFunction(
      tester.sigs.i_i(), {kWasmI32},
      {WASM_SET_LOCAL(1, WASM_I32V(100)),
       WASM_LET_2_I(
           kLocalI32, WASM_I32V(1), kLocalI32, WASM_I32V(10),
           WASM_I32_SUB(WASM_I32_ADD(WASM_GET_LOCAL(0),     // 1st let-local
                                     WASM_GET_LOCAL(2)),    // Parameter
                        WASM_I32_ADD(WASM_GET_LOCAL(1),     // 2nd let-local
                                     WASM_GET_LOCAL(3)))),  // Function local
       kExprEnd});
  // Result: (1 + 1000) - (10 + 100) = 891

  uint32_t let_erase_local_index = 0;
  const uint32_t kLetTestErase = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmI32},
      {WASM_SET_LOCAL(let_erase_local_index, WASM_I32V(0)),
       WASM_LET_1_V(kLocalI32, WASM_I32V(1), WASM_NOP),
       WASM_GET_LOCAL(let_erase_local_index), kExprEnd});
  // The result should be 0 and not 1, as local_get(0) refers to the original
  // local.

  tester.CompileModule();

  tester.CheckResult(kLetTest1, 42);
  tester.CheckResult(kLetTest2, 420);
  tester.CheckResult(kLetTestLocals, 891, 1000);
  tester.CheckResult(kLetTestErase, 0);
}

TEST(WasmBasicArray) {
  WasmGCTester tester;
  uint32_t type_index = tester.DefineArray(wasm::kWasmI32, true);
  ValueType kRefTypes[] = {ref(type_index)};
  FunctionSig sig_q_v(1, 0, kRefTypes);
  ValueType kOptRefType = optref(type_index);

  // f: a = [12, 12, 12]; a[1] = 42; return a[arg0]
  uint32_t local_index = 1;
  const uint32_t kGetElem = tester.DefineFunction(
      tester.sigs.i_i(), {kOptRefType},
      {WASM_SET_LOCAL(local_index,
                      WASM_ARRAY_NEW(type_index, WASM_I32V(12), WASM_I32V(3))),
       WASM_ARRAY_SET(type_index, WASM_GET_LOCAL(local_index), WASM_I32V(1),
                      WASM_I32V(42)),
       WASM_ARRAY_GET(type_index, WASM_GET_LOCAL(local_index),
                      WASM_GET_LOCAL(0)),
       kExprEnd});

  // Reads and returns an array's length.
  const uint32_t kGetLength = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_ARRAY_LEN(type_index,
                      WASM_ARRAY_NEW(type_index, WASM_I32V(0), WASM_I32V(42))),
       kExprEnd});

  // Create an array of length 2, initialized to [42, 42].
  const uint32_t kAllocate = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_ARRAY_NEW(type_index, WASM_I32V(42), WASM_I32V(2)), kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kGetElem, 12, 0);
  tester.CheckResult(kGetElem, 42, 1);
  tester.CheckResult(kGetElem, 12, 2);
  tester.CheckHasThrown(kGetElem, 3);
  tester.CheckHasThrown(kGetElem, -1);
  tester.CheckResult(kGetLength, 42);

  MaybeHandle<Object> h_result = tester.GetResultObject(kAllocate);
  CHECK(h_result.ToHandleChecked()->IsWasmArray());
#if OBJECT_PRINT
  h_result.ToHandleChecked()->Print();
#endif
}

TEST(WasmPackedArrayU) {
  WasmGCTester tester;
  uint32_t array_index = tester.DefineArray(kWasmI8, true);
  ValueType array_type = optref(array_index);

  uint32_t param_index = 0;
  uint32_t local_index = 1;

  int32_t expected_output_3 = 258;

  const uint32_t kF = tester.DefineFunction(
      tester.sigs.i_i(), {array_type},
      {WASM_SET_LOCAL(local_index,
                      WASM_ARRAY_NEW(array_index, WASM_I32V(0), WASM_I32V(4))),
       WASM_ARRAY_SET(array_index, WASM_GET_LOCAL(local_index), WASM_I32V(0),
                      WASM_I32V(1)),
       WASM_ARRAY_SET(array_index, WASM_GET_LOCAL(local_index), WASM_I32V(1),
                      WASM_I32V(10)),
       WASM_ARRAY_SET(array_index, WASM_GET_LOCAL(local_index), WASM_I32V(2),
                      WASM_I32V(200)),
       WASM_ARRAY_SET(array_index, WASM_GET_LOCAL(local_index), WASM_I32V(3),
                      WASM_I32V(expected_output_3)),
       WASM_ARRAY_GET_U(array_index, WASM_GET_LOCAL(local_index),
                        WASM_GET_LOCAL(param_index)),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kF, 1, 0);
  tester.CheckResult(kF, 10, 1);
  tester.CheckResult(kF, 200, 2);
  // Only the 2 lsb's of 258 should be stored in the array.
  tester.CheckResult(kF, static_cast<uint8_t>(expected_output_3), 3);
}

TEST(WasmPackedArrayS) {
  WasmGCTester tester;
  uint32_t array_index = tester.DefineArray(kWasmI16, true);
  ValueType array_type = optref(array_index);

  int32_t expected_outputs[] = {0x12345678, 10, 0xFEDC, 0xFF1234};

  uint32_t param_index = 0;
  uint32_t local_index = 1;
  const uint32_t kF = tester.DefineFunction(
      tester.sigs.i_i(), {array_type},
      {WASM_SET_LOCAL(
           local_index,
           WASM_ARRAY_NEW(array_index, WASM_I32V(0x12345678), WASM_I32V(4))),
       WASM_ARRAY_SET(array_index, WASM_GET_LOCAL(local_index), WASM_I32V(1),
                      WASM_I32V(10)),
       WASM_ARRAY_SET(array_index, WASM_GET_LOCAL(local_index), WASM_I32V(2),
                      WASM_I32V(0xFEDC)),
       WASM_ARRAY_SET(array_index, WASM_GET_LOCAL(local_index), WASM_I32V(3),
                      WASM_I32V(0xFF1234)),
       WASM_ARRAY_GET_S(array_index, WASM_GET_LOCAL(local_index),
                        WASM_GET_LOCAL(param_index)),
       kExprEnd});

  tester.CompileModule();
  // Exactly the 2 lsb's should be stored by array.new.
  tester.CheckResult(kF, static_cast<int16_t>(expected_outputs[0]), 0);
  tester.CheckResult(kF, static_cast<int16_t>(expected_outputs[1]), 1);
  // Sign should be extended.
  tester.CheckResult(kF, static_cast<int16_t>(expected_outputs[2]), 2);
  // Exactly the 2 lsb's should be stored by array.set.
  tester.CheckResult(kF, static_cast<int16_t>(expected_outputs[3]), 3);
}

TEST(BasicRTT) {
  WasmGCTester tester;
  uint32_t type_index = tester.DefineStruct({F(wasm::kWasmI32, true)});
  uint32_t subtype_index =
      tester.DefineStruct({F(wasm::kWasmI32, true), F(wasm::kWasmI32, true)});
  ValueType kRttTypes[] = {ValueType::Rtt(type_index, 1)};
  FunctionSig sig_t_v(1, 0, kRttTypes);
  ValueType kRttSubtypes[] = {
      ValueType::Rtt(static_cast<HeapType>(subtype_index), 2)};
  FunctionSig sig_t2_v(1, 0, kRttSubtypes);
  ValueType kRttTypesDeeper[] = {ValueType::Rtt(type_index, 2)};
  FunctionSig sig_t3_v(1, 0, kRttTypesDeeper);
  ValueType kRefTypes[] = {ref(type_index)};
  FunctionSig sig_q_v(1, 0, kRefTypes);

  const uint32_t kRttCanon = tester.DefineFunction(
      &sig_t_v, {}, {WASM_RTT_CANON(type_index), kExprEnd});
  const uint32_t kRttSub = tester.DefineFunction(
      &sig_t2_v, {},
      {WASM_RTT_CANON(type_index), WASM_RTT_SUB(subtype_index), kExprEnd});
  const uint32_t kRttSubGeneric = tester.DefineFunction(
      &sig_t3_v, {},
      {WASM_RTT_CANON(kLocalEqRef), WASM_RTT_SUB(type_index), kExprEnd});
  const uint32_t kStructWithRtt = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_STRUCT_NEW_WITH_RTT(type_index, WASM_I32V(42),
                                WASM_RTT_CANON(type_index)),
       kExprEnd});
  const int kFieldIndex = 1;
  const int kLocalStructIndex = 1;  // Shifted in 'let' block.
  const int kLocalRttIndex = 0;     // Let-bound, hence first local.
  // This implements the following function:
  //   var local_struct: type0;
  //   let (local_rtt = rtt.sub(rtt.canon(type0), type1) in {
  //     local_struct = new type1 with rtt 'local_rtt';
  //     return (ref.test local_struct local_rtt) +
  //            ((ref.cast local_struct local_rtt)[field0]);
  //   }
  // The expected return value is 1+42 = 43.
  const uint32_t kRefCast = tester.DefineFunction(
      tester.sigs.i_v(), {optref(type_index)},
      /* TODO(jkummerow): The macro order here is a bit of a hack. */
      {WASM_RTT_CANON(type_index),
       WASM_LET_1_I(
           WASM_RTT(2, subtype_index), WASM_RTT_SUB(subtype_index),
           WASM_SET_LOCAL(kLocalStructIndex,
                          WASM_STRUCT_NEW_WITH_RTT(
                              subtype_index, WASM_I32V(11), WASM_I32V(42),
                              WASM_GET_LOCAL(kLocalRttIndex))),
           WASM_I32_ADD(
               WASM_REF_TEST(type_index, subtype_index,
                             WASM_GET_LOCAL(kLocalStructIndex),
                             WASM_GET_LOCAL(kLocalRttIndex)),
               WASM_STRUCT_GET(subtype_index, kFieldIndex,
                               WASM_REF_CAST(type_index, subtype_index,
                                             WASM_GET_LOCAL(kLocalStructIndex),
                                             WASM_GET_LOCAL(kLocalRttIndex)))),
           kExprEnd)});

  tester.CompileModule();

  Handle<Object> ref_result =
      tester.GetResultObject(kRttCanon).ToHandleChecked();

  CHECK(ref_result->IsMap());
  Handle<Map> map = Handle<Map>::cast(ref_result);
  CHECK(map->IsWasmStructMap());
  CHECK_EQ(reinterpret_cast<Address>(
               tester.instance()->module()->struct_type(type_index)),
           map->wasm_type_info().foreign_address());

  Handle<Object> subref_result =
      tester.GetResultObject(kRttSub).ToHandleChecked();
  CHECK(subref_result->IsMap());
  Handle<Map> submap = Handle<Map>::cast(subref_result);
  CHECK_EQ(*map, submap->wasm_type_info().parent());
  CHECK_EQ(reinterpret_cast<Address>(
               tester.instance()->module()->struct_type(subtype_index)),
           submap->wasm_type_info().foreign_address());
  Handle<Object> subref_result_canonicalized =
      tester.GetResultObject(kRttSub).ToHandleChecked();
  CHECK(subref_result.is_identical_to(subref_result_canonicalized));

  Handle<Object> sub_generic_1 =
      tester.GetResultObject(kRttSubGeneric).ToHandleChecked();
  Handle<Object> sub_generic_2 =
      tester.GetResultObject(kRttSubGeneric).ToHandleChecked();
  CHECK(sub_generic_1.is_identical_to(sub_generic_2));

  Handle<Object> s = tester.GetResultObject(kStructWithRtt).ToHandleChecked();
  CHECK(s->IsWasmStruct());
  CHECK_EQ(Handle<WasmStruct>::cast(s)->map(), *map);

  tester.CheckResult(kRefCast, 43);
}

TEST(RefTestCastNull) {
  WasmGCTester tester;
  uint8_t type_index =
      static_cast<uint8_t>(tester.DefineStruct({F(wasm::kWasmI32, true)}));

  const uint32_t kRefTestNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(type_index, type_index, WASM_REF_NULL(type_index),
                     WASM_RTT_CANON(type_index)),
       kExprEnd});

  const uint32_t kRefCastNull = tester.DefineFunction(
      tester.sigs.i_i(),  // Argument and return value ignored
      {},
      {WASM_REF_CAST(type_index, type_index, WASM_REF_NULL(type_index),
                     WASM_RTT_CANON(type_index)),
       kExprDrop, WASM_I32V(0), kExprEnd});
  tester.CompileModule();
  tester.CheckResult(kRefTestNull, 0);
  tester.CheckHasThrown(kRefCastNull, 0);
}

TEST(BasicI31) {
  WasmGCTester tester;
  const uint32_t kSigned = tester.DefineFunction(
      tester.sigs.i_i(), {},
      {WASM_I31_GET_S(WASM_I31_NEW(WASM_GET_LOCAL(0))), kExprEnd});
  const uint32_t kUnsigned = tester.DefineFunction(
      tester.sigs.i_i(), {},
      {WASM_I31_GET_U(WASM_I31_NEW(WASM_GET_LOCAL(0))), kExprEnd});
  // TODO(7748): Support (rtt.canon i31), and add a test like:
  // (ref.test (i31.new ...) (rtt.canon i31)).
  tester.CompileModule();
  tester.CheckResult(kSigned, 123, 123);
  tester.CheckResult(kUnsigned, 123, 123);
  // Truncation:
  tester.CheckResult(kSigned, 0x1234, static_cast<int32_t>(0x80001234));
  tester.CheckResult(kUnsigned, 0x1234, static_cast<int32_t>(0x80001234));
  // Sign/zero extension:
  tester.CheckResult(kSigned, -1, 0x7FFFFFFF);
  tester.CheckResult(kUnsigned, 0x7FFFFFFF, 0x7FFFFFFF);
}

TEST(JsAccessDisallowed) {
  WasmGCTester tester;
  uint32_t type_index = tester.DefineStruct({F(wasm::kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  FunctionSig sig_q_v(1, 0, kRefTypes);

  WasmFunctionBuilder* fun = tester.builder()->AddFunction(&sig_q_v);
  byte code[] = {WASM_STRUCT_NEW(type_index, WASM_I32V(42)), kExprEnd};
  fun->EmitCode(code, sizeof(code));
  tester.builder()->AddExport(CStrVector("f"), fun);
  tester.CompileModule();
  TryCatch try_catch(reinterpret_cast<v8::Isolate*>(tester.isolate()));
  MaybeHandle<WasmExportedFunction> exported =
      testing::GetExportedFunction(tester.isolate(), tester.instance(), "f");
  CHECK(!exported.is_null());
  CHECK(!try_catch.HasCaught());
  MaybeHandle<Object> result = Execution::Call(
      tester.isolate(), exported.ToHandleChecked(),
      tester.isolate()->factory()->undefined_value(), 0, nullptr);
  CHECK(result.is_null());
  CHECK(try_catch.HasCaught());
}

}  // namespace test_gc
}  // namespace wasm
}  // namespace internal
}  // namespace v8
