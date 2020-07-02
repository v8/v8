// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "src/utils/utils.h"
#include "src/utils/vector.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/struct-types.h"
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
        builder(&zone),
        isolate_(CcTest::InitIsolateOnce()),
        scope(isolate_),
        thrower(isolate_, "Test wasm GC") {
    testing::SetupIsolateForWasmModule(isolate_);
  }

  uint32_t AddGlobal(ValueType type, bool mutability, WasmInitExpr init) {
    return builder.AddGlobal(type, mutability, init);
  }

  void DefineFunction(const char* name, FunctionSig* sig,
                      std::initializer_list<ValueType> locals,
                      std::initializer_list<byte> code) {
    WasmFunctionBuilder* fun = builder.AddFunction(sig);
    builder.AddExport(CStrVector(name), fun);
    for (ValueType local : locals) {
      fun->AddLocal(local);
    }
    fun->EmitCode(code.begin(), static_cast<uint32_t>(code.size()));
  }

  uint32_t DefineStruct(std::initializer_list<F> fields) {
    StructType::Builder type_builder(&zone,
                                     static_cast<uint32_t>(fields.size()));
    for (F field : fields) {
      type_builder.AddField(field.first, field.second);
    }
    return builder.AddStructType(type_builder.Build());
  }

  uint32_t DefineArray(ValueType element_type, bool mutability) {
    return builder.AddArrayType(new (&zone)
                                    ArrayType(element_type, mutability));
  }

  void CompileModule() {
    ZoneBuffer buffer(&zone);
    builder.WriteTo(&buffer);
    MaybeHandle<WasmInstanceObject> maybe_instance =
        testing::CompileAndInstantiateForTesting(
            isolate_, &thrower, ModuleWireBytes(buffer.begin(), buffer.end()));
    if (thrower.error()) FATAL("%s", thrower.error_msg());
    instance_ = maybe_instance.ToHandleChecked();
  }

  void CheckResult(const char* function, int32_t expected,
                   std::initializer_list<Object> args) {
    Handle<Object>* argv = zone.NewArray<Handle<Object>>(args.size());
    int i = 0;
    for (Object arg : args) {
      argv[i++] = handle(arg, isolate_);
    }
    CHECK_EQ(expected, testing::CallWasmFunctionForTesting(
                           isolate_, instance_, &thrower, function,
                           static_cast<uint32_t>(args.size()), argv));
  }

  // TODO(7748): This uses the JavaScript interface to retrieve the plain
  // WasmStruct. Once the JS interaction story is settled, this may well
  // need to be changed.
  MaybeHandle<Object> GetJSResult(const char* function,
                                  std::initializer_list<Object> args) {
    Handle<Object>* argv = zone.NewArray<Handle<Object>>(args.size());
    int i = 0;
    for (Object arg : args) {
      argv[i++] = handle(arg, isolate_);
    }
    Handle<WasmExportedFunction> exported =
        testing::GetExportedFunction(isolate_, instance_, function)
            .ToHandleChecked();
    return Execution::Call(isolate_, exported,
                           isolate_->factory()->undefined_value(),
                           static_cast<uint32_t>(args.size()), argv);
  }

  void CheckHasThrown(const char* function,
                      std::initializer_list<Object> args) {
    TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate_));
    MaybeHandle<Object> result = GetJSResult(function, args);
    CHECK(result.is_null());
    CHECK(try_catch.HasCaught());
    isolate_->clear_pending_exception();
  }

  Handle<WasmInstanceObject> instance() { return instance_; }
  Isolate* isolate() { return isolate_; }

  TestSignatures sigs;

 private:
  const FlagScope<bool> flag_gc;
  const FlagScope<bool> flag_reftypes;
  const FlagScope<bool> flag_typedfuns;

  v8::internal::AccountingAllocator allocator;
  Zone zone;
  WasmModuleBuilder builder;

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

// TODO(7748): Use WASM_EXEC_TEST once interpreter and liftoff are supported
TEST(WasmBasicStruct) {
  WasmGCTester tester;
  uint32_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kOptRefType = optref(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  // Test struct.new and struct.get.
  tester.DefineFunction(
      "get1", tester.sigs.i_v(), {},
      {WASM_STRUCT_GET(
           type_index, 0,
           WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
       kExprEnd});

  // Test struct.new and struct.get.
  tester.DefineFunction(
      "get2", tester.sigs.i_v(), {},
      {WASM_STRUCT_GET(
           type_index, 1,
           WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
       kExprEnd});

  // Test struct.new, returning struct references to JS.
  tester.DefineFunction(
      "getJs", &sig_q_v, {},
      {WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64)), kExprEnd});

  // Test struct.set, struct refs types in locals.
  uint32_t j_local_index = 0;
  uint32_t j_field_index = 0;
  tester.DefineFunction(
      "set", tester.sigs.i_v(), {kOptRefType},
      {WASM_SET_LOCAL(j_local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(42),
                                                     WASM_I32V(64))),
       WASM_STRUCT_SET(type_index, j_field_index, WASM_GET_LOCAL(j_local_index),
                       WASM_I32V(-99)),
       WASM_STRUCT_GET(type_index, j_field_index,
                       WASM_GET_LOCAL(j_local_index)),
       kExprEnd});

  tester.CompileModule();

  tester.CheckResult("get1", 42, {});
  tester.CheckResult("get2", 64, {});
  CHECK(tester.GetJSResult("getJs", {}).ToHandleChecked()->IsWasmStruct());
  tester.CheckResult("set", -99, {});
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

  uint32_t global_index = tester.AddGlobal(
      kOptRefType, true, WasmInitExpr(WasmInitExpr::kRefNullConst));
  uint32_t field_index = 0;
  tester.DefineFunction(
      "f", tester.sigs.i_v(), {},
      {WASM_SET_GLOBAL(global_index, WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                     WASM_I32V(66))),
       WASM_STRUCT_GET(
           type_index, field_index,
           WASM_REF_AS_NON_NULL(WASM_IF_ELSE_R(
               kOptRefType, WASM_I32V(1), WASM_GET_GLOBAL(global_index),
               WASM_REF_NULL(static_cast<byte>(type_index))))),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult("f", 55, {});
}

TEST(WasmBrOnNull) {
  WasmGCTester tester;
  uint32_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kOptRefType = optref(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);
  uint32_t l_local_index = 0;
  tester.DefineFunction(
      "taken", tester.sigs.i_v(), {kOptRefType},
      {WASM_BLOCK_I(WASM_I32V(42),
                    // Branch will be taken.
                    // 42 left on stack outside the block (not 52).
                    WASM_BR_ON_NULL(0, WASM_GET_LOCAL(l_local_index)),
                    WASM_I32V(52), WASM_BR(0)),
       kExprEnd});

  uint32_t m_field_index = 0;
  tester.DefineFunction(
      "notTaken", tester.sigs.i_v(), {},
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
  tester.CheckResult("taken", 42, {});
  tester.CheckResult("notTaken", 52, {});
}

TEST(WasmRefEq) {
  WasmGCTester tester;
  byte type_index = static_cast<byte>(
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)}));
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kOptRefType = optref(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  byte local_index = 0;
  tester.DefineFunction(
      "f", tester.sigs.i_v(), {kOptRefType},
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
  tester.CheckResult("f", 0b1001, {});
}

TEST(WasmPackedStructU) {
  WasmGCTester tester;

  uint32_t type_index = tester.DefineStruct(
      {F(kWasmI8, true), F(kWasmI16, true), F(kWasmI32, true)});
  ValueType struct_type = optref(type_index);

  uint32_t local_index = 0;

  int32_t expected_output_0 = 0x1234;
  int32_t expected_output_1 = -1;

  tester.DefineFunction(
      "f_0", tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(local_index,
                      WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                                      WASM_I32V(expected_output_1),
                                      WASM_I32V(0x12345678))),
       WASM_STRUCT_GET_U(type_index, 0, WASM_GET_LOCAL(local_index)),
       kExprEnd});

  tester.DefineFunction(
      "f_1", tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(local_index,
                      WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                                      WASM_I32V(expected_output_1),
                                      WASM_I32V(0x12345678))),
       WASM_STRUCT_GET_U(type_index, 1, WASM_GET_LOCAL(local_index)),
       kExprEnd});
  tester.CompileModule();

  tester.CheckResult("f_0", static_cast<uint8_t>(expected_output_0), {});
  tester.CheckResult("f_1", static_cast<uint16_t>(expected_output_1), {});
}

TEST(WasmPackedStructS) {
  WasmGCTester tester;

  uint32_t type_index = tester.DefineStruct(
      {F(kWasmI8, true), F(kWasmI16, true), F(kWasmI32, true)});
  ValueType struct_type = optref(type_index);

  uint32_t local_index = 0;

  int32_t expected_output_0 = 0x80;
  int32_t expected_output_1 = 42;

  tester.DefineFunction(
      "f_0", tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(
           local_index,
           WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                           WASM_I32V(expected_output_1), WASM_I32V(0))),
       WASM_STRUCT_GET_S(type_index, 0, WASM_GET_LOCAL(local_index)),
       kExprEnd});

  tester.DefineFunction(
      "f_1", tester.sigs.i_v(), {struct_type},
      {WASM_SET_LOCAL(local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(0x80),
                                                   WASM_I32V(expected_output_1),
                                                   WASM_I32V(0))),
       WASM_STRUCT_GET_S(type_index, 1, WASM_GET_LOCAL(local_index)),
       kExprEnd});

  tester.CompileModule();

  tester.CheckResult("f_0", static_cast<int8_t>(expected_output_0), {});
  tester.CheckResult("f_1", static_cast<int16_t>(expected_output_1), {});
}

TEST(WasmLetInstruction) {
  WasmGCTester tester;
  uint32_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});

  uint32_t let_local_index = 0;
  uint32_t let_field_index = 0;
  tester.DefineFunction(
      "let_test_1", tester.sigs.i_v(), {},
      {WASM_LET_1_I(WASM_SEQ(kLocalRef, static_cast<byte>(type_index)),
                    WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(52)),
                    WASM_STRUCT_GET(type_index, let_field_index,
                                    WASM_GET_LOCAL(let_local_index))),
       kExprEnd});

  uint32_t let_2_field_index = 0;
  tester.DefineFunction(
      "let_test_2", tester.sigs.i_v(), {},
      {WASM_LET_2_I(kLocalI32, WASM_I32_ADD(WASM_I32V(42), WASM_I32V(-32)),
                    WASM_SEQ(kLocalRef, static_cast<byte>(type_index)),
                    WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(52)),
                    WASM_I32_MUL(WASM_STRUCT_GET(type_index, let_2_field_index,
                                                 WASM_GET_LOCAL(1)),
                                 WASM_GET_LOCAL(0))),
       kExprEnd});

  tester.DefineFunction(
      "let_test_locals", tester.sigs.i_i(), {kWasmI32},
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
  tester.DefineFunction("let_test_erase", tester.sigs.i_v(), {kWasmI32},
                        {WASM_SET_LOCAL(let_erase_local_index, WASM_I32V(0)),
                         WASM_LET_1_V(kLocalI32, WASM_I32V(1), WASM_NOP),
                         WASM_GET_LOCAL(let_erase_local_index), kExprEnd});
  // The result should be 0 and not 1, as local_get(0) refers to the original
  // local.

  tester.CompileModule();

  tester.CheckResult("let_test_1", 42, {});
  tester.CheckResult("let_test_2", 420, {});
  tester.CheckResult("let_test_locals", 891, {Smi::FromInt(1000)});
  tester.CheckResult("let_test_erase", 0, {});
}

TEST(WasmBasicArray) {
  WasmGCTester tester;
  uint32_t type_index = tester.DefineArray(wasm::kWasmI32, true);
  ValueType kRefTypes[] = {ref(type_index)};
  FunctionSig sig_q_v(1, 0, kRefTypes);
  ValueType kOptRefType = optref(type_index);

  // f: a = [12, 12, 12]; a[1] = 42; return a[arg0]
  uint32_t local_index = 1;
  tester.DefineFunction(
      "f", tester.sigs.i_i(), {kOptRefType},
      {WASM_SET_LOCAL(local_index,
                      WASM_ARRAY_NEW(type_index, WASM_I32V(12), WASM_I32V(3))),
       WASM_ARRAY_SET(type_index, WASM_GET_LOCAL(local_index), WASM_I32V(1),
                      WASM_I32V(42)),
       WASM_ARRAY_GET(type_index, WASM_GET_LOCAL(local_index),
                      WASM_GET_LOCAL(0)),
       kExprEnd});

  // Reads and returns an array's length.
  tester.DefineFunction(
      "g", tester.sigs.i_v(), {},
      {WASM_ARRAY_LEN(type_index,
                      WASM_ARRAY_NEW(type_index, WASM_I32V(0), WASM_I32V(42))),
       kExprEnd});

  // Create an array of length 2, initialized to [42, 42].
  tester.DefineFunction(
      "h", &sig_q_v, {},
      {WASM_ARRAY_NEW(type_index, WASM_I32V(42), WASM_I32V(2)), kExprEnd});

  tester.CompileModule();

  tester.CheckResult("f", 12, {Smi::FromInt(0)});
  tester.CheckResult("f", 42, {Smi::FromInt(1)});
  tester.CheckResult("f", 12, {Smi::FromInt(2)});
  tester.CheckHasThrown("f", {Smi::FromInt(3)});
  tester.CheckHasThrown("f", {Smi::FromInt(-1)});
  tester.CheckResult("g", 42, {});

  MaybeHandle<Object> h_result = tester.GetJSResult("h", {});
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

  tester.DefineFunction(
      "f", tester.sigs.i_i(), {array_type},
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
  tester.CheckResult("f", 1, {Smi::FromInt(0)});
  tester.CheckResult("f", 10, {Smi::FromInt(1)});
  tester.CheckResult("f", 200, {Smi::FromInt(2)});
  // Only the 2 lsb's of 258 should be stored in the array.
  tester.CheckResult("f", static_cast<uint8_t>(expected_output_3),
                     {Smi::FromInt(3)});
}

TEST(WasmPackedArrayS) {
  WasmGCTester tester;
  uint32_t array_index = tester.DefineArray(kWasmI16, true);
  ValueType array_type = optref(array_index);

  int32_t expected_outputs[] = {0x12345678, 10, 0xFEDC, 0xFF1234};

  uint32_t param_index = 0;
  uint32_t local_index = 1;
  tester.DefineFunction(
      "f", tester.sigs.i_i(), {array_type},
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
  tester.CheckResult("f", static_cast<int16_t>(expected_outputs[0]),
                     {Smi::FromInt(0)});
  tester.CheckResult("f", static_cast<int16_t>(expected_outputs[1]),
                     {Smi::FromInt(1)});
  // Sign should be extended.
  tester.CheckResult("f", static_cast<int16_t>(expected_outputs[2]),
                     {Smi::FromInt(2)});
  // Exactly the 2 lsb's should be stored by array.set.
  tester.CheckResult("f", static_cast<int16_t>(expected_outputs[3]),
                     {Smi::FromInt(3)});
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
  ValueType kRefTypes[] = {ref(type_index)};
  FunctionSig sig_q_v(1, 0, kRefTypes);

  tester.DefineFunction("f", &sig_t_v, {},
                        {WASM_RTT_CANON(type_index), kExprEnd});
  tester.DefineFunction(
      "g", &sig_t2_v, {},
      {WASM_RTT_CANON(type_index), WASM_RTT_SUB(subtype_index), kExprEnd});
  tester.DefineFunction("h", &sig_q_v, {},
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
  tester.DefineFunction(
      "i", tester.sigs.i_v(), {optref(type_index)},
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

  Handle<Object> ref_result = tester.GetJSResult("f", {}).ToHandleChecked();

  CHECK(ref_result->IsMap());
  Handle<Map> map = Handle<Map>::cast(ref_result);
  CHECK(map->IsWasmStructMap());
  CHECK_EQ(reinterpret_cast<Address>(
               tester.instance()->module()->struct_type(type_index)),
           map->wasm_type_info().foreign_address());

  Handle<Object> subref_result = tester.GetJSResult("g", {}).ToHandleChecked();
  CHECK(subref_result->IsMap());
  Handle<Map> submap = Handle<Map>::cast(subref_result);
  CHECK_EQ(*map, submap->wasm_type_info().parent());
  CHECK_EQ(reinterpret_cast<Address>(
               tester.instance()->module()->struct_type(subtype_index)),
           submap->wasm_type_info().foreign_address());

  Handle<Object> s = tester.GetJSResult("h", {}).ToHandleChecked();
  CHECK(s->IsWasmStruct());
  CHECK_EQ(Handle<WasmStruct>::cast(s)->map(), *map);
}

TEST(BasicI31) {
  WasmGCTester tester;
  tester.DefineFunction(
      "f", tester.sigs.i_i(), {},
      {WASM_I31_GET_S(WASM_I31_NEW(WASM_GET_LOCAL(0))), kExprEnd});
  tester.DefineFunction(
      "g", tester.sigs.i_i(), {},
      {WASM_I31_GET_U(WASM_I31_NEW(WASM_GET_LOCAL(0))), kExprEnd});
  // TODO(7748): Support (rtt.canon i31), and add a test like:
  // (ref.test (i31.new ...) (rtt.canon i31)).
  tester.CompileModule();
  tester.CheckResult("f", 123, {Smi::FromInt(123)});
  tester.CheckResult("g", 123, {Smi::FromInt(123)});
  // Truncation:
  tester.CheckResult(
      "f", 0x1234,
      {*tester.isolate()->factory()->NewNumberFromUint(0x80001234)});
  tester.CheckResult(
      "g", 0x1234,
      {*tester.isolate()->factory()->NewNumberFromUint(0x80001234)});
  // Sign/zero extension:
  tester.CheckResult(
      "f", -1, {*tester.isolate()->factory()->NewNumberFromUint(0x7FFFFFFF)});
  tester.CheckResult(
      "g", 0x7FFFFFFF,
      {*tester.isolate()->factory()->NewNumberFromUint(0x7FFFFFFF)});
}

}  // namespace test_gc
}  // namespace wasm
}  // namespace internal
}  // namespace v8
