// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-decoder.h"

#include <iomanip>
#include <vector>

#include "src/execution/isolate.h"
#include "src/execution/local-isolate-inl.h"
#include "src/heap/factory.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"
#include "src/init/v8.h"
#include "src/objects/contexts.h"
#include "src/objects/fixed-array.h"
#include "src/runtime/runtime.h"
#include "test/unittests/interpreter/bytecode-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace interpreter {

#define B(Name) static_cast<uint8_t>(Bytecode::k##Name)

using BytecodeDecoderTest = TestWithIsolate;

TEST_F(BytecodeDecoderTest, DecodeBytecodeAndOperands) {
  struct BytecodesAndResult {
    const uint8_t bytecode[32];
    const size_t length;
    const char* output;
  };

  const BytecodesAndResult cases[] = {
      {{B(LdaSmi), U8(1)}, 2, "            LdaSmi [1]"},
      {{B(Wide), B(LdaSmi), U16(1000)}, 4, "      LdaSmi.Wide [1000]"},
      {{B(ExtraWide), B(LdaSmi), U32(100000)}, 6, "LdaSmi.ExtraWide [100000]"},
      {{B(LdaSmi), U8(-1)}, 2, "            LdaSmi [-1]"},
      {{B(Wide), B(LdaSmi), U16(-1000)}, 4, "      LdaSmi.Wide [-1000]"},
      {{B(ExtraWide), B(LdaSmi), U32(-100000)},
       6,
       "LdaSmi.ExtraWide [-100000]"},
      {{B(Star), R8(5)}, 2, "            Star r5"},
      {{B(Wide), B(Star), R16(136)}, 4, "      Star.Wide r136"},
      {{B(Wide), B(CallAnyReceiver), R16(134), R16(135), U16(10), U16(177)},
       10,
       "CallAnyReceiver.Wide r134, r135-r144, FBV[177]"},
      {{B(ForInPrepare), R8(10), U8(11)},
       3,
       "         ForInPrepare r10-r12, FBV[11]"},
      {{B(CallRuntime), U16(Runtime::FunctionId::kIsSmi), R8(0), U8(0)},
       5,
       "   CallRuntime [IsSmi], r0-r0"},
      {{B(Ldar),
        static_cast<uint8_t>(Register::FromParameterIndex(2).ToOperand())},
       2,
       "            Ldar a1"},
      {{B(Wide), B(CreateObjectLiteral), U16(513), U16(1027), U8(165)},
       7,
       "CreateObjectLiteral.Wide [513:0], FBV[1027], #a5"},
      {{B(ExtraWide), B(JumpIfNull), U32(123456789)},
       6,
       "JumpIfNull.ExtraWide [123456789]"},
      {{B(CallJSRuntime), U8(Context::BOOLEAN_FUNCTION_INDEX), R8(0), U8(0)},
       4,
       "      CallJSRuntime [boolean_function], r0-r0"},
      {{B(TestEqualStrict), R8(0), U8(1)},
       3,
       "         TestEqualStrict r0, EmbeddedFeedback[Boolean]"},
      {{B(Add), R8(0), U8(1)},
       3,
       "         Add r0, EmbeddedFeedback[SignedSmall]"},
      {{B(Inc), U8(1)}, 2, "            Inc EmbeddedFeedback[SignedSmall]"},
  };

  Handle<TrustedFixedArray> constant_pool =
      factory()->NewTrustedFixedArray(2000);

  for (size_t i = 0; i < arraysize(cases); ++i) {
    // Generate reference string by prepending formatted bytes.
    std::stringstream expected_ss;
    std::ios default_format(nullptr);
    default_format.copyfmt(expected_ss);
    // Match format of BytecodeDecoder::Decode() for byte representations.
    expected_ss.fill('0');
    expected_ss.flags(std::ios::right | std::ios::hex);
    for (size_t b = 0; b < cases[i].length; b++) {
      expected_ss << std::setw(2) << static_cast<uint32_t>(cases[i].bytecode[b])
                  << ' ';
    }
    expected_ss.copyfmt(default_format);
    expected_ss << cases[i].output;

    // Generate decoded byte output.
    std::stringstream actual_ss;
    BytecodeDecoder::Decode(actual_ss, cases[i].bytecode, *constant_pool);

    // Compare.
    CHECK_EQ(actual_ss.str(), expected_ss.str());
  }
}

namespace {

class BackgroundDecodeThread final : public base::Thread {
 public:
  BackgroundDecodeThread(Isolate* isolate, const uint8_t* bytecode,
                         std::unique_ptr<PersistentHandles> ph,
                         IndirectHandle<TrustedFixedArray> constant_pool,
                         std::string* output)
      : base::Thread(Options("BackgroundDecodeThread")),
        isolate_(isolate),
        bytecode_(bytecode),
        ph_(std::move(ph)),
        constant_pool_(constant_pool),
        output_(output) {}

  void Run() override {
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
    local_isolate.heap()->AttachPersistentHandles(std::move(ph_));
    UnparkedScope unparked_scope(&local_isolate);
    std::stringstream ss;
    BytecodeDecoder::Decode(ss, bytecode_, *constant_pool_, false);
    *output_ = ss.str();
  }

 private:
  Isolate* isolate_;
  const uint8_t* bytecode_;
  std::unique_ptr<PersistentHandles> ph_;
  IndirectHandle<TrustedFixedArray> constant_pool_;
  std::string* output_;
};

}  // namespace

TEST_F(BytecodeDecoderTest, OmitConstantPoolDetailsOffMainThread) {
  const uint8_t bytecode[] = {B(LdaConstant), U8(0)};
  Handle<TrustedFixedArray> constant_pool = factory()->NewTrustedFixedArray(1);
  Handle<String> str = factory()->NewStringFromAsciiChecked("hello");
  constant_pool->set(0, *str);

  std::stringstream main_ss;
  BytecodeDecoder::Decode(main_ss, bytecode, *constant_pool, false);
  CHECK_EQ(main_ss.str(), "LdaConstant [0:\"hello\"]");

  auto ph = i_isolate()->NewPersistentHandles();
  IndirectHandle<TrustedFixedArray> persistent_cp =
      ph->NewHandle(*constant_pool);
  std::string bg_output;
  BackgroundDecodeThread thread(i_isolate(), bytecode, std::move(ph),
                                persistent_cp, &bg_output);
  CHECK(thread.Start());
  i_isolate()->main_thread_local_isolate()->ExecuteMainThreadWhileParked(
      [&]() { thread.Join(); });
  CHECK_EQ(bg_output, "LdaConstant [0]");
}

#undef B

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
