// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/fuzzing/random-module-generation.h"
#include "test/common/flag-utils.h"
#include "test/fuzzer/wasm/fuzzer-common.h"
#include "test/unittests/fuzztest.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::wasm::fuzzing {

class ModuleGenerationTest
    : public fuzztest::PerFuzzTestFixtureAdapter<TestWithContext> {
 public:
  ModuleGenerationTest() : zone_(&allocator_, "ModuleGenerationTest") {
    // Enable GC, required by `ResetTypeCanonicalizer`.
    v8_flags.expose_gc = true;
    EnableExperimentalWasmFeatures(isolate());
  }

  ~ModuleGenerationTest() override = default;

  void TestMVP(int tier_mask, int debug_mask, const std::vector<uint8_t>&);

 private:
  AccountingAllocator allocator_;
  Zone zone_;
};

// Fuzz tests.

void ModuleGenerationTest::TestMVP(int tier_mask, int debug_mask,
                                   const std::vector<uint8_t>& input) {
  // Set the tier mask to deterministically test a combination of Liftoff and
  // Turbofan.
  FlagScope<int> tier_mask_scope(&v8_flags.wasm_tier_mask_for_testing,
                                 tier_mask);
  // Generate debug code for some Liftoff functions.
  FlagScope<int> debug_mask_scope(&v8_flags.wasm_debug_mask_for_testing,
                                  debug_mask);

  zone_.Reset();
  base::Vector<const uint8_t> wire_bytes = GenerateRandomWasmModule(
      &zone_, WasmModuleGenerationOptions::MVP(), base::VectorOf(input));
  constexpr bool kRequireValid = true;
  SyncCompileAndExecuteAgainstReference(isolate(), wire_bytes, kRequireValid);
}

V8_FUZZ_TEST_F(ModuleGenerationTest, TestMVP)
    .WithDomains(fuzztest::Arbitrary<int>(),  // tier_mask
                 fuzztest::Arbitrary<int>(),  // debug_mask
                 fuzztest::VectorOf(fuzztest::Arbitrary<uint8_t>())
                     .WithMinSize(1)
                     .WithMaxSize(512));

}  // namespace v8::internal::wasm::fuzzing
