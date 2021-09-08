// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8config.h"

// TODO(clemensb): Extend this to other OSes.
#if V8_OS_POSIX && !V8_OS_FUCHSIA
#include <signal.h>
#endif  // V8_OS_POSIX && !V8_OS_FUCHSIA

#include "src/flags/flags.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace wasm {

enum MemoryProtectionMode {
  kNoProtection,
  kPku,
  kMprotect,
  kPkuWithMprotectFallback
};

const char* MemoryProtectionModeToString(MemoryProtectionMode mode) {
  switch (mode) {
    case kNoProtection:
      return "NoProtection";
    case kPku:
      return "Pku";
    case kMprotect:
      return "Mprotect";
    case kPkuWithMprotectFallback:
      return "PkuWithMprotectFallback";
  }
}

class MemoryProtectionTest : public TestWithNativeContext {
 public:
  void Initialize(MemoryProtectionMode mode) {
    mode_ = mode;
    bool enable_pku = mode == kPku || mode == kPkuWithMprotectFallback;
    FLAG_wasm_memory_protection_keys = enable_pku;
    if (enable_pku) {
      GetWasmCodeManager()->InitializeMemoryProtectionKeyForTesting();
    }

    bool enable_mprotect =
        mode == kMprotect || mode == kPkuWithMprotectFallback;
    FLAG_wasm_write_protect_code_memory = enable_mprotect;
  }

  void CompileModule() {
    CHECK_NULL(native_module_);
    native_module_ = CompileNativeModule();
    code_ = native_module_->GetCode(0);
  }

  NativeModule* native_module() const { return native_module_.get(); }

  WasmCode* code() const { return code_; }

  bool code_is_protected() {
    return V8_HAS_PTHREAD_JIT_WRITE_PROTECT || has_pku() || has_mprotect();
  }

  void MakeCodeWritable() {
    native_module_->MakeWritable(base::AddressRegionOf(code_->instructions()));
  }

  void WriteToCode() { code_->instructions()[0] = 0; }

 private:
  bool has_pku() {
    bool param_has_pku = mode_ == kPku || mode_ == kPkuWithMprotectFallback;
    return param_has_pku &&
           GetWasmCodeManager()->HasMemoryProtectionKeySupport();
  }

  bool has_mprotect() {
    return mode_ == kMprotect || mode_ == kPkuWithMprotectFallback;
  }

  std::shared_ptr<NativeModule> CompileNativeModule() {
    // Define the bytes for a module with a single empty function.
    static const byte module_bytes[] = {
        WASM_MODULE_HEADER, SECTION(Type, ENTRY_COUNT(1), SIG_ENTRY_v_v),
        SECTION(Function, ENTRY_COUNT(1), SIG_INDEX(0)),
        SECTION(Code, ENTRY_COUNT(1), ADD_COUNT(0 /* locals */, kExprEnd))};

    ModuleResult result =
        DecodeWasmModule(WasmFeatures::All(), std::begin(module_bytes),
                         std::end(module_bytes), false, kWasmOrigin,
                         isolate()->counters(), isolate()->metrics_recorder(),
                         v8::metrics::Recorder::ContextId::Empty(),
                         DecodingMethod::kSync, GetWasmEngine()->allocator());
    CHECK(result.ok());

    Handle<FixedArray> export_wrappers;
    ErrorThrower thrower(isolate(), "");
    constexpr int kNoCompilationId = 0;
    std::shared_ptr<NativeModule> native_module = CompileToNativeModule(
        isolate(), WasmFeatures::All(), &thrower, std::move(result).value(),
        ModuleWireBytes{base::ArrayVector(module_bytes)}, &export_wrappers,
        kNoCompilationId);
    CHECK(!thrower.error());
    CHECK_NOT_NULL(native_module);

    return native_module;
  }

  MemoryProtectionMode mode_;
  std::shared_ptr<NativeModule> native_module_;
  WasmCodeRefScope code_refs_;
  WasmCode* code_;
};

class ParameterizedMemoryProtectionTest
    : public MemoryProtectionTest,
      public ::testing::WithParamInterface<MemoryProtectionMode> {
 public:
  void SetUp() override { Initialize(GetParam()); }
};

#define ASSERT_DEATH_IF_PROTECTED(code)  \
  if (code_is_protected()) {             \
    ASSERT_DEATH_IF_SUPPORTED(code, ""); \
  } else {                               \
    code;                                \
  }

std::string PrintMemoryProtectionTestParam(
    ::testing::TestParamInfo<MemoryProtectionMode> info) {
  return MemoryProtectionModeToString(info.param);
}

INSTANTIATE_TEST_SUITE_P(MemoryProtection, ParameterizedMemoryProtectionTest,
                         ::testing::Values(kNoProtection, kPku, kMprotect,
                                           kPkuWithMprotectFallback),
                         PrintMemoryProtectionTestParam);

TEST_P(ParameterizedMemoryProtectionTest, CodeNotWritableAfterCompilation) {
  CompileModule();
  ASSERT_DEATH_IF_PROTECTED(WriteToCode());
}

TEST_P(ParameterizedMemoryProtectionTest, CodeWritableWithinScope) {
  CompileModule();
  CodeSpaceWriteScope write_scope(native_module());
  MakeCodeWritable();
  WriteToCode();
}

TEST_P(ParameterizedMemoryProtectionTest, CodeNotWritableAfterScope) {
  CompileModule();
  {
    CodeSpaceWriteScope write_scope(native_module());
    MakeCodeWritable();
    WriteToCode();
  }
  ASSERT_DEATH_IF_PROTECTED(WriteToCode());
}

#if V8_OS_POSIX && !V8_OS_FUCHSIA
class ParameterizedMemoryProtectionTestWithSignalHandling
    : public MemoryProtectionTest,
      public ::testing::WithParamInterface<
          std::tuple<MemoryProtectionMode, bool, bool>> {
 public:
  class SignalHandlerScope {
   public:
    SignalHandlerScope() {
      CHECK_NULL(current_handler_scope_);
      current_handler_scope_ = this;
      struct sigaction sa;
      sa.sa_sigaction = &HandleSignal;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONSTACK;
      CHECK_EQ(0, sigaction(SIGPROF, &sa, &old_signal_handler_));
    }

    ~SignalHandlerScope() {
      CHECK_EQ(current_handler_scope_, this);
      current_handler_scope_ = nullptr;
      sigaction(SIGPROF, &old_signal_handler_, nullptr);
    }

    void SetAddressToWriteToOnSignal(uint8_t* address) {
      CHECK_NULL(code_address_);
      CHECK_NOT_NULL(address);
      code_address_ = address;
    }

    int num_handled_signals() const { return handled_signals_; }

   private:
    static void HandleSignal(int signal, siginfo_t*, void*) {
      if (signal == SIGPROF) {
        printf("Handled SIGPROF.\n");
      } else {
        printf("Handled unknown signal: %d.\n", signal);
      }
      CHECK_NOT_NULL(current_handler_scope_);
      current_handler_scope_->handled_signals_ += 1;
      if (current_handler_scope_->code_address_ != nullptr) {
        printf("Writing to %p.\n", current_handler_scope_->code_address_);
        *current_handler_scope_->code_address_ = 0;
      }
    }

    struct sigaction old_signal_handler_;
    int handled_signals_ = 0;
    uint8_t* code_address_ = nullptr;

    // These are accessed from the signal handler.
    static SignalHandlerScope* current_handler_scope_;
  };

  void SetUp() override { Initialize(std::get<0>(GetParam())); }

  void TestSignalHandler() {
    const bool write_in_signal_handler = std::get<1>(GetParam());
    const bool open_write_scope = std::get<2>(GetParam());
    CompileModule();
    SignalHandlerScope signal_handler_scope;

    CHECK_EQ(0, signal_handler_scope.num_handled_signals());
    pthread_kill(pthread_self(), SIGPROF);
    CHECK_EQ(1, signal_handler_scope.num_handled_signals());

    uint8_t* code_start_ptr = &code()->instructions()[0];
    uint8_t code_start = *code_start_ptr;
    CHECK_NE(0, code_start);
    if (write_in_signal_handler) {
      signal_handler_scope.SetAddressToWriteToOnSignal(code_start_ptr);
    }
    // This will make us crash if code is protected and
    // {write_in_signal_handler} is set.
    {
      base::Optional<CodeSpaceWriteScope> write_scope;
      if (open_write_scope) write_scope.emplace(native_module());
      pthread_kill(pthread_self(), SIGPROF);
    }

    // If we write and code is protected, we never reach here.
    CHECK(!write_in_signal_handler || !code_is_protected());
    CHECK_EQ(2, signal_handler_scope.num_handled_signals());
    CHECK_EQ(write_in_signal_handler ? 0 : code_start, *code_start_ptr);
  }
};

// static
ParameterizedMemoryProtectionTestWithSignalHandling::SignalHandlerScope*
    ParameterizedMemoryProtectionTestWithSignalHandling::SignalHandlerScope::
        current_handler_scope_ = nullptr;

std::string PrintMemoryProtectionAndSignalHandlingTestParam(
    ::testing::TestParamInfo<std::tuple<MemoryProtectionMode, bool, bool>>
        info) {
  MemoryProtectionMode protection_mode = std::get<0>(info.param);
  const bool write_in_signal_handler = std::get<1>(info.param);
  const bool open_write_scope = std::get<2>(info.param);
  return std::string{MemoryProtectionModeToString(protection_mode)} + "_" +
         (write_in_signal_handler ? "Write" : "NoWrite") + "_" +
         (open_write_scope ? "WithScope" : "NoScope");
}

INSTANTIATE_TEST_SUITE_P(
    MemoryProtection, ParameterizedMemoryProtectionTestWithSignalHandling,
    ::testing::Combine(::testing::Values(kNoProtection, kPku, kMprotect,
                                         kPkuWithMprotectFallback),
                       ::testing::Bool(), ::testing::Bool()),
    PrintMemoryProtectionAndSignalHandlingTestParam);

TEST_P(ParameterizedMemoryProtectionTestWithSignalHandling, TestSignalHandler) {
  const bool write_in_signal_handler = std::get<1>(GetParam());
  if (write_in_signal_handler) {
    ASSERT_DEATH_IF_PROTECTED(TestSignalHandler());
  } else {
    TestSignalHandler();
  }
}
#endif  // V8_OS_POSIX && !V8_OS_FUCHSIA

}  // namespace wasm
}  // namespace internal
}  // namespace v8
