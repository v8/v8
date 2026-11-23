// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/base/strong-alias.h"
#include "src/codegen/compiler.h"
#include "src/common/globals.h"
#include "src/execution/isolate-inl.h"
#include "src/objects/js-objects-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/string.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"
#include "src/parsing/pending-compilation-error-handler.h"
#include "test/fuzzer/fuzzer-support.h"

namespace {

namespace i = v8::internal;

constexpr size_t kMaxInputSize = 2048;
constexpr size_t kMaxCompileSize = 1024;

void RunDifferentialOracleForMode(v8::Isolate* isolate,
                                  i::Handle<i::String> source, bool is_module,
                                  i::LanguageMode language_mode) {
  v8::TryCatch try_catch(isolate);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::HandleScope handle_scope(i_isolate);
  i::Factory* factory = i_isolate->factory();

  i::Handle<i::Script> script_eager = factory->NewScript(source);
  i::Handle<i::Script> script_lazy = factory->NewScript(source);
  if (is_module) {
    v8::ScriptOriginOptions module_options(false, false, false, true);
    script_eager->set_origin_options(module_options);
    script_lazy->set_origin_options(module_options);
  }
  script_eager->set_outer_language_mode(language_mode);
  script_lazy->set_outer_language_mode(language_mode);

  i::UnoptimizedCompileState state_eager;
  i::ReusableUnoptimizedCompileState reusable_eager(i_isolate);
  i::UnoptimizedCompileFlags flags_eager =
      i::UnoptimizedCompileFlags::ForScriptCompile(i_isolate, *script_eager);
  flags_eager.set_allow_lazy_parsing(false);
  flags_eager.set_allow_lazy_compile(false);
  i::ParseInfo info_eager(i_isolate, flags_eager, &state_eager,
                          &reusable_eager);

  i::UnoptimizedCompileState state_lazy;
  i::ReusableUnoptimizedCompileState reusable_lazy(i_isolate);
  i::UnoptimizedCompileFlags flags_lazy =
      i::UnoptimizedCompileFlags::ForScriptCompile(i_isolate, *script_lazy);
  flags_lazy.set_allow_lazy_parsing(true);
  flags_lazy.set_allow_lazy_compile(true);
  i::ParseInfo info_lazy(i_isolate, flags_lazy, &state_lazy, &reusable_lazy);

  bool ok_eager =
      i::parsing::ParseProgram(&info_eager, script_eager, i_isolate,
                               i::parsing::ReportStatisticsMode{false});
  bool ok_lazy =
      i::parsing::ParseProgram(&info_lazy, script_lazy, i_isolate,
                               i::parsing::ReportStatisticsMode{false});

  if (info_eager.pending_error_handler()->stack_overflow() ||
      info_lazy.pending_error_handler()->stack_overflow()) {
    return;
  }

  // Stage 1: Eager Parser vs. Lazy PreParser accept/reject & error parity.
  CHECK_EQ(ok_eager, ok_lazy);
  if (!ok_eager) {
    info_eager.pending_error_handler()->PrepareErrors(
        i_isolate, info_eager.ast_value_factory());
    info_lazy.pending_error_handler()->PrepareErrors(
        i_isolate, info_lazy.ast_value_factory());
    CHECK(i::String::Equals(
        i_isolate,
        info_eager.pending_error_handler()->FormatErrorMessageForTest(
            i_isolate),
        info_lazy.pending_error_handler()->FormatErrorMessageForTest(
            i_isolate)));
    info_eager.pending_error_handler()->ReportErrors(i_isolate, script_eager);
    try_catch.Reset();
    return;
  }

  if (source->length() > kMaxCompileSize) return;

  // Stage 2: Reuse already-parsed info_lazy to run CompileToplevel with
  // --stress-lazy and --stress-lazy-source-positions.
  i::IsCompiledScope is_compiled_scope;
  if (!i::Compiler::CompileToplevel(&info_lazy, script_lazy, i_isolate,
                                    &is_compiled_scope)
           .is_null() &&
      i::v8_flags.stress_lazy) {
    i::SharedFunctionInfo::ScriptIterator iterator(i_isolate, *script_lazy);
    for (i::Tagged<i::SharedFunctionInfo> shared = iterator.Next();
         !shared.is_null(); shared = iterator.Next()) {
      if (shared->is_compiled()) continue;
      // StressLazy discards exceptions and only holds one IsCompiledScope per
      // loop iteration, so an SFI may be uncompiled due to GC flushing or a
      // BytecodeGenerator stack overflow. Re-compile with KEEP_EXCEPTION and
      // check that any failure is a stack overflow (RangeError).
      i::HandleScope loop_scope(i_isolate);
      i::Handle<i::SharedFunctionInfo> shared_handle(shared, i_isolate);
      i::IsCompiledScope inner_scope;
      if (!i::Compiler::Compile(i_isolate, shared_handle,
                                i::Compiler::KEEP_EXCEPTION, &inner_scope)) {
        CHECK(i_isolate->has_exception());
        i::Tagged<i::Object> exception = i_isolate->exception();
        CHECK(i::IsJSObject(exception));
        CHECK(i::Cast<i::JSObject>(exception)->map()->GetConstructor() ==
              *i_isolate->range_error_function());
        try_catch.Reset();
      }
    }
  }
  try_catch.Reset();
}

}  // namespace

V8_SYMBOL_USED extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  i::v8_flags.stress_lazy.Set(true);
  i::v8_flags.stress_lazy_source_positions.Set(true);
  v8_fuzzer::FuzzerSupport::InitializeFuzzerSupport(argc, argv);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > kMaxInputSize) return 0;

  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::MaybeHandle<i::String> maybe_source =
      i_isolate->factory()->NewStringFromOneByte(
          v8::base::VectorOf(data, size));
  if (maybe_source.is_null()) return 0;
  i::Handle<i::String> source = maybe_source.ToHandleChecked();

  RunDifferentialOracleForMode(isolate, source, /*is_module=*/false,
                               i::LanguageMode::kSloppy);
  RunDifferentialOracleForMode(isolate, source, /*is_module=*/false,
                               i::LanguageMode::kStrict);
  RunDifferentialOracleForMode(isolate, source, /*is_module=*/true,
                               i::LanguageMode::kStrict);

  static size_t iterations = 0;
  if ((++iterations & 127) == 0) {
    isolate->RequestGarbageCollectionForTesting(
        v8::Isolate::kFullGarbageCollection);
  }
  return 0;
}
