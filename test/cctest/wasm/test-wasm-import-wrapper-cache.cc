// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-compiler.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-import-wrapper-cache.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "test/cctest/cctest.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/test-signatures.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_wasm_import_wrapper_cache {

std::shared_ptr<NativeModule> NewModule(Isolate* isolate) {
  auto module = std::make_shared<WasmModule>(kWasmOrigin);
  size_t kCodeSizeEstimate = 0;
  auto native_module = GetWasmEngine()->NewNativeModule(
      isolate, WasmEnabledFeatures::All(), WasmDetectedFeatures{},
      CompileTimeImports{}, std::move(module), kCodeSizeEstimate);
  native_module->SetWireBytes({});
  return native_module;
}

TEST(CacheHit) {
  FlagScope<bool> cleanup_immediately(&v8_flags.stress_wasm_code_gc, true);
  Isolate* isolate = CcTest::InitIsolateOnce();
  auto module = NewModule(isolate);
  TestSignatures sigs;

  auto kind = ImportCallKind::kJSFunction;
  auto sig = sigs.i_i();
  CanonicalTypeIndex type_index =
      GetTypeCanonicalizer()->AddRecursiveGroup(sig);
  int expected_arity = static_cast<int>(sig->parameter_count());
  auto* canonical_sig =
      GetTypeCanonicalizer()->LookupFunctionSignature(type_index);
  {
    std::shared_ptr<wasm::WasmImportWrapperHandle> c1 =
        CompileImportWrapperForTest(isolate, kind, canonical_sig, type_index,
                                    expected_arity, kNoSuspend);

    CHECK(c1->has_code());
    CHECK_EQ(WasmCode::Kind::kWasmToJsWrapper, c1->code()->kind());

    std::shared_ptr<wasm::WasmImportWrapperHandle> c2 =
        GetWasmImportWrapperCache()->Get(isolate, kind, type_index,
                                         expected_arity, kNoSuspend,
                                         canonical_sig);

    CHECK(c2->has_code());
    CHECK_EQ(c1, c2);
  }
  // Ending the lifetime of the {WasmCodeRefScope} should drop the refcount
  // of the wrapper to zero, causing its cleanup at the next Wasm Code GC
  // (requested via interrupt).
  isolate->stack_guard()->HandleInterrupts();
  CHECK(!GetWasmImportWrapperCache()->HasCodeForTesting(
      kind, type_index, expected_arity, kNoSuspend));
}

TEST(CacheMissSig) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  auto module = NewModule(isolate);
  TestSignatures sigs;
  WasmCodeRefScope wasm_code_ref_scope;

  auto kind = ImportCallKind::kJSFunction;
  auto* sig1 = sigs.i_i();
  int expected_arity1 = static_cast<int>(sig1->parameter_count());
  CanonicalTypeIndex type_index1 =
      GetTypeCanonicalizer()->AddRecursiveGroup(sig1);
  auto* canonical_sig1 =
      GetTypeCanonicalizer()->LookupFunctionSignature(type_index1);
  auto sig2 = sigs.i_ii();
  int expected_arity2 = static_cast<int>(sig2->parameter_count());
  CanonicalTypeIndex type_index2 =
      GetTypeCanonicalizer()->AddRecursiveGroup(sig2);

  std::shared_ptr<wasm::WasmImportWrapperHandle> c1 =
      CompileImportWrapperForTest(isolate, kind, canonical_sig1, type_index1,
                                  expected_arity1, kNoSuspend);

  CHECK(c1->has_code());
  CHECK_EQ(WasmCode::Kind::kWasmToJsWrapper, c1->code()->kind());

  CHECK(!GetWasmImportWrapperCache()->HasCodeForTesting(
      kind, type_index2, expected_arity2, kNoSuspend));
}

TEST(CacheMissKind) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  auto module = NewModule(isolate);
  TestSignatures sigs;
  WasmCodeRefScope wasm_code_ref_scope;

  auto kind1 = ImportCallKind::kJSFunction;
  auto kind2 = ImportCallKind::kUseCallBuiltin;
  auto sig = sigs.i_i();
  int expected_arity = static_cast<int>(sig->parameter_count());
  CanonicalTypeIndex type_index =
      GetTypeCanonicalizer()->AddRecursiveGroup(sig);
  auto* canonical_sig =
      GetTypeCanonicalizer()->LookupFunctionSignature(type_index);

  std::shared_ptr<wasm::WasmImportWrapperHandle> c1 =
      CompileImportWrapperForTest(isolate, kind1, canonical_sig, type_index,
                                  expected_arity, kNoSuspend);

  CHECK(c1->has_code());
  CHECK_EQ(WasmCode::Kind::kWasmToJsWrapper, c1->code()->kind());

  CHECK(!GetWasmImportWrapperCache()->HasCodeForTesting(
      kind2, type_index, expected_arity, kNoSuspend));
}

TEST(CacheHitMissSig) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  auto module = NewModule(isolate);
  TestSignatures sigs;
  WasmCodeRefScope wasm_code_ref_scope;

  auto kind = ImportCallKind::kJSFunction;
  auto sig1 = sigs.i_i();
  int expected_arity1 = static_cast<int>(sig1->parameter_count());
  CanonicalTypeIndex type_index1 =
      GetTypeCanonicalizer()->AddRecursiveGroup(sig1);
  auto* canonical_sig1 =
      GetTypeCanonicalizer()->LookupFunctionSignature(type_index1);
  auto sig2 = sigs.i_ii();
  int expected_arity2 = static_cast<int>(sig2->parameter_count());
  CanonicalTypeIndex type_index2 =
      GetTypeCanonicalizer()->AddRecursiveGroup(sig2);
  auto* canonical_sig2 =
      GetTypeCanonicalizer()->LookupFunctionSignature(type_index2);

  std::shared_ptr<wasm::WasmImportWrapperHandle> c1 =
      CompileImportWrapperForTest(isolate, kind, canonical_sig1, type_index1,
                                  expected_arity1, kNoSuspend);

  CHECK_NOT_NULL(c1);
  CHECK_EQ(WasmCode::Kind::kWasmToJsWrapper, c1->code()->kind());

  CHECK(!GetWasmImportWrapperCache()->HasCodeForTesting(
      kind, type_index2, expected_arity2, kNoSuspend));

  std::shared_ptr<wasm::WasmImportWrapperHandle> c2 =
      CompileImportWrapperForTest(isolate, kind, canonical_sig2, type_index2,
                                  expected_arity2, kNoSuspend);

  CHECK_NE(c1, c2);

  std::shared_ptr<wasm::WasmImportWrapperHandle> c3 =
      GetWasmImportWrapperCache()->Get(isolate, kind, type_index1,
                                       expected_arity1, kNoSuspend,
                                       canonical_sig1);

  CHECK(c3->has_code());
  CHECK_EQ(c1, c3);

  std::shared_ptr<wasm::WasmImportWrapperHandle> c4 =
      GetWasmImportWrapperCache()->Get(isolate, kind, type_index2,
                                       expected_arity2, kNoSuspend,
                                       canonical_sig2);

  CHECK(c4->has_code());
  CHECK_EQ(c2, c4);
}

}  // namespace test_wasm_import_wrapper_cache
}  // namespace wasm
}  // namespace internal
}  // namespace v8
