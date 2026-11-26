// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-function.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-message.h"
#include "include/v8-platform.h"
#include "include/v8-primitive.h"
#include "include/v8-profiler.h"
#include "include/v8-script.h"
#include "src/api/api-inl.h"
#include "src/base/platform/semaphore.h"
#include "src/base/synchronization-point-support.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/compiler.h"
#include "src/debug/debug-interface.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-verifier.h"
#include "src/heap/local-heap-inl.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/scope-info-inl.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/objects/string-inl.h"
#include "src/snapshot/code-serializer.h"
#include "src/utils/utils.h"
#include "test/common/flag-utils.h"
#include "test/unittests/heap/heap-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
#include "src/diagnostics/etw-isolate-load-script-data-win.h"
#endif  // defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)

namespace v8 {

class DeserializeTest : public TestWithPlatform {
 public:
  class IsolateAndContextScope {
   public:
    explicit IsolateAndContextScope(DeserializeTest* test)
        : test_(test),
          isolate_wrapper_(kNoCounters),
          isolate_scope_(isolate_wrapper_.isolate()),
          handle_scope_(isolate_wrapper_.isolate()) {
      CHECK_NULL(test->isolate_);
      CHECK(test->context_.IsEmpty());
      test->isolate_ = isolate_wrapper_.isolate();
      Local<Context> context = Context::New(test->isolate_);
      test->context_.Reset(test->isolate_, context);
      context->Enter();
    }
    ~IsolateAndContextScope() {
      test_->context_.Get(test_->isolate_)->Exit();
      test_->isolate_ = nullptr;
      test_->context_.Reset();
    }

   private:
    DeserializeTest* test_;
    v8::IsolateWrapper isolate_wrapper_;
    v8::Isolate::Scope isolate_scope_;
    v8::HandleScope handle_scope_;
  };

  Local<String> NewString(const char* val) {
    return String::NewFromUtf8(isolate(), val).ToLocalChecked();
  }

  Local<Value> RunGlobalFunc(const char* name) {
    Local<Value> func_val =
        context()->Global()->Get(context(), NewString(name)).ToLocalChecked();
    CHECK(func_val->IsFunction());
    Local<Function> func = Local<Function>::Cast(func_val);
    return func->Call(context(), Undefined(isolate()), 0, nullptr)
        .ToLocalChecked();
  }

  Isolate* isolate() { return isolate_; }
  v8::Local<v8::Context> context() {
    DCHECK(!context_.IsEmpty());
    return context_.Get(isolate_);
  }

#ifdef V8_ENABLE_TEST_ONLY_SYNC_POINTS
  i::DirectHandle<i::JSFunction> GetGlobalJSFunction(const char* name) {
    return i::Cast<i::JSFunction>(
        Utils::OpenDirectHandle(*context()
                                     ->Global()
                                     ->Get(context(), NewString(name))
                                     .ToLocalChecked()));
  }

  // Deserializes a code cache for a script defining `foo` on a background
  // thread and runs `action` on the main thread twice while that script is not
  // yet finalized: once paused at the first allocation of the trusted section,
  // and once after the background thread is done. A finalized script defining
  // `bar` exists throughout, so `action` also has something to enumerate.
  template <typename Action>
  void RunDuringOffThreadDeserialize(const Action& action);
#endif  // V8_ENABLE_TEST_ONLY_SYNC_POINTS

 private:
  Isolate* isolate_ = nullptr;
  v8::Global<v8::Context> context_;
};

// Check that deserialization works.
TEST_F(DeserializeTest, Deserialize) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;

  {
    IsolateAndContextScope scope(this);

    Local<String> source_code = NewString("function foo() { return 42; }");
    Local<Script> script =
        Script::Compile(context(), source_code).ToLocalChecked();

    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), Integer::New(isolate(), 42));

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);

    Local<String> source_code = NewString("function foo() { return 42; }");
    ScriptCompiler::Source source(source_code, cached_data.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();

    CHECK(!source.GetCachedData()->rejected);
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), v8::Integer::New(isolate(), 42));
  }
}

// Check that deserialization with a different script rejects the cache but
// still works via standard compilation.
TEST_F(DeserializeTest, DeserializeRejectsDifferentSource) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;

  {
    IsolateAndContextScope scope(this);

    Local<String> source_code = NewString("function foo() { return 42; }");
    Local<Script> script =
        Script::Compile(context(), source_code).ToLocalChecked();

    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), Integer::New(isolate(), 42));

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);

    // The source hash is based on the source length, so have to make sure that
    // this is different here.
    Local<String> source_code = NewString("function bar() { return 142; }");
    ScriptCompiler::Source source(source_code, cached_data.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();

    CHECK(source.GetCachedData()->rejected);
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("bar"), v8::Integer::New(isolate(), 142));
  }
}

class DeserializeThread : public base::Thread {
 public:
  explicit DeserializeThread(ScriptCompiler::ConsumeCodeCacheTask* task)
      : Thread(base::Thread::Options("DeserializeThread")), task_(task) {}

  void Run() override { task_->Run(); }

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> TakeTask() {
    return std::move(task_);
  }

 private:
  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task_;
};

// Check that off-thread deserialization works.
TEST_F(DeserializeTest, OffThreadDeserialize) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;

  {
    IsolateAndContextScope scope(this);

    Local<String> source_code = NewString("function foo() { return 42; }");
    Local<Script> script =
        Script::Compile(context(), source_code).ToLocalChecked();

    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), Integer::New(isolate(), 42));

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);

    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data->data, cached_data->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();

    Local<String> source_code = NewString("function foo() { return 42; }");
    ScriptCompiler::Source source(source_code, cached_data.release(),
                                  deserialize_thread.TakeTask().release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();

    CHECK(!source.GetCachedData()->rejected);
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), v8::Integer::New(isolate(), 42));
  }
}

#ifdef V8_ENABLE_TEST_ONLY_SYNC_POINTS
template <typename Action>
void DeserializeTest::RunDuringOffThreadDeserialize(const Action& action) {
  if (!i::v8_flags.concurrent_cache_deserialization) GTEST_SKIP();
  // Without the sandbox there is no separate trusted section, so the
  // deserializer does not allocate after the sync point and never pauses.
  if (!V8_ENABLE_SANDBOX_BOOL) GTEST_SKIP();

  static constexpr const char* kSyncPoint =
      "OffThreadDeserializeInTrustedSection";
  static constexpr const char* kSource =
      "function foo(t) { if (t) throw new Error(); return 42; } foo(false);";

  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  {
    IsolateAndContextScope scope(this);
    Local<Script> script =
        Script::Compile(context(), NewString(kSource)).ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
    CHECK_NOT_NULL(cached_data);
  }

  IsolateAndContextScope scope(this);
  CHECK(!Script::Compile(context(),
                         NewString("function bar() { return 1; } bar();"))
             .ToLocalChecked()
             ->Run(context())
             .IsEmpty());

  base::SynchronizationPointSupport::Get()->RequestBlockAtNextSafepoint(
      kSyncPoint, base::TimeDelta::FromSeconds(10));

  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  // Park while waiting, so that the background thread is never stuck waiting
  // for the main thread to reach a safepoint.
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  i::LocalHeap* local_heap = i_isolate->main_thread_local_heap();
  local_heap->ExecuteWhileParked([&]() {
    CHECK(base::SynchronizationPointSupport::Get()->WaitUntilBlocked(
        kSyncPoint, base::TimeDelta::FromSeconds(10)));
  });

  action(isolate());

  CHECK(base::SynchronizationPointSupport::Get()->Resume(kSyncPoint));
  local_heap->ExecuteWhileParked([&]() { deserialize_thread.Join(); });

  // Run `action` again now that the off-thread SFIs have bytecode, while
  // their Script is still unfinalized.
  action(isolate());

  ScriptCompiler::Source source(NewString(kSource), cached_data.release(),
                                deserialize_thread.TakeTask().release());
  Local<Script> script =
      ScriptCompiler::Compile(context(), &source,
                              ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();
  CHECK(!source.GetCachedData()->rejected);
  CHECK(!script->Run(context()).IsEmpty());
  CHECK_EQ(RunGlobalFunc("foo"), v8::Integer::New(isolate(), 42));

  // Check that `action` left the then-unfinalized script untouched: no line
  // ends and no source positions were computed for it early.
  i::DirectHandle<i::JSFunction> foo = GetGlobalJSFunction("foo");
  if (!i_isolate->NeedsSourcePositions() &&
      !i_isolate->IsLoggingCodeCreation()) {
    CHECK(!i::Cast<i::Script>(foo->shared()->script())->has_line_ends());
    if (i::v8_flags.enable_lazy_source_positions &&
        !i::v8_flags.stress_lazy_source_positions) {
      CHECK(!foo->shared()->AreSourcePositionsAvailable(i_isolate));
    }
  }

  // Verify lazy source position collection still succeeds once finalized; 25
  // is the column of `throw` in kSource.
  v8::TryCatch try_catch(isolate());
  Local<Script> throw_script =
      Script::Compile(context(), NewString("foo(true);")).ToLocalChecked();
  CHECK(throw_script->Run(context()).IsEmpty());
  CHECK(try_catch.HasCaught());
  CHECK_EQ(try_catch.Message()->GetStartColumn(context()).FromJust(), 25);
}

TEST_F(DeserializeTest, OffThreadDeserializeWithCpuProfiler) {
  RunDuringOffThreadDeserialize([this](Isolate* isolate) {
    v8::CpuProfiler* profiler =
        v8::CpuProfiler::New(isolate, v8::kDebugNaming, v8::kEagerLogging);
    // Creating an eager-logging profiler collects source positions for
    // finalized scripts.
    CHECK(GetGlobalJSFunction("bar")->shared()->AreSourcePositionsAvailable(
        reinterpret_cast<i::Isolate*>(isolate)));
    profiler->Dispose();
  });
}

TEST_F(DeserializeTest, OffThreadDeserializeWithJitCodeEventHandler) {
  RunDuringOffThreadDeserialize([](Isolate* isolate) {
    isolate->SetJitCodeEventHandler(v8::kJitCodeEventEnumExisting,
                                    [](const v8::JitCodeEvent*) {});
    isolate->SetJitCodeEventHandler(v8::kJitCodeEventDefault, nullptr);
  });
}

#if defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
TEST_F(DeserializeTest, OffThreadDeserializeWithEtwEnableLog) {
  i::FlagScope<bool> enable_etw(&i::v8_flags.enable_etw_stack_walking, true);
  RunDuringOffThreadDeserialize([](Isolate* isolate) {
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    // Interpreted-frame tracing copies the interpreter entry trampoline per
    // function, which allocates Code and is incompatible with --jitless.
    if (!i::v8_flags.jitless) {
      i_isolate->set_etw_trace_interpreted_frames();
    }
    i::ETWJITInterface::IsolateLoadScriptData::EnableLog(
        i_isolate, 0,
        std::weak_ptr<i::ETWJITInterface::EtwIsolateCaptureStateMonitor>(),
        v8::kJitCodeEventEnumExisting);
    i::ETWJITInterface::IsolateLoadScriptData::DisableLog(i_isolate, 0);
  });
}
#endif  // defined(V8_OS_WIN) && defined(V8_ENABLE_ETW_STACK_WALKING)
#endif  // V8_ENABLE_TEST_ONLY_SYNC_POINTS

namespace {

std::string LongMagicCommentValue() {
  return std::format("data:application/json;base64,{:A>2000}", "");
}

std::string SourceWithLongMagicComments(
    std::string_view body = "function foo() { return 42; }") {
  return std::format(
      "{0}\n//# sourceURL={1}\n//# sourceMappingURL={1}\n//# debugId={1}\n",
      body, LongMagicCommentValue());
}

// Compiled scripts have the source map URL magic comment value as a slice of
// the source (the source URL and debug ID are sequential copies). The code
// cache contains it as a sequential string, since the slice could not be
// deserialized off-thread, where the source is not available.
void CheckLongMagicCommentValues(Isolate* isolate, Local<Script> script,
                                 bool expect_slices) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  i::DirectHandle<i::Script> i_script(
      i::Cast<i::Script>(
          Utils::OpenDirectHandle(*script->GetUnboundScript())->script()),
      i_isolate);
  std::string value = LongMagicCommentValue();
  // The source URL and debug ID are always sequential strings.
  i::Tagged<i::Object> copied_fields[] = {i_script->source_url(),
                                          i_script->debug_id()};
  for (i::Tagged<i::Object> field : copied_fields) {
    CHECK(i::IsSeqString(field));
    CHECK(i::Cast<i::String>(field)->IsEqualTo(
        base::OneByteVector(value.data(), value.size())));
  }
  i::Tagged<i::Object> source_mapping_url = i_script->source_mapping_url();
  CHECK_EQ(expect_slices, i::IsSlicedString(source_mapping_url));
  if (expect_slices) {
    CHECK_EQ(i::Cast<i::SlicedString>(source_mapping_url)->parent(),
             i_script->source());
  }
  CHECK(i::Cast<i::String>(source_mapping_url)
            ->IsEqualTo(base::OneByteVector(value.data(), value.size())));
}

}  // namespace

TEST_F(DeserializeTest, DeserializeLongMagicComments) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  std::string source_text = SourceWithLongMagicComments();

  {
    IsolateAndContextScope scope(this);
    Local<Script> script =
        Script::Compile(context(), NewString(source_text.c_str()))
            .ToLocalChecked();
    CheckLongMagicCommentValues(isolate(), script, true);
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);
    ScriptCompiler::Source source(NewString(source_text.c_str()),
                                  cached_data.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!source.GetCachedData()->rejected);
    CheckLongMagicCommentValues(isolate(), script, false);
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), Integer::New(isolate(), 42));
  }
}

TEST_F(DeserializeTest, OffThreadDeserializeLongMagicComments) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  std::string source_text = SourceWithLongMagicComments();

  {
    IsolateAndContextScope scope(this);
    Local<Script> script =
        Script::Compile(context(), NewString(source_text.c_str()))
            .ToLocalChecked();
    CheckLongMagicCommentValues(isolate(), script, true);
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);
    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data->data, cached_data->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();

    ScriptCompiler::Source source(NewString(source_text.c_str()),
                                  cached_data.release(),
                                  deserialize_thread.TakeTask().release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!source.GetCachedData()->rejected);
    CheckLongMagicCommentValues(isolate(), script, false);
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), Integer::New(isolate(), 42));
  }
}

TEST_F(DeserializeTest, DeserializeLongMagicCommentsFromTwoByteSource) {
  // The source is two-byte, but the magic comment values are one-byte. The
  // values are two-byte slices of the source, but should be cached (and
  // deserialized) as one-byte strings.
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  std::string source_text = SourceWithLongMagicComments(
      "var s = '\xD7\x90';\nfunction foo() { return 42; }");

  {
    IsolateAndContextScope scope(this);
    Local<Script> script =
        Script::Compile(context(), NewString(source_text.c_str()))
            .ToLocalChecked();
    CheckLongMagicCommentValues(isolate(), script, true);
    i::DirectHandle<i::Script> i_script(
        i::Cast<i::Script>(
            Utils::OpenDirectHandle(*script->GetUnboundScript())->script()),
        reinterpret_cast<i::Isolate*>(isolate()));
    CHECK(i::Cast<i::String>(i_script->source())->IsTwoByteRepresentation());
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  for (bool off_thread : {false, true}) {
    IsolateAndContextScope scope(this);
    auto data = std::make_unique<ScriptCompiler::CachedData>(
        cached_data->data, cached_data->length,
        ScriptCompiler::CachedData::BufferNotOwned);
    std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task;
    if (off_thread) {
      DeserializeThread deserialize_thread(
          ScriptCompiler::StartConsumingCodeCache(
              isolate(), std::make_unique<ScriptCompiler::CachedData>(
                             cached_data->data, cached_data->length,
                             ScriptCompiler::CachedData::BufferNotOwned)));
      CHECK(deserialize_thread.Start());
      deserialize_thread.Join();
      task = deserialize_thread.TakeTask();
    }
    ScriptCompiler::Source source(NewString(source_text.c_str()),
                                  data.release(), task.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!source.GetCachedData()->rejected);
    CheckLongMagicCommentValues(isolate(), script, false);
    i::DirectHandle<i::Script> i_script(
        i::Cast<i::Script>(
            Utils::OpenDirectHandle(*script->GetUnboundScript())->script()),
        reinterpret_cast<i::Isolate*>(isolate()));
    i::Tagged<i::Object> fields[] = {i_script->source_url(),
                                     i_script->source_mapping_url(),
                                     i_script->debug_id()};
    for (i::Tagged<i::Object> field : fields) {
      CHECK(i::IsSeqOneByteString(field));
    }
  }
}

TEST_F(DeserializeTest, DeserializeKeepsApiSourceMapURL) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  std::string source_text = SourceWithLongMagicComments();
  auto compile = [&](ScriptCompiler::CachedData* data) {
    ScriptOrigin origin(NewString("origin.js"), 0, 0, false, -1,
                        NewString("api.js.map"));
    ScriptCompiler::Source source(NewString(source_text.c_str()), origin, data);
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                data ? ScriptCompiler::kConsumeCodeCache
                                     : ScriptCompiler::kNoCompileOptions)
            .ToLocalChecked();
    CHECK(!data || !source.GetCachedData()->rejected);
    String::Utf8Value url(isolate(),
                          script->GetUnboundScript()->GetSourceMappingURL());
    CHECK_EQ(0, strcmp("api.js.map", *url));
    return script;
  };

  {
    IsolateAndContextScope scope(this);
    Local<Script> script = compile(nullptr);
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);
    compile(cached_data.release());
  }
}

// Check that off-thread deserialization works.
TEST_F(DeserializeTest, OffThreadDeserializeRejectsDifferentSource) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;

  {
    IsolateAndContextScope scope(this);

    Local<String> source_code = NewString("function foo() { return 42; }");
    Local<Script> script =
        Script::Compile(context(), source_code).ToLocalChecked();

    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), Integer::New(isolate(), 42));

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);

    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data->data, cached_data->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();

    Local<String> source_code = NewString("function bar() { return 142; }");
    ScriptCompiler::Source source(source_code, cached_data.release(),
                                  deserialize_thread.TakeTask().release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();

    CHECK(source.GetCachedData()->rejected);
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("bar"), v8::Integer::New(isolate(), 142));
  }
}

class DeserializeStarterThread : public base::Thread {
 public:
  explicit DeserializeStarterThread(Isolate* isolate,
                                    v8::ScriptCompiler::CachedData* cached_data)
      : Thread(base::Thread::Options("DeserializeStarterThread")),
        isolate_(isolate),
        cached_data_(cached_data) {}

  void Run() override {
    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCacheOnBackground(
            isolate_, std::make_unique<ScriptCompiler::CachedData>(
                          cached_data_->data, cached_data_->length,
                          ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();
    task_ = deserialize_thread.TakeTask();
  }

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> TakeTask() {
    return std::move(task_);
  }

 private:
  Isolate* isolate_;
  v8::ScriptCompiler::CachedData* cached_data_;
  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task_;
};

// Check that off-thread deserialization started from a background thread works.
TEST_F(DeserializeTest, OffThreadDeserializeStartedFromBackgroundThread) {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;

  {
    IsolateAndContextScope scope(this);

    Local<String> source_code = NewString("function foo() { return 42; }");
    Local<Script> script =
        Script::Compile(context(), source_code).ToLocalChecked();

    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), Integer::New(isolate(), 42));

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  {
    IsolateAndContextScope scope(this);

    DeserializeStarterThread deserialize_starter_thread(isolate(),
                                                        cached_data.get());
    CHECK(deserialize_starter_thread.Start());
    {
      // Check that code execution works wille the DeserializeStarterThread
      // staring a ConsumeCodeCacheTask.
      Local<String> other_source_code =
          NewString("function bar() { return 21; }");
      Local<Script> other_script =
          Script::Compile(context(), other_source_code).ToLocalChecked();
      CHECK(!other_script->Run(context()).IsEmpty());
      CHECK_EQ(RunGlobalFunc("bar"), Integer::New(isolate(), 21));
    }
    deserialize_starter_thread.Join();

    Local<String> source_code = NewString("function foo() { return 42; }");
    ScriptCompiler::Source source(
        source_code, cached_data.release(),
        deserialize_starter_thread.TakeTask().release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();

    CHECK(!source.GetCachedData()->rejected);
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("foo"), v8::Integer::New(isolate(), 42));
  }
}

// This class is a dynamic wrapper for v8::ScriptOrigin, using v8::Global for
// its fields, instead of v8::Local. Therefore, it can be used in tests such as
// the ones below, which explicitly disable conservative stack scanning. In such
// tests, using v8::ScriptOrigin in configurations where v8::Local is a direct
// pointer (i.e., with v8_enable_direct_handle=true) would have been incorrect.
// Without CSS, the GC could miss an object referenced by a v8::Local (if it was
// not otherwise retained). Or, even if the object was retained, the GC could
// move it without updating the v8::Local. In both cases, the v8::Local would
// contain an invalid direct pointer after GC.
class PersistentScriptOrigin {
 public:
  PersistentScriptOrigin(Isolate* isolate, Local<Value> resource_name)
      : isolate_(isolate), resource_name_(isolate, resource_name) {}
  ~PersistentScriptOrigin() { resource_name_.Reset(); }

  ScriptOrigin AsScriptOrigin() const {
    return ScriptOrigin(resource_name_.Get(isolate_));
  }

 private:
  Isolate* isolate_;
  Global<Value> resource_name_;
};

class MergeDeserializedCodeTest : public DeserializeTest {
 protected:
  // The source code used in these tests.
  static constexpr char kSourceCode[] = R"(
    // Looks like an IIFE but isn't, to get eagerly parsed:
    { let captured = 10;
      var eager = (function () {
        // Actual IIFE, also eagerly parsed:
        return (function iife() {
          return captured, 42;
        })();
      });
      // Lazily parsed:
      var lazy = function () { return eager(); };
    }
  )";

  // Objects from the Script's object graph whose lifetimes and connectedness
  // are useful to track.
  enum ScriptObject {
    kScript,
    kToplevelSfi,
    kToplevelFunctionData,
    kToplevelFeedbackMetadata,
    kEagerSfi,
    kEagerFunctionData,
    kEagerFeedbackMetadata,
    kIifeSfi,
    kIifeFunctionData,
    kIifeFeedbackMetadata,
    kLazySfi,
    kScriptObjectsCount
  };
  enum ScriptObjectFlag {
    kNone,

    kScriptFlag = 1 << kScript,
    kToplevelSfiFlag = 1 << kToplevelSfi,
    kToplevelFunctionDataFlag = 1 << kToplevelFunctionData,
    kToplevelFeedbackMetadataFlag = 1 << kToplevelFeedbackMetadata,
    kEagerSfiFlag = 1 << kEagerSfi,
    kEagerFunctionDataFlag = 1 << kEagerFunctionData,
    kEagerFeedbackMetadataFlag = 1 << kEagerFeedbackMetadata,
    kIifeSfiFlag = 1 << kIifeSfi,
    kIifeFunctionDataFlag = 1 << kIifeFunctionData,
    kIifeFeedbackMetadataFlag = 1 << kIifeFeedbackMetadata,
    kLazySfiFlag = 1 << kLazySfi,

    kAllScriptObjects = (1 << kScriptObjectsCount) - 1,
    kAllCompiledSfis = kToplevelSfiFlag | kEagerSfiFlag | kIifeSfiFlag,
    kAllSfis = kAllCompiledSfis | kLazySfiFlag,
    kEagerAndLazy = kLazySfiFlag | kEagerSfiFlag,
    kToplevelEagerAndLazy = kToplevelSfiFlag | kEagerAndLazy,
    kToplevelAndEager = kToplevelSfiFlag | kEagerSfiFlag,
  };

  template <typename T>
  static i::Tagged<i::SharedFunctionInfo> GetSharedFunctionInfo(
      Local<T> function_or_script) {
    i::DirectHandle<i::JSFunction> i_function =
        i::Cast<i::JSFunction>(Utils::OpenDirectHandle(*function_or_script));
    return i_function->shared();
  }

  static i::Tagged<i::Object> ExtractSharedFunctionInfoData(
      i::Tagged<i::SharedFunctionInfo> sfi, i::Isolate* i_isolate) {
    auto data = sfi->GetTrustedData(i_isolate);
    // BytecodeArrays live in trusted space and so cannot be referenced through
    // tagged/compressed pointers from e.g. a FixedArray. Instead, we need to
    // use their in-sandbox wrapper object for that purpose.
    if (i::Tagged<i::BytecodeArray> bytes; TryCast(data, &bytes)) {
      return bytes->wrapper();
    }
    return i::Cast<i::Object>(data);
  }

  void ValidateStandaloneGraphAndPopulateArray(
      i::Tagged<i::SharedFunctionInfo> toplevel_sfi,
      i::Tagged<i::WeakFixedArray> array, i::Isolate* i_isolate,
      bool lazy_should_be_compiled = false,
      bool eager_should_be_compiled = true) {
    i::DisallowGarbageCollection no_gc;
    CHECK(toplevel_sfi->is_compiled());
    array->set(kToplevelSfi, MakeWeak(toplevel_sfi));
    array->set(
        kToplevelFunctionData,
        MakeWeakOrSmi(ExtractSharedFunctionInfoData(toplevel_sfi, i_isolate)));
    array->set(kToplevelFeedbackMetadata,
               MakeWeak(toplevel_sfi->feedback_metadata()));
    i::Tagged<i::Script> script = i::Cast<i::Script>(toplevel_sfi->script());
    array->set(kScript, MakeWeak(script));
    i::Tagged<i::WeakFixedArray> sfis = script->infos();
    CHECK_EQ(sfis->length().value(), 4);
    CHECK_EQ(sfis->get(0), MakeWeak(toplevel_sfi));
    i::Tagged<i::SharedFunctionInfo> eager =
        i::Cast<i::SharedFunctionInfo>(sfis->get(1).GetHeapObjectAssumeWeak());
    CHECK_EQ(eager->is_compiled(), eager_should_be_compiled);
    array->set(kEagerSfi, MakeWeak(eager));
    if (eager_should_be_compiled) {
      array->set(
          kEagerFunctionData,
          MakeWeakOrSmi(ExtractSharedFunctionInfoData(eager, i_isolate)));
      array->set(kEagerFeedbackMetadata, MakeWeak(eager->feedback_metadata()));
      i::Tagged<i::SharedFunctionInfo> iife = i::Cast<i::SharedFunctionInfo>(
          sfis->get(2).GetHeapObjectAssumeWeak());
      CHECK(iife->is_compiled());
      array->set(kIifeSfi, MakeWeak(iife));
      array->set(kIifeFunctionData,
                 MakeWeakOrSmi(ExtractSharedFunctionInfoData(iife, i_isolate)));
      array->set(kIifeFeedbackMetadata, MakeWeak(iife->feedback_metadata()));
    }
    i::Tagged<i::SharedFunctionInfo> lazy =
        i::Cast<i::SharedFunctionInfo>(sfis->get(3).GetHeapObjectAssumeWeak());
    CHECK_EQ(lazy->is_compiled(), lazy_should_be_compiled);
    array->set(kLazySfi, MakeWeak(lazy));
  }

  void AgeBytecodeAndGC(ScriptObjectFlag sfis_to_age,
                        i::IndirectHandle<i::WeakFixedArray> original_objects,
                        i::Isolate* i_isolate) {
    for (int index = 0; index < kScriptObjectsCount; ++index) {
      if ((sfis_to_age & (1 << index)) == (1 << index)) {
        i::Tagged<i::SharedFunctionInfo> sfi = i::Cast<i::SharedFunctionInfo>(
            original_objects->get(index).GetHeapObjectAssumeWeak());
        i::SharedFunctionInfo::EnsureOldForTesting(sfi);
      }
    }

    InvokeMajorGC(i_isolate);

    // A second round of GC is necessary in case incremental marking had already
    // started before the bytecode was aged.
    InvokeMajorGC(i_isolate);
  }

  class MergeThread : public base::Thread {
   public:
    explicit MergeThread(ScriptCompiler::ConsumeCodeCacheTask* task)
        : Thread(base::Thread::Options("MergeThread")), task_(task) {}

    void Run() override { task_->MergeWithExistingScript(); }

   private:
    ScriptCompiler::ConsumeCodeCacheTask* task_;
  };

  void RetainObjects(ScriptObjectFlag to_retain,
                     i::Tagged<i::WeakFixedArray> original_objects,
                     i::Tagged<i::FixedArray> retained_original_objects,
                     i::Isolate* i_isolate) {
    for (int index = 0; index < kScriptObjectsCount; ++index) {
      if ((to_retain & (1 << index)) == (1 << index)) {
        i::Tagged<i::MaybeObject> maybe = original_objects->get(index);
        if (i::Tagged<i::HeapObject> heap_object;
            maybe.GetHeapObjectIfWeak(&heap_object)) {
          retained_original_objects->set(index, heap_object);
          continue;
        }
      }
      retained_original_objects->set(
          index, i::ReadOnlyRoots(i_isolate).undefined_value());
    }
  }

  void TestOffThreadMerge(ScriptObjectFlag retained_before_background_merge,
                          ScriptObjectFlag aged_before_background_merge,
                          bool run_code_after_background_merge,
                          ScriptObjectFlag retained_after_background_merge,
                          ScriptObjectFlag aged_after_background_merge,
                          bool lazy_should_be_compiled = false,
                          bool eager_should_be_compiled = true) {
    i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
        true);
    std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    PersistentScriptOrigin default_origin(isolate(), NewString(""));

    i::IndirectHandle<i::WeakFixedArray> original_objects =
        i_isolate->factory()->NewWeakFixedArray(kScriptObjectsCount);
    i::IndirectHandle<i::FixedArray> retained_original_objects =
        i_isolate->factory()->NewFixedArray(kScriptObjectsCount);
    i::IndirectHandle<i::WeakFixedArray> new_objects =
        i_isolate->factory()->NewWeakFixedArray(kScriptObjectsCount);
    Global<Script> original_script;

    // Compile the script for the first time, to both populate the Isolate
    // compilation cache and produce code cache data.
    {
      v8::HandleScope handle_scope(isolate());
      ScriptOrigin origin = default_origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSourceCode), &origin)
              .ToLocalChecked();

      ValidateStandaloneGraphAndPopulateArray(GetSharedFunctionInfo(script),
                                              *original_objects, i_isolate);

      RetainObjects(retained_before_background_merge, *original_objects,
                    *retained_original_objects, i_isolate);

      cached_data.reset(
          ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));

      if (run_code_after_background_merge) {
        // We must retain the v8::Script (a JSFunction) so we can run it later.
        original_script.Reset(isolate(), script);
        // It doesn't make any sense to configure a test case which says it
        // doesn't want to retain the toplevel SFI but does want to run the
        // script later.
        CHECK(retained_before_background_merge & kToplevelSfiFlag);
      }
    }

    {
      // We need to invoke GC without stack, otherwise some objects may survive.
      i::DisableConservativeStackScanningScopeForTesting no_css_scope(
          i_isolate->heap());
      AgeBytecodeAndGC(aged_before_background_merge, original_objects,
                       i_isolate);
    }

    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data->data, cached_data->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();

    std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
        deserialize_thread.TakeTask();

    task->SourceTextAvailable(isolate(), NewString(kSourceCode),
                              default_origin.AsScriptOrigin());

    // If the top-level SFI was retained and not flushed, then no merge is
    // necessary because the results from the deserialization will be discarded.
    // If nothing at all was retained, then no merge is necessary because the
    // original Script is no longer in the compilation cache. Otherwise, a merge
    // is necessary.
    bool merge_expected =
        (retained_before_background_merge != kNone) &&
        (!(retained_before_background_merge & kToplevelSfiFlag) ||
         (aged_before_background_merge & kToplevelSfiFlag));
    CHECK_EQ(merge_expected, task->ShouldMergeWithExistingScript());

    if (merge_expected) {
      MergeThread merge_thread(task.get());
      CHECK(merge_thread.Start());
      merge_thread.Join();
    }

    if (run_code_after_background_merge) {
      Local<Script> script = original_script.Get(isolate());
      CHECK(!script->Run(context()).IsEmpty());
      CHECK_EQ(RunGlobalFunc("lazy"), v8::Integer::New(isolate(), 42));
      ValidateStandaloneGraphAndPopulateArray(GetSharedFunctionInfo(script),
                                              *original_objects, i_isolate,
                                              true /*lazy_should_be_compiled*/);
    }

    RetainObjects(retained_after_background_merge, *original_objects,
                  *retained_original_objects, i_isolate);

    {
      // We need to invoke GC without stack, otherwise some objects may survive.
      i::DisableConservativeStackScanningScopeForTesting no_css_scope(
          i_isolate->heap());
      AgeBytecodeAndGC(aged_after_background_merge, original_objects,
                       i_isolate);
    }

    Global<Script> new_script;
    {
      ScriptCompiler::Source source(NewString(kSourceCode),
                                    default_origin.AsScriptOrigin(),
                                    cached_data.release(), task.release());
      Local<Script> script =
          ScriptCompiler::Compile(context(), &source,
                                  ScriptCompiler::kConsumeCodeCache)
              .ToLocalChecked();
      new_script.Reset(isolate(), script);

      CHECK(!source.GetCachedData()->rejected);
      ValidateStandaloneGraphAndPopulateArray(
          GetSharedFunctionInfo(script), *new_objects, i_isolate,
          lazy_should_be_compiled, eager_should_be_compiled);
    }

    // At this point, the original_objects array might still have pointers to
    // some old discarded content, such as UncompiledData from flushed
    // functions. GC again to clear it all out.
    {
      // We need to invoke GC without stack, otherwise some objects may survive.
      i::DisableConservativeStackScanningScopeForTesting no_css_scope(
          i_isolate->heap());
      InvokeMajorGC(i_isolate);
    }

    // All tracked objects from the original Script should have been reused if
    // they're still alive.
    for (int index = 0; index < kScriptObjectsCount; ++index) {
      if (original_objects->get(index).IsWeak() &&
          new_objects->get(index).IsWeak()) {
        CHECK_EQ(original_objects->get(index), new_objects->get(index));
      }
    }

    CHECK(!new_script.Get(isolate())->Run(context()).IsEmpty());
    CHECK_EQ(RunGlobalFunc("lazy"), v8::Integer::New(isolate(), 42));
  }

  void FlushWithMajorGC(i::Isolate* i_isolate) {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  Local<Value> Eval(const char* src) {
    return Script::Compile(context(), NewString(src))
        .ToLocalChecked()
        ->Run(context())
        .ToLocalChecked();
  }

  std::unique_ptr<ScriptCompiler::CachedData> MakeCodeCache(
      const char* src, const char* url, const char* warmup = nullptr) {
    IsolateAndContextScope scope(this);
    v8::HandleScope hs(isolate());
    ScriptOrigin origin(NewString(url));
    Local<Script> script =
        Script::Compile(context(), NewString(src), &origin).ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    if (warmup) Eval(warmup);
    return std::unique_ptr<ScriptCompiler::CachedData>(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask>
  StartDeserializeBeforeMerge(const char* src, const ScriptOrigin& origin,
                              const ScriptCompiler::CachedData* cached_data) {
    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data->data, cached_data->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();
    auto task = deserialize_thread.TakeTask();
    task->SourceTextAvailable(isolate(), NewString(src), origin);
    CHECK(task->ShouldMergeWithExistingScript());
    return task;
  }

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> StartMergeTask(
      const char* src, const ScriptOrigin& origin,
      const ScriptCompiler::CachedData* cached_data) {
    auto task = StartDeserializeBeforeMerge(src, origin, cached_data);
    MergeThread merge_thread(task.get());
    CHECK(merge_thread.Start());
    merge_thread.Join();
    return task;
  }
};

TEST_F(MergeDeserializedCodeTest, NoMergeWhenAlreadyCompiled) {
  // Retain everything; age nothing.
  TestOffThreadMerge(kAllScriptObjects,  // retained_before_background_merge
                     kNone,              // aged_before_background_merge
                     false,              // run_code_after_background_merge
                     kAllScriptObjects,  // retained_after_background_merge
                     kNone);             // aged_after_background_merge
}

TEST_F(MergeDeserializedCodeTest, NoMergeWhenOriginalWasDiscarded) {
  // Retain nothing.
  TestOffThreadMerge(kNone,   // retained_before_background_merge
                     kNone,   // aged_before_background_merge
                     false,   // run_code_after_background_merge
                     kNone,   // retained_after_background_merge
                     kNone);  // aged_after_background_merge
}

TEST_F(MergeDeserializedCodeTest, NoMergeWhenOriginalWasDiscardedLate) {
  // The original top-level SFI is retained by the background merge task even
  // though other retainers are discarded.
  TestOffThreadMerge(kAllScriptObjects,  // retained_before_background_merge
                     kNone,              // aged_before_background_merge
                     false,              // run_code_after_background_merge
                     kNone,              // retained_after_background_merge
                     kNone);             // aged_after_background_merge
}

TEST_F(MergeDeserializedCodeTest, MergeIntoFlushedSFIs) {
  // Retain all SFIs but age them.
  TestOffThreadMerge(kAllSfis,          // retained_before_background_merge
                     kAllCompiledSfis,  // aged_before_background_merge
                     false,             // run_code_after_background_merge
                     kAllSfis,          // retained_after_background_merge
                     kNone);            // aged_after_background_merge
}

TEST_F(MergeDeserializedCodeTest, MergeBasic) {
  // Retain the eager and lazy functions; discard the top-level SFI.
  // This is a common scenario which requires a merge.
  TestOffThreadMerge(kEagerAndLazy,     // retained_before_background_merge
                     kToplevelSfiFlag,  // aged_before_background_merge
                     false,             // run_code_after_background_merge
                     kNone,             // retained_after_background_merge
                     kNone);            // aged_after_background_merge
}

TEST_F(MergeDeserializedCodeTest, MergeBasicWithFlushing) {
  // Retain the eager and lazy functions; discard the top-level SFI.
  // Also flush the eager function, which discards the IIFE.
  // This is a common scenario which requires a merge.
  TestOffThreadMerge(kEagerAndLazy,      // retained_before_background_merge
                     kToplevelAndEager,  // aged_before_background_merge
                     false,              // run_code_after_background_merge
                     kNone,              // retained_after_background_merge
                     kNone);             // aged_after_background_merge
}

TEST_F(MergeDeserializedCodeTest, MergeBasicWithLateFlushing) {
  // Flush the eager function after the background merge has taken place. In
  // this case, the data from the background thread points to the eager SFI but
  // not its bytecode, so the end result is that the eager SFI is not compiled
  // after completion on the main thread.
  TestOffThreadMerge(kEagerAndLazy,     // retained_before_background_merge
                     kToplevelSfiFlag,  // aged_before_background_merge
                     false,             // run_code_after_background_merge
                     kNone,             // retained_after_background_merge
                     kEagerSfiFlag,     // aged_after_background_merge
                     false,             // lazy_should_be_compiled
                     false);            // eager_should_be_compiled
}

TEST_F(MergeDeserializedCodeTest, RunScriptButNoReMergeNecessary) {
  // The original script is run after the background merge, causing the
  // top-level SFI and lazy SFI to become compiled. However, no SFIs are
  // created when running the script, so the main thread needn't redo the merge.
  TestOffThreadMerge(kToplevelEagerAndLazy,  // retained_before_background_merge
                     kToplevelSfiFlag,       // aged_before_background_merge
                     true,                   // run_code_after_background_merge
                     kAllScriptObjects,      // retained_after_background_merge
                     kNone,                  // aged_after_background_merge
                     true);                  // lazy_should_be_compiled
}

TEST_F(MergeDeserializedCodeTest, MainThreadReMerge) {
  // By flushing the eager SFI early, we cause the IIFE SFI to disappear
  // entirely. When the original script runs after the background merge, the
  // IIFE SFI is recreated. Thus, the main thread must redo the merge.
  TestOffThreadMerge(kToplevelEagerAndLazy,  // retained_before_background_merge
                     kToplevelAndEager,      // aged_before_background_merge
                     true,                   // run_code_after_background_merge
                     kAllScriptObjects,      // retained_after_background_merge
                     kToplevelSfiFlag,       // aged_after_background_merge
                     true);                  // lazy_should_be_compiled
}

TEST_F(MergeDeserializedCodeTest, Regress1360024) {
  // This test case triggers a re-merge on the main thread, similar to
  // MainThreadReMerge. However, it does not retain the lazy function's SFI at
  // any step, which causes the merge to use the SFI from the newly deserialized
  // script for that function. This exercises a bug in the original
  // implementation where the re-merging on the main thread would crash if the
  // merge algorithm had selected any uncompiled SFIs from the new script.
  TestOffThreadMerge(kToplevelAndEager,      // retained_before_background_merge
                     kToplevelAndEager,      // aged_before_background_merge
                     true,                   // run_code_after_background_merge
                     kToplevelAndEager,      // retained_after_background_merge
                     kToplevelSfiFlag,       // aged_after_background_merge
                     true);                  // lazy_should_be_compiled
}

TEST_F(MergeDeserializedCodeTest, MergeWithNoFollowUpWork) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  constexpr char kSourceCode[] = "function f() {}";
  Global<Script> original_script;

  // Compile the script for the first time, to both populate the Isolate
  // compilation cache and produce code cache data.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));

    // Retain the v8::Script (a JSFunction) so we can run it later.
    original_script.Reset(isolate(), script);
  }

  // Age the top-level bytecode so that the Isolate compilation cache will
  // contain only the Script.
  i::SharedFunctionInfo::EnsureOldForTesting(
      GetSharedFunctionInfo(original_script.Get(isolate())));
  {
    // We need to invoke GC without stack, otherwise some objects may survive.
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);

    // A second round of GC is necessary in case incremental marking had already
    // started before the bytecode was aged.
    InvokeMajorGC(i_isolate);
  }

  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  deserialize_thread.Join();

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
      deserialize_thread.TakeTask();

  // At this point, the cached script's top-level SFI is not compiled, so a
  // background merge is recommended.
  task->SourceTextAvailable(isolate(), NewString(kSourceCode),
                            default_origin.AsScriptOrigin());

  CHECK(task->ShouldMergeWithExistingScript());

  // Run the original script, which will cause its top-level SFI to become
  // compiled again, and make the SFI for the nested function exist.
  CHECK(!original_script.Get(isolate())->Run(context()).IsEmpty());

  // The background merge does nothing and requests no follow-up work on the
  // main thread because the original script has the same SFIs at the same level
  // of compiledness.
  MergeThread merge_thread(task.get());
  CHECK(merge_thread.Start());
  merge_thread.Join();

  // Complete compilation on the main thread. Even though no follow-up work is
  // required, this step should reuse the original script.
  ScriptCompiler::Source source(NewString(kSourceCode),
                                default_origin.AsScriptOrigin(),
                                cached_data.release(), task.release());
  Local<Script> script =
      ScriptCompiler::Compile(context(), &source,
                              ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();

  CHECK_EQ(GetSharedFunctionInfo(script),
           GetSharedFunctionInfo(original_script.Get(isolate())));
}

TEST_F(MergeDeserializedCodeTest, MergeThatCompilesLazyFunction) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  constexpr char kSourceCode[] =
      "var f = function () {var s = f.toString(); f = null; return s;};";
  constexpr uint8_t kFunctionText[] =
      "function () {var s = f.toString(); f = null; return s;}";

  // Compile the script for the first time to produce code cache data.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());

    // Cause the function to become compiled before creating the code cache.
    Local<String> expected =
        String::NewFromOneByte(isolate(), kFunctionText).ToLocalChecked();
    Local<Value> actual = RunGlobalFunc("f");
    CHECK(expected->StrictEquals(actual));

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  i_isolate->compilation_cache()->Clear();

  // Compile the script for the second time, but don't run the function 'f'.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());

    // Age the top-level bytecode so that the Isolate compilation cache will
    // contain only the Script.
    i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
  }

  {
    // We need to invoke GC without stack, otherwise some objects may survive.
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);

    // A second round of GC is necessary in case incremental marking had already
    // started before the bytecode was aged.
    InvokeMajorGC(i_isolate);
  }

  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  deserialize_thread.Join();

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
      deserialize_thread.TakeTask();

  // At this point, the cached script's function 'f' is not compiled, but the
  // matching function in the deserialized graph is compiled, so a background
  // merge is recommended.
  task->SourceTextAvailable(isolate(), NewString(kSourceCode),
                            default_origin.AsScriptOrigin());

  CHECK(task->ShouldMergeWithExistingScript());

  MergeThread merge_thread(task.get());
  CHECK(merge_thread.Start());
  merge_thread.Join();

  // Complete compilation on the main thread. This step installs compiled data
  // for the function 'f'.
  ScriptCompiler::Source source(NewString(kSourceCode),
                                default_origin.AsScriptOrigin(),
                                cached_data.release(), task.release());
  Local<Script> script =
      ScriptCompiler::Compile(context(), &source,
                              ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();
  CHECK(!script->Run(context()).IsEmpty());

  // Ensure that we can get the string representation of 'f', which requires the
  // ScopeInfo to be set correctly.
  Local<String> expected =
      String::NewFromOneByte(isolate(), kFunctionText).ToLocalChecked();
  Local<Value> actual = RunGlobalFunc("f");
  CHECK(expected->StrictEquals(actual));
}

TEST_F(MergeDeserializedCodeTest,
       MergeThatCompilesLazyFunctionWithOuterScopeInfo) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.verify_code_merge.Set(true);
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  constexpr char kSourceCode[] =
      "var outer = function () {\n"
      "  var x = 1;\n"
      "  var g = function () { return x; };\n"
      "  var f = function () { return 42; };\n"
      "  return f;\n"
      "};\n"
      "var f = outer();";

  // Compile the script for the first time to produce code cache data.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());

    // Cause the inner function to become compiled before creating the code
    // cache.
    Local<Value> actual = RunGlobalFunc("f");
    CHECK_EQ(42, actual->Int32Value(context()).FromJust());

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  i_isolate->compilation_cache()->Clear();

  // Compile the script for the second time, but don't run the function 'f'.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());

    // Age the top-level bytecode so that the Isolate compilation cache will
    // contain only the Script.
    i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
  }

  {
    // We need to invoke GC without stack, otherwise some objects may survive.
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  deserialize_thread.Join();

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
      deserialize_thread.TakeTask();

  task->SourceTextAvailable(isolate(), NewString(kSourceCode),
                            default_origin.AsScriptOrigin());

  CHECK(task->ShouldMergeWithExistingScript());

  {
    // Finalize any in-progress incremental marking and disable stress-marking
    // flags before merge_thread starts so a pre-existing GC request cannot
    // fire while merge_thread is artificially paused unparked inside
    // DisallowGarbageCollection at BeforeGetOuterScopeInfo.
    i::ManualGCScope manual_gc_scope(i_isolate);

    // Request block at sync point before background thread reads outer scope
    // info.
    base::SynchronizationPointSupport::Get()->RequestBlockAt(
        "BeforeGetOuterScopeInfo", base::TimeDelta::FromSeconds(10));

    MergeThread merge_thread(task.get());
    CHECK(merge_thread.Start());

    // Wait until background merge is paused at the sync point.
    CHECK(base::SynchronizationPointSupport::Get()->WaitUntilBlocked(
        "BeforeGetOuterScopeInfo", base::TimeDelta::FromSeconds(10)));

    // While background merge is paused before reading outer scope info, trigger
    // real lazy compilation of 'f' on the main thread. Suppress automatic GCs
    // during this call so it cannot block on Safepoint::Barrier::Arm while
    // merge_thread is paused unparked.
    {
      i::AlwaysAllocateScopeForTesting always_allocate(i_isolate->heap());
      Local<Value> actual = RunGlobalFunc("f");
      CHECK_EQ(42, actual->Int32Value(context()).FromJust());
    }

    // Resume the background merge thread.
    CHECK(base::SynchronizationPointSupport::Get()->Resume(
        "BeforeGetOuterScopeInfo"));
    merge_thread.Join();
  }

  // Complete compilation on the main thread.
  ScriptCompiler::Source source(NewString(kSourceCode),
                                default_origin.AsScriptOrigin(),
                                cached_data.release(), task.release());
  Local<Script> script =
      ScriptCompiler::Compile(context(), &source,
                              ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();
  CHECK(!script->Run(context()).IsEmpty());

  Local<Value> actual = RunGlobalFunc("f");
  CHECK_EQ(42, actual->Int32Value(context()).FromJust());
}

// Reproduces an in-development near-miss caught by the concurrency model
// checker while restoring the CompleteMergeInForeground fast-path skip: a
// two-slot handoff TOCTOU race in SharedFunctionInfo::TryGetScopeInfoForMerge
// (and TryGetOuterScopeInfo) that was previously masked on ToT because the
// foreground pass unconditionally rescanned every valid SFI in
// old_script->infos().
//
// Initial state before BeginMergeInBackground:
//   old_script->infos():
//     [0] top-level : ClearedValue (collected by GC)
//     [1] outer     : ClearedValue (collected by GC)
//     [2] old_f     : Uncompiled SFI
//                       name_or_scope_info = String("f")
//                       outer_scope_info   = Scope_outer_old (uid=1)
//   new_script->infos() (from code cache):
//     [0] new_top   : Compiled SFI (Scope_top_new)
//     [1] new_outer : Compiled SFI (Scope_outer_new, uid=1)
//     [2] new_f     : Uncompiled SFI (outer_scope_info = Scope_outer_new)
//
// Sequence Diagram:
//
//   Background Merge Thread                     Main Thread (JS Execution)
//   -----------------------                     --------------------------
//   BeginMergeInBackground:
//   [slot 1]: old_script->infos[1] is Cleared
//     -> used_new_sfis_.push_back(new_outer)
//        (new_outer has Scope_outer_new, uid=1)
//
//   [slot 2]: old_script->infos[2] is old_f
//     forwarder.RecordScopeInfos(old_f)
//       old_f->TryGetScopeInfoForMerge():
//       1) Read old_f->name_or_scope_info(kAcquireLoad)
//          -> observes String("f") (not a ScopeInfo)
//
//       === SYNCHRONIZATION_POINT("BeforeGetScopeInfoForMerge") ===
//                     |
//                     +-----------------------> RunGlobalFunc("f") (lazy
//                     compile):
//                                               1) old_f->SetScopeInfo(
//                                                    Scope_f_old,
//                                                    kReleaseStore)
//                                                  [Scope_f_old->outer =
//                                                   Scope_outer_old (uid=1)]
//                                               2)
//                                               old_f->set_feedback_metadata(
//                                                    FeedbackMetadata,
//                                                    kReleaseStore)
//                                                  [overwrites
//                                                  outer_scope_info!]
//                     <-----------------------+
//       2) Read old_f->raw_outer_scope_info_or_feedback_metadata(kAcquireLoad)
//          -> observes FeedbackMetadata (not a ScopeInfo!)
//          -> [NEAR-MISS WITHOUT FIX]: returns empty_scope_info()!
//             Scope_outer_old (uid=1) is NEVER recorded in forwarder!
//          -> [WITH FIX]: re-reads old_f->name_or_scope_info(kAcquireLoad),
//             observing Scope_f_old -> records Scope_f_old & Scope_outer_old.
//
//     3) Check old_f->scope_info(kAcquireLoad)->IsEmpty():
//        -> observes Scope_f_old (IsEmpty() == false!)
//        -> sets new_script->infos[2] = old_f (NOT Smi::zero())
//
//   --------------------------------------------------------------------------
//   CompleteMergeInForeground (on Main Thread):
//     Pass 1 (Rescan):
//       [slot 2]: maybe_new_info (old_f) == maybe_old_info (old_f)
//                 -> FAST-PATH SKIPPED! (Scope_outer_old still not recorded)
//     Pass 2 (Publish):
//       [slot 1]: publishes new_outer (with unforwarded Scope_outer_new, uid=1)
//       [slot 2]: keeps old_f (whose Scope_f_old->outer is Scope_outer_old,
//       uid=1)
//
//     VerifyCodeMerge:
//       CRASH WITHOUT FIX! Two distinct ScopeInfo objects for uid=1:
//       Scope_outer_new (on slot 1) != Scope_outer_old (on slot 2's outer
//       chain)
//       -> Isolate::PushParamsAndDie("duplicate scope info unique id")
TEST_F(MergeDeserializedCodeTest,
       ConcurrentLazyCompileDuringTryGetScopeInfoForMerge) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.verify_code_merge.Set(true);
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  constexpr char kSourceCode[] =
      "var outer = function () {\n"
      "  var x = 42;\n"
      "  var f = function () { return x; };\n"
      "  return f;\n"
      "};\n"
      "var f = outer();\n"
      "outer = null;";

  // 1. First compilation: outer() runs so 'outer' is compiled in the code
  // cache (with Scope_outer_new), while 'f' remains uncompiled.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  i_isolate->compilation_cache()->Clear();

  // 2. Second compilation: run outer() and set outer = null, then age both
  // top-level and 'outer' so GC clears slot 0 (top-level) and slot 1 (outer) in
  // old_script->infos(). Only slot 2 ('f', uncompiled) remains alive and holds
  // the sole reference to Scope_outer_old in its outer_scope_info slot.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());

    i::Tagged<i::SharedFunctionInfo> top_sfi = GetSharedFunctionInfo(script);
    i::Tagged<i::Script> old_script = i::Cast<i::Script>(top_sfi->script());
    for (uint32_t k = 0; k < old_script->infos()->ulength().value(); ++k) {
      i::Tagged<i::HeapObject> ho;
      if (old_script->infos()->get(k).GetHeapObjectIfWeak(&ho) &&
          i::Is<i::SharedFunctionInfo>(ho)) {
        i::Tagged<i::SharedFunctionInfo> sfi =
            i::Cast<i::SharedFunctionInfo>(ho);
        if (sfi->is_compiled()) {
          i::SharedFunctionInfo::EnsureOldForTesting(sfi);
        }
      }
    }
  }

  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  deserialize_thread.Join();

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
      deserialize_thread.TakeTask();
  task->SourceTextAvailable(isolate(), NewString(kSourceCode),
                            default_origin.AsScriptOrigin());
  CHECK(task->ShouldMergeWithExistingScript());

  {
    // Finalize any in-progress incremental marking and disable stress-marking
    // flags before merge_thread starts so a pre-existing GC request cannot
    // fire while merge_thread is artificially paused unparked inside
    // DisallowGarbageCollection at BeforeGetScopeInfoForMerge.
    i::ManualGCScope manual_gc_scope(i_isolate);

    base::SynchronizationPointSupport::Get()->RequestBlockAt(
        "BeforeGetScopeInfoForMerge", base::TimeDelta::FromSeconds(10));

    MergeThread merge_thread(task.get());
    CHECK(merge_thread.Start());

    CHECK(base::SynchronizationPointSupport::Get()->WaitUntilBlocked(
        "BeforeGetScopeInfoForMerge", base::TimeDelta::FromSeconds(10)));

    // Compile 'f' on the main thread while the background thread is paused
    // between name_or_scope_info(kAcquireLoad) and
    // raw_outer_scope_info_or_feedback_metadata(kAcquireLoad). Suppress
    // automatic GCs during this call so it cannot block on
    // Safepoint::Barrier::Arm while merge_thread is paused unparked.
    {
      i::AlwaysAllocateScopeForTesting always_allocate(i_isolate->heap());
      Local<Value> actual = RunGlobalFunc("f");
      CHECK_EQ(42, actual->Int32Value(context()).FromJust());
    }

    CHECK(base::SynchronizationPointSupport::Get()->Resume(
        "BeforeGetScopeInfoForMerge"));
    merge_thread.Join();
  }

  ScriptCompiler::Source source(NewString(kSourceCode),
                                default_origin.AsScriptOrigin(),
                                cached_data.release(), task.release());
  Local<Script> script =
      ScriptCompiler::Compile(context(), &source,
                              ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();
  CHECK(!script->Run(context()).IsEmpty());
}

// Reproduces an in-development near-miss caught by the concurrency model
// checker while replacing `sfis_without_scope_info_` with an in-band
// `Smi::zero()` sentinel in `new_script->infos()`: an inter-statement TOCTOU
// race in BeginMergeInBackground if `Smi::zero()` were decided by a second
// load `sfi->scope_info(kAcquireLoad)->IsEmpty()` after
// `forwarder.RecordScopeInfos(maybe_old_info)` instead of using the
// `RecordResult` returned directly by `RecordScopeInfos`.
//
// Initial state before BeginMergeInBackground:
//   old_script->infos():
//     [0] top-level : ClearedValue (collected by GC)
//     [1] old_outer : Uncompiled SFI (scope_info = empty_scope_info)
//     [2] old_f     : UndefinedValue (not yet allocated because outer() never
//     ran)
//   new_script->infos() (from code cache):
//     [0] new_top   : Compiled SFI
//     [1] new_outer : Flushed SFI (Scope_outer_new, uid=1, no BytecodeArray)
//     [2] new_f     : Compiled SFI (Scope_f_new, outer = Scope_outer_new,
//     uid=1)
//
// Sequence Diagram:
//
//   Background Merge Thread                     Main Thread (JS Execution)
//   -----------------------                     --------------------------
//   BeginMergeInBackground:
//   [slot 1] (old_outer):
//     1) forwarder.RecordScopeInfos(old_outer):
//        old_outer->TryGetScopeInfoForMerge()
//        -> observes empty_scope_info()
//        -> records NOTHING in forwarder (returns kMissingOwnScopeInfo)
//
//     === SYNCHRONIZATION_POINT("BackgroundMerge_AfterRecordScopeInfos") ===
//                     |
//                     +-----------------------> outer() called (lazy compile):
//                                               IterativelyExecuteAndFinalize...
//                                               a) UpdateSharedFunctionFlags...
//                                                  -> old_outer->SetScopeInfo(
//                                                       Scope_outer_old, uid=1)
//
//                                               === SYNCHRONIZATION_POINT(
//                                                   "AfterUpdateSharedFunctionFlags")
//                                                   [Paused BEFORE
//                                                   FinalizeSingle
//                                                    UnoptimizedCompilationJob
//                                                    allocates old_f at slot
//                                                    2!]
//                     <-----------------------+
//     2) [NEAR-MISS WITHOUT FIX]:
//        Second load `old_outer->scope_info(kAcquireLoad)->IsEmpty()`
//        -> observes Scope_outer_old (IsEmpty() == false!)
//        -> sets new_script->infos[1] = old_outer (NOT Smi::zero()!)
//        [WITH FIX]:
//        Uses `record_result == kMissingOwnScopeInfo` from step 1
//        -> sets new_script->infos[1] = Smi::zero()
//
//   [slot 2] (old_f):
//     3) old_script->infos[2] is STILL UndefinedValue!
//        -> used_new_sfis_.push_back(new_f)
//           (new_f's Scope_f_new->outer is Scope_outer_new, uid=1)
//
//   (BackgroundMerge finishes)
//                     |
//                     +-----------------------> Resumes
//                     FinalizeSingleUnoptimized...
//                                               b) Allocates old_f at slot 2
//                                               c) Ages old_outer + MajorGC
//                                               collects
//                                                  unreferenced old_f at slot
//                                                  2, leaving slot 2 =
//                                                  ClearedValue while old_outer
//                                                  (slot 1) retains
//                                                  Scope_outer_old (uid=1).
//
//   --------------------------------------------------------------------------
//   CompleteMergeInForeground (on Main Thread):
//     Pass 1 (Rescan):
//       [slot 1]: [NEAR-MISS WITHOUT FIX] maybe_new_info (old_outer) ==
//       maybe_old_info
//                 -> FAST-PATH SKIPPED! Scope_outer_old (uid=1) NEVER recorded!
//                 [WITH FIX] maybe_new_info is Smi::zero()
//                 -> rescans old_outer, records Scope_outer_old (uid=1), and
//                    forwards new_f's outer scope to Scope_outer_old!
//     Pass 2 (Publish):
//       [slot 1]: keeps old_outer (with Scope_outer_old, uid=1)
//       [slot 2]: publishes new_f (from used_new_sfis_, whose outer scope is
//                 still Scope_outer_new (uid=1) without the fix!)
//
//     VerifyCodeMerge:
//       CRASH WITHOUT FIX! Duplicate ScopeInfo for uid=1:
//       Scope_outer_old (on slot 1) != Scope_outer_new (on slot 2's outer
//       chain)
//       -> Isolate::PushParamsAndDie("duplicate scope info unique id")
TEST_F(MergeDeserializedCodeTest, ConcurrentLazyCompileAfterRecordScopeInfos) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.verify_code_merge.Set(true);
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  constexpr char kSourceCode[] =
      "var outer = function () {\n"
      "  var x = 42;\n"
      "  var f = function () { return x; };\n"
      "  return f;\n"
      "};\n"
      "if (globalThis.run_outer) {\n"
      "  var f = outer();\n"
      "}";

  // 1. First compilation: define `globalThis.run_outer = true` so both
  // `outer()` and `f()` are compiled (creating Scope_outer_new and
  // Scope_f_new). Then flush `outer`'s bytecode while keeping `f` alive and
  // compiled so that in `cached_data`, `new_outer` (slot 1) has no bytecode
  // (`!new_outer->HasBytecodeArray()`) while `new_f` (slot 2) is compiled with
  // `OuterScopeInfo == Scope_outer_new`.
  {
    v8::HandleScope handle_scope(isolate());
    CHECK(!Script::Compile(context(), NewString("globalThis.run_outer = true;"))
               .ToLocalChecked()
               ->Run(context())
               .IsEmpty());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    Local<Value> actual = RunGlobalFunc("f");
    CHECK_EQ(42, actual->Int32Value(context()).FromJust());

    Local<Function> outer_fn = context()
                                   ->Global()
                                   ->Get(context(), NewString("outer"))
                                   .ToLocalChecked()
                                   .As<Function>();
    i::Tagged<i::SharedFunctionInfo> outer_sfi =
        i::Cast<i::JSFunction>(*v8::Utils::OpenDirectHandle(*outer_fn))
            ->shared();
    i::SharedFunctionInfo::EnsureOldForTesting(outer_sfi);
    {
      i::DisableConservativeStackScanningScopeForTesting no_css_scope(
          i_isolate->heap());
      InvokeMajorGC(i_isolate);
      InvokeMajorGC(i_isolate);
    }

    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  i_isolate->compilation_cache()->Clear();

  // 2. Second compilation: set `globalThis.run_outer = false` so
  // `script->Run(context())` defines `globalThis.outer` (keeping `old_script`
  // and `old_outer` alive across GC) without calling `outer()`!
  // After GC:
  // - slot 0 (top-level) is collected (`ClearedValue`), so
  //   `ShouldMergeWithExistingScript()` is true.
  // - slot 1 (`old_outer`) is alive and UNCOMPILED
  //   (`old_outer->scope_info()->IsEmpty() == true`).
  // - slot 2 (`old_f`) is `ClearedValue`, so `new_f` (compiled in `new_script`
  //   with `OuterScopeInfo == Scope_outer_new`) will be placed in
  //   `used_new_sfis_`.
  {
    v8::HandleScope handle_scope(isolate());
    CHECK(!Script::Compile(
               context(),
               NewString("globalThis.run_outer = false; globalThis.f = null;"))
               .ToLocalChecked()
               ->Run(context())
               .IsEmpty());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
  }

  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  deserialize_thread.Join();

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
      deserialize_thread.TakeTask();
  task->SourceTextAvailable(isolate(), NewString(kSourceCode),
                            default_origin.AsScriptOrigin());
  CHECK(task->ShouldMergeWithExistingScript());

  {
    // Finalize any in-progress incremental marking and disable stress-marking
    // flags before merge_thread starts so a pre-existing GC request cannot
    // fire while merge_thread is artificially paused unparked inside
    // DisallowGarbageCollection at BackgroundMerge_AfterRecordScopeInfos.
    i::ManualGCScope manual_gc_scope(i_isolate);

    base::SynchronizationPointSupport::Get()->RequestBlockAt(
        "BackgroundMerge_AfterRecordScopeInfos",
        base::TimeDelta::FromSeconds(10));

    MergeThread merge_thread(task.get());
    CHECK(merge_thread.Start());

    // Wait until background thread has called RecordScopeInfos(old_outer) at
    // slot 1 (observing empty_scope_info) and is paused right before
    // old_outer->scope_info()->IsEmpty().
    CHECK(base::SynchronizationPointSupport::Get()->WaitUntilBlocked(
        "BackgroundMerge_AfterRecordScopeInfos",
        base::TimeDelta::FromSeconds(10)));

    class CoordinatorThread : public base::Thread {
     public:
      CoordinatorThread(MergeThread* merge_thread, base::Semaphore* ready_sem)
          : base::Thread(Options("CoordinatorThread")),
            merge_thread_(merge_thread),
            ready_sem_(ready_sem) {}
      void Run() override {
        base::SynchronizationPointSupport::Get()->RequestBlockAt(
            "AfterUpdateSharedFunctionFlags", base::TimeDelta::FromSeconds(10));
        ready_sem_->Signal();
        CHECK(base::SynchronizationPointSupport::Get()->WaitUntilBlocked(
            "AfterUpdateSharedFunctionFlags",
            base::TimeDelta::FromSeconds(10)));
        CHECK(base::SynchronizationPointSupport::Get()->Resume(
            "BackgroundMerge_AfterRecordScopeInfos"));
        merge_thread_->Join();
        CHECK(base::SynchronizationPointSupport::Get()->Resume(
            "AfterUpdateSharedFunctionFlags"));
      }

     private:
      MergeThread* merge_thread_;
      base::Semaphore* ready_sem_;
    };
    base::Semaphore ready_sem(0);
    CoordinatorThread coordinator(&merge_thread, &ready_sem);
    CHECK(coordinator.Start());
    ready_sem.Wait();

    // Call `outer()` on the main thread:
    // 1. `UpdateSharedFunctionFlagsAfterCompilation` sets
    //    `old_outer->SetScopeInfo(Scope_outer_old)` and blocks at
    //    `AfterUpdateSharedFunctionFlags` before `old_f` (slot 2) is created.
    // 2. `coordinator` resumes `merge_thread`, which sees
    //    `old_outer->scope_info()->IsEmpty() == false` at slot 1 and sees slot
    //    2 is still `UndefinedValue` (adding `new_f` to `used_new_sfis_`), and
    //    finishes `BeginMergeInBackground`.
    // 3. After `merge_thread.Join()`, the main thread finishes `outer()`, then
    //    ages `old_outer` and GCs so `old_f` at slot 2 is collected while
    //    `old_outer` at slot 1 retains `Scope_outer_old`.
    {
      // Suppress automatic GCs during this call so it cannot block on
      // Safepoint::Barrier::Arm while merge_thread is paused unparked.
      i::AlwaysAllocateScopeForTesting always_allocate(i_isolate->heap());
      v8::HandleScope handle_scope(isolate());
      Local<Function> outer_fn = context()
                                     ->Global()
                                     ->Get(context(), NewString("outer"))
                                     .ToLocalChecked()
                                     .As<Function>();
      CHECK(!outer_fn->Call(context(), context()->Global(), 0, nullptr)
                 .IsEmpty());
      coordinator.Join();

      i::Tagged<i::SharedFunctionInfo> outer_sfi =
          i::Cast<i::JSFunction>(*v8::Utils::OpenDirectHandle(*outer_fn))
              ->shared();
      i::SharedFunctionInfo::EnsureOldForTesting(outer_sfi);
    }
  }
  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  ScriptCompiler::Source source(NewString(kSourceCode),
                                default_origin.AsScriptOrigin(),
                                cached_data.release(), task.release());
  Local<Script> script =
      ScriptCompiler::Compile(context(), &source,
                              ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();
  CHECK(!script->Run(context()).IsEmpty());
}

// Verifies that BeginMergeInBackground pins unpaired cached SharedFunctionInfos
// before overwriting new_script->infos()[i] with Smi::zero(). Without pinning,
// GC between BeginMergeInBackground and CompleteMergeInForeground can collect
// the old SFI and allow a recompile to allocate a different uncompiled SFI with
// a fresh outer ScopeInfo in the same slot; CompleteMergeInForeground would
// then mistake Smi::zero() as still matching the original SFI and skip
// RecordScopeInfos, causing VerifyCodeMerge to fail with duplicate ScopeInfo.
TEST_F(MergeDeserializedCodeTest, UnpairedUncompiledSfiWithSmiSentinel) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.verify_code_merge.Set(true);
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  constexpr char kSourceCode[] =
      "var outer = function () {\n"
      "  var x = 42;\n"
      "  var mid = function () {\n"
      "    return function inner() { return x; };\n"
      "  };\n"
      "  return mid;\n"
      "};\n"
      "if (globalThis.compile_mode === 1) {\n"
      "  globalThis.mid_fn = outer();\n"
      "} else if (globalThis.compile_mode === 2) {\n"
      "  globalThis.inner_fn = outer()();\n"
      "  globalThis.outer = null;\n"
      "}";

  // 1. Create code cache with `outer` compiled (owning Scope_outer_new) and
  // `mid` uncompiled, so `inner` (slot 3) is absent (UndefinedValue) in
  // `new_script->infos()`.
  {
    v8::HandleScope handle_scope(isolate());
    CHECK(!Script::Compile(context(), NewString("globalThis.compile_mode = 1;"))
               .ToLocalChecked()
               ->Run(context())
               .IsEmpty());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
  }

  i_isolate->compilation_cache()->Clear();

  auto age_all_compiled_sfis = [&](Local<Script> script) {
    i::Tagged<i::SharedFunctionInfo> top_sfi = GetSharedFunctionInfo(script);
    i::Tagged<i::Script> i_script = i::Cast<i::Script>(top_sfi->script());
    for (uint32_t k = 0; k < i_script->infos()->ulength().value(); ++k) {
      i::Tagged<i::HeapObject> ho;
      if (i_script->infos()->get(k).GetHeapObjectIfWeak(&ho) &&
          i::Is<i::SharedFunctionInfo>(ho)) {
        i::Tagged<i::SharedFunctionInfo> sfi =
            i::Cast<i::SharedFunctionInfo>(ho);
        if (sfi->is_compiled()) {
          i::SharedFunctionInfo::EnsureOldForTesting(sfi);
        }
      }
    }
  };

  // 2. Populate `old_script` with only `old_inner_1` alive (uncompiled, holding
  // Scope_outer_1 as its outer_scope_info), while top-level, `outer`, and `mid`
  // are collected.
  {
    v8::HandleScope handle_scope(isolate());
    CHECK(!Script::Compile(
               context(),
               NewString(
                   "globalThis.compile_mode = 2; globalThis.mid_fn = null;"))
               .ToLocalChecked()
               ->Run(context())
               .IsEmpty());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    age_all_compiled_sfis(script);
  }
  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  // 3. Deserialize and run BeginMergeInBackground. Slot 3 (`inner`) is unpaired
  // (UndefinedValue in new_script, Weak(old_inner_1) in old_script), so
  // BeginMergeInBackground records Scope_outer_1 (forwarding new_outer to
  // Scope_outer_1) and writes Smi::zero() to new_script->infos()[3].
  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  deserialize_thread.Join();

  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
      deserialize_thread.TakeTask();
  task->SourceTextAvailable(isolate(), NewString(kSourceCode),
                            default_origin.AsScriptOrigin());
  CHECK(task->ShouldMergeWithExistingScript());

  MergeThread merge_thread(task.get());
  CHECK(merge_thread.Start());
  merge_thread.Join();

  // 4. Between BeginMergeInBackground and CompleteMergeInForeground, drop
  // `inner_fn` and run GC. Without pinning `old_inner_1`, GC collects it and
  // the subsequent compilation (merging against the still-cached `old_script`)
  // creates a fresh Scope_outer_2 and `old_inner_2` at slot 3.
  {
    v8::HandleScope handle_scope(isolate());
    CHECK(!Script::Compile(context(), NewString("globalThis.inner_fn = null;"))
               .ToLocalChecked()
               ->Run(context())
               .IsEmpty());
  }
  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    age_all_compiled_sfis(script);
  }
  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  // 5. CompleteMergeInForeground + VerifyCodeMerge must not observe duplicate
  // ScopeInfos for `outer` (Scope_outer_1 vs Scope_outer_2).
  ScriptCompiler::Source source(NewString(kSourceCode),
                                default_origin.AsScriptOrigin(),
                                cached_data.release(), task.release());
  Local<Script> script =
      ScriptCompiler::Compile(context(), &source,
                              ScriptCompiler::kConsumeCodeCache)
          .ToLocalChecked();
  CHECK(!script->Run(context()).IsEmpty());
  Local<Value> actual = RunGlobalFunc("inner_fn");
  CHECK_EQ(42, actual->Int32Value(context()).FromJust());
}

// POC: Relaxed-store publication of fresh SharedFunctionInfo into
// Script::infos() read concurrently by BackgroundMergeTask without acquire.
//
// Writer (main thread): calling an uncompiled outer closure triggers lazy
// compilation, which for every nested function literal calls
// Compiler::GetSharedFunctionInfo -> NewSharedFunctionInfoForLiteral ->
// SharedFunctionInfo::SetScript. SetScript publishes the freshly-allocated
// SFI into the live cached Script's infos() WeakFixedArray with a relaxed
// store (fixed-array-inl.h TaggedArrayBase::set -> Relaxed_Store).
//
// Reader (background thread): BackgroundMergeTask::BeginMergeInBackground
// iterates the same infos() array and relaxed-loads each slot
// (compiler.cc:2447), then immediately dereferences the loaded object
// (HasBytecodeArray / scope_info / RecordScopeInfos at 2466-2504).
//
// There is no release fence between SFI field initialisation and the infos()
// pointer store, and no acquire on the infos() load. TSAN reports this as a
// data race; on ARM/ARM64 the background thread may observe the new pointer
// before the SFI's field stores are visible.
TEST_F(MergeDeserializedCodeTest,
       DISABLED_ConcurrentLazyCompileDuringBackgroundMerge) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.lazy_compile_dispatcher.Set(false);

  // Source layout (literal-id order):
  //   [0]                toplevel
  //   [1..kPadLiterals]  many padding closures, each with many nested
  //                      literals -> the deserialised cache contains an SFI
  //                      for every one of these slots, so the background
  //                      merge does non-trivial work per slot and takes
  //                      milliseconds to walk the array.
  //   [tail..]           kNumQuick small closures with one nested literal
  //                      each. Calling these on the main thread is fast and
  //                      allocates fresh SFIs into the *high-index* infos()
  //                      slots while the merge loop is still walking towards
  //                      them.
  constexpr int kNumPad = 60;
  constexpr int kPadInner = 400;
  constexpr int kNumQuick = 200;
  std::string src = "var pad = [];\n";
  for (int o = 0; o < kNumPad; ++o) {
    src += "pad[" + std::to_string(o) + "] = function(){\n";
    for (int j = 0; j < kPadInner; ++j) {
      src += "  var g" + std::to_string(j) + " = function(){return " +
             std::to_string(j) + ";};\n";
    }
    src += "};\n";
  }
  src += "var quick = [];\n";
  for (int q = 0; q < kNumQuick; ++q) {
    src += "quick[" + std::to_string(q) +
           "] = function(){ let c=1; var i=function(){return c;}; };\n";
  }

  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  // 1) Compile, run the toplevel (creates JSFunctions for outers), call every
  // outer so that *all* nested SFIs exist and get serialised into the code
  // cache. A fully-populated cache makes the background merge do non-trivial
  // work for every slot, lengthening the race window.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(src.c_str()), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    // Compile all padding + quick closures so the code cache is fully
    // populated; this makes the deserialised new_script have an SFI in every
    // slot, so the background merge does work for every iteration.
    CHECK(!Script::Compile(
               context(),
               NewString("for (var p of pad) p(); for (var q of quick) q();"))
               .ToLocalChecked()
               ->Run(context())
               .IsEmpty());
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));

    // Age every compiled SFI in the live cached Script's infos()
    i::Tagged<i::Script> i_script =
        i::Cast<i::Script>(GetSharedFunctionInfo(script)->script());
    i::IndirectHandle<i::WeakFixedArray> old_infos(i_script->infos(),
                                                   i_isolate);
    // Age every compiled SFI so its bytecode is flushed on the next GC. This
    // drops the only strong references to the nested SFIs (the outer constant
    // pools), so after GC the nested infos() slots become cleared and
    // subsequent lazy compilation will allocate *fresh* SFIs there.
    for (uint32_t k = 0; k < old_infos->ulength().value(); ++k) {
      i::Tagged<i::MaybeObject> mo = old_infos->get(k);
      i::Tagged<i::HeapObject> ho;
      if (mo.GetHeapObjectIfWeak(&ho) && i::Is<i::SharedFunctionInfo>(ho)) {
        i::Tagged<i::SharedFunctionInfo> sfi =
            i::Cast<i::SharedFunctionInfo>(ho);
        if (sfi->is_compiled()) {
          i::SharedFunctionInfo::EnsureOldForTesting(sfi);
        }
      }
    }
  }

  {
    i::DisableConservativeStackScanningScopeForTesting no_css(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  // Pre-fetch the surviving quick JSFunctions so the lazy-compile loop below
  // has minimal overhead between merge_thread.Start() and the first SetScript.
  std::vector<v8::Global<v8::Function>> quick_funcs(kNumQuick);
  {
    v8::HandleScope handle_scope(isolate());
    Local<Object> arr = context()
                            ->Global()
                            ->Get(context(), NewString("quick"))
                            .ToLocalChecked()
                            .As<Object>();
    for (int q = 0; q < kNumQuick; ++q) {
      Local<Value> f = arr->Get(context(), q).ToLocalChecked();
      CHECK(f->IsFunction());
      quick_funcs[q].Reset(isolate(), f.As<Function>());
    }
  }

  // 2) Deserialise the (fully-populated) cache off-thread.
  DeserializeThread deserialize_thread(ScriptCompiler::StartConsumingCodeCache(
      isolate(), std::make_unique<ScriptCompiler::CachedData>(
                     cached_data->data, cached_data->length,
                     ScriptCompiler::CachedData::BufferNotOwned)));
  CHECK(deserialize_thread.Start());
  deserialize_thread.Join();
  std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
      deserialize_thread.TakeTask();
  task->SourceTextAvailable(isolate(), NewString(src.c_str()),
                            default_origin.AsScriptOrigin());
  CHECK(task->ShouldMergeWithExistingScript());

  // 3) RACE: start the background merge, then on the main thread immediately
  // lazy-compile the small `quick` closures.
  //
  // Background: BeginMergeInBackground iterates all ~24k infos() slots,
  // relaxed-loading each one (compiler.cc:2447) and dereferencing every weak
  // entry (HasBytecodeArray / scope_info / SetScopeInfo / RecordScopeInfos at
  // compiler.cc:2466-2504) without any acquire fence or lock.
  //
  // Main thread: each quick[q]() call lazy-compiles the closure body,
  // creating a fresh nested SharedFunctionInfo (published into the live
  // Script's infos() via SetScript -> WeakFixedArray::set -> Relaxed_Store)
  // and installing a fresh ScopeInfo on the existing quick[q] SFI -- both
  // mutate state reachable from old_script->infos() while the background
  // merge is reading and writing the same fields with plain non-atomic
  // accessors (e.g. TaggedField::load at tagged-field-inl.h:234).
  MergeThread merge_thread(task.get());
  CHECK(merge_thread.Start());
  {
    v8::HandleScope handle_scope(isolate());
    for (int q = 0; q < kNumQuick; ++q) {
      quick_funcs[q]
          .Get(isolate())
          ->Call(context(), Undefined(isolate()), 0, nullptr)
          .ToLocalChecked();
    }
  }
  merge_thread.Join();
}

TEST_F(MergeDeserializedCodeTest, MergeThatStartsButDoesNotFinish) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  constexpr int kSimultaneousScripts = 10;
  std::vector<std::unique_ptr<v8::ScriptCompiler::CachedData>> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(isolate(), NewString(""));

  // Compile the script for the first time to produce code cache data.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kSourceCode), &origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());

    // Create a bunch of copies of the code cache data.
    for (int i = 0; i < kSimultaneousScripts; ++i) {
      cached_data.emplace_back(
          ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
    }

    // Age the top-level bytecode so that the Isolate compilation cache will
    // contain only the Script.
    i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
  }

  {  // We need to invoke GC without stack, otherwise some objects may survive.
    i::DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);

    // A second round of GC is necessary in case incremental marking had already
    // started before the bytecode was aged.
    InvokeMajorGC(i_isolate);
  }

  // Start several background deserializations.
  std::vector<std::unique_ptr<DeserializeThread>> deserialize_threads;
  for (int i = 0; i < kSimultaneousScripts; ++i) {
    deserialize_threads.push_back(std::make_unique<DeserializeThread>(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data[i]->data, cached_data[i]->length,
                           ScriptCompiler::CachedData::BufferNotOwned))));
  }
  for (int i = 0; i < kSimultaneousScripts; ++i) {
    CHECK(deserialize_threads[i]->Start());
  }
  for (int i = 0; i < kSimultaneousScripts; ++i) {
    deserialize_threads[i]->Join();
  }

  // Start background merges for all of those simultaneous scripts.
  std::vector<std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask>> tasks;
  std::vector<std::unique_ptr<MergeThread>> merge_threads;
  for (int i = 0; i < kSimultaneousScripts; ++i) {
    tasks.push_back(deserialize_threads[i]->TakeTask());
    tasks[i]->SourceTextAvailable(isolate(), NewString(kSourceCode),
                                  default_origin.AsScriptOrigin());
    CHECK(tasks[i]->ShouldMergeWithExistingScript());
    merge_threads.push_back(std::make_unique<MergeThread>(tasks[i].get()));
  }
  for (int i = 0; i < kSimultaneousScripts; ++i) {
    CHECK(merge_threads[i]->Start());
  }
  for (int i = 0; i < kSimultaneousScripts; ++i) {
    merge_threads[i]->Join();
  }

  // Complete compilation of each script on the main thread. The first one will
  // actually finish its merge; the others will abandon their in-progress merges
  // and instead use the result from the first script since it will be in the
  // Isolate compilation cache.
  i::IndirectHandle<i::SharedFunctionInfo> first_script_sfi;
  for (int i = 0; i < kSimultaneousScripts; ++i) {
    ScriptCompiler::Source source(NewString(kSourceCode),
                                  default_origin.AsScriptOrigin(),
                                  cached_data[i].release(), tasks[i].release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    if (i == 0) {
      first_script_sfi = i::handle(GetSharedFunctionInfo(script), i_isolate);
    } else {
      CHECK_EQ(*first_script_sfi, GetSharedFunctionInfo(script));
    }
    CHECK(!script->Run(context()).IsEmpty());
  }
}

TEST_F(MergeDeserializedCodeTest, ModuleCacheClassicScriptMismatch) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  std::unique_ptr<v8::ScriptCompiler::CachedData> cached_data;
  IsolateAndContextScope scope(this);
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
  PersistentScriptOrigin default_origin(
      isolate(), NewString("http://example.com/test.js"));

  constexpr char kPolyglotSource[] =
      "let L0 = 1;\n"
      "var z, o;\n"
      "function g() { throw 1; }\n"
      "function K(a, b) { var t = globalThis.top; (t.__keep = t.__keep || "
      "[]).push(a, b); }\n"
      "z = await /1; function lazyOuter() { function inner() {} } { try { g() "
      "} catch /* 1, o = { a: function () {}, 'NAMENAMENAMENAME' //*/\n"
      "(e) { K(() => e, function later() { return e }) } }\n";

  // Setup: define `await` and `top` in global scope for classic script parse.
  {
    v8::HandleScope handle_scope(isolate());
    Local<Script> setup =
        Script::Compile(
            context(), NewString("var await = 6; globalThis.top = globalThis;"))
            .ToLocalChecked();
    CHECK(!setup->Run(context()).IsEmpty());
  }

  // Step 1: Compile and run as a classic script, populating the Isolate
  // compilation cache and retaining catch-block closures in globalThis.__keep.
  // Also compile as a module with the same resource name and create a module
  // code cache, then age and flush the classic script's top-level bytecode.
  {
    v8::HandleScope handle_scope(isolate());
    ScriptOrigin classic_origin = default_origin.AsScriptOrigin();
    Local<Script> script =
        Script::Compile(context(), NewString(kPolyglotSource), &classic_origin)
            .ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());

    ScriptOrigin module_origin(NewString("http://example.com/test.js"), 0, 0,
                               false, -1, Local<Value>(), false, false,
                               /*is_module=*/true);
    ScriptCompiler::Source module_source(NewString(kPolyglotSource),
                                         module_origin);
    Local<Module> module =
        ScriptCompiler::CompileModule(isolate(), &module_source)
            .ToLocalChecked();
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(module->GetUnboundModuleScript()));

    i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
  }

  // Step 2: Flush the classic script's top-level bytecode via major GC.
  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  // Step 3a: Start off-thread consumption of the MODULE code cache, then notify
  // SourceTextAvailable with the CLASSIC ScriptOrigin after Run() completes.
  {
    auto task_cached_data = std::make_unique<ScriptCompiler::CachedData>(
        cached_data->data, cached_data->length,
        ScriptCompiler::CachedData::BufferNotOwned);
    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data->data, cached_data->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();

    std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
        deserialize_thread.TakeTask();
    task->SourceTextAvailable(isolate(), NewString(kPolyglotSource),
                              default_origin.AsScriptOrigin());
    CHECK(!task->ShouldMergeWithExistingScript());

    v8::HandleScope handle_scope(isolate());
    ScriptCompiler::Source source(NewString(kPolyglotSource),
                                  default_origin.AsScriptOrigin(),
                                  task_cached_data.release(), task.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!script.IsEmpty());
    CHECK(source.GetCachedData()->rejected);

    // Age and flush the recompiled classic script's top-level bytecode again so
    // Step 3b also sees a flushed Script in the Isolate compilation cache.
    i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
  }
  {
    i::DisableConservativeStackScanningScopeForTesting no_css_scope(
        i_isolate->heap());
    InvokeMajorGC(i_isolate);
    InvokeMajorGC(i_isolate);
  }

  // Step 3b: Also test calling SourceTextAvailable on the main thread BEFORE
  // Run() executes on the background thread.
  {
    auto task_cached_data = std::make_unique<ScriptCompiler::CachedData>(
        cached_data->data, cached_data->length,
        ScriptCompiler::CachedData::BufferNotOwned);
    std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           cached_data->data, cached_data->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    task->SourceTextAvailable(isolate(), NewString(kPolyglotSource),
                              default_origin.AsScriptOrigin());

    DeserializeThread deserialize_thread(task.release());
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();
    task = deserialize_thread.TakeTask();
    CHECK(!task->ShouldMergeWithExistingScript());

    v8::HandleScope handle_scope(isolate());
    ScriptCompiler::Source source(NewString(kPolyglotSource),
                                  default_origin.AsScriptOrigin(),
                                  task_cached_data.release(), task.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!script.IsEmpty());
    CHECK(source.GetCachedData()->rejected);

    Local<Script> run_kept =
        Script::Compile(context(),
                        NewString("for (const f of globalThis.__keep) f();"))
            .ToLocalChecked();
    CHECK(!run_kept->Run(context()).IsEmpty());
  }
}

// Regression test for crbug.com/589815794:
// Serializer::ObjectSerializer::VisitPointers must not coalesce ReadOnlySpace
// slots into OutputRawData runs, because BackgroundMergeTask can concurrently
// overwrite ScopeInfo::inferred_function_name with a mutable heap string
// between VisitPointers and OutputRawData's PutRaw, leaving a raw dangling
// compressed heap pointer in the serialized code cache.
TEST_F(MergeDeserializedCodeTest,
       ConcurrentScopeInfoMutationDuringCodeCacheSerialization) {
  i::FlagScope<bool> merge_scope(
      &i::v8_flags.merge_background_deserialized_script_with_compilation_cache,
      true);
  i::FlagScope<bool> lazy_scope(&i::v8_flags.lazy, true);
  i::FlagScope<bool> flush_scope(&i::v8_flags.flush_bytecode, true);
  constexpr char kSource[] =
      "var G;\n"
      "(function setG(fn) { G = fn; })(\n"
      "  (function G() {\n"
      "    return {\n"
      "      copyWithin: function() {\n"
      "        eval('');\n"
      "        return new Error().stack;\n"
      "      }\n"
      "    }.copyWithin;\n"
      "  })\n"
      ");\n";

  std::unique_ptr<v8::ScriptCompiler::CachedData> raced_cache;

  {
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    PersistentScriptOrigin default_origin(
        isolate(), NewString("http://example.com/race.js"));

    std::unique_ptr<v8::ScriptCompiler::CachedData> heap_name_cache;
    // 1. Flush G's eagerly-compiled bytecode and recompile G lazily so
    // Parser::DoParseFunction pushes "G" as enclosing name, giving copyWithin a
    // mutable heap-allocated inferred name ("G.copyWithin") in the code cache.
    {
      v8::HandleScope handle_scope(isolate());
      ScriptOrigin origin = default_origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSource), &origin)
              .ToLocalChecked();
      CHECK(!script->Run(context()).IsEmpty());

      Local<Value> g_val =
          context()->Global()->Get(context(), NewString("G")).ToLocalChecked();
      CHECK(g_val->IsFunction());
      i::SharedFunctionInfo::EnsureOldForTesting(
          GetSharedFunctionInfo(g_val.As<Function>()));

      {
        i::DisableConservativeStackScanningScopeForTesting no_css(
            i_isolate->heap());
        InvokeMajorGC(i_isolate);
        InvokeMajorGC(i_isolate);
      }

      Local<Value> inner_val = RunGlobalFunc("G");
      CHECK(inner_val->IsFunction());
      Local<Function> inner_fn = inner_val.As<Function>();
      CHECK(!inner_fn->Call(context(), Undefined(isolate()), 0, nullptr)
                 .IsEmpty());

      i::Tagged<i::SharedFunctionInfo> inner_sfi =
          GetSharedFunctionInfo(inner_fn);
      CHECK(inner_sfi->HasScopeInfo());
      CHECK(inner_sfi->scope_info()->HasInferredFunctionName());
      i::Tagged<i::Object> inferred =
          inner_sfi->scope_info()->inferred_function_name();
      CHECK(i::IsString(inferred));
      CHECK(!i::HeapLayout::InReadOnlySpace(i::Cast<i::HeapObject>(inferred)));

      heap_name_cache.reset(
          ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
    }

    i_isolate->compilation_cache()->Clear();

    // 2. Compile the same script fresh so G is eagerly parsed without pushing
    // "G" as enclosing name, leaving copyWithin's
    // ScopeInfo::inferred_function_name as the non-root ReadOnlySpace string
    // "copyWithin".
    v8::Global<v8::Function> kept_inner_fn;
    {
      v8::HandleScope handle_scope(isolate());
      ScriptOrigin origin = default_origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSource), &origin)
              .ToLocalChecked();
      CHECK(!script->Run(context()).IsEmpty());
      Local<Value> inner_val = RunGlobalFunc("G");
      CHECK(inner_val->IsFunction());
      Local<Function> inner_fn = inner_val.As<Function>();
      CHECK(!inner_fn->Call(context(), Undefined(isolate()), 0, nullptr)
                 .IsEmpty());
      kept_inner_fn.Reset(isolate(), inner_fn);

      i::Tagged<i::SharedFunctionInfo> inner_sfi =
          GetSharedFunctionInfo(inner_fn);
      CHECK(inner_sfi->HasScopeInfo());
      CHECK(inner_sfi->scope_info()->HasInferredFunctionName());
      i::Tagged<i::Object> inferred =
          inner_sfi->scope_info()->inferred_function_name();
      CHECK(i::IsString(inferred));
#if V8_STATIC_ROOTS_BOOL
      CHECK(i::HeapLayout::InReadOnlySpace(i::Cast<i::HeapObject>(inferred)));
#endif

      // Age all compiled SFIs so top-level and o.copyWithin flush bytecode
      // while o.copyWithin retains its ScopeInfo (and old_script stays in the
      // compilation cache).
      i::Tagged<i::Script> old_script =
          i::Cast<i::Script>(GetSharedFunctionInfo(script)->script());
      for (uint32_t k = 0; k < old_script->infos()->ulength().value(); ++k) {
        i::Tagged<i::HeapObject> ho;
        if (old_script->infos()->get(k).GetHeapObjectIfWeak(&ho) &&
            i::Is<i::SharedFunctionInfo>(ho)) {
          i::Tagged<i::SharedFunctionInfo> sfi =
              i::Cast<i::SharedFunctionInfo>(ho);
          if (sfi->is_compiled()) {
            i::SharedFunctionInfo::EnsureOldForTesting(sfi);
          }
        }
      }
    }

    {
      i::DisableConservativeStackScanningScopeForTesting no_css(
          i_isolate->heap());
      InvokeMajorGC(i_isolate);
      InvokeMajorGC(i_isolate);
    }

    // 3. Start background code-cache deserialization and set up the merge task
    // against old_script (whose top-level SFI is flushed).
    DeserializeThread deserialize_thread(
        ScriptCompiler::StartConsumingCodeCache(
            isolate(), std::make_unique<ScriptCompiler::CachedData>(
                           heap_name_cache->data, heap_name_cache->length,
                           ScriptCompiler::CachedData::BufferNotOwned)));
    CHECK(deserialize_thread.Start());
    deserialize_thread.Join();

    std::unique_ptr<ScriptCompiler::ConsumeCodeCacheTask> task =
        deserialize_thread.TakeTask();
    task->SourceTextAvailable(isolate(), NewString(kSource),
                              default_origin.AsScriptOrigin());
    CHECK(task->ShouldMergeWithExistingScript());

    // 4. Recompile top-level and G on old_script so CreateCodeCache will
    // serialize old_script -> G -> o.copyWithin's SFI -> ScopeInfo, while
    // o.copyWithin itself remains uncompiled with its ReadOnlySpace
    // inferred_function_name.
    v8::Global<v8::UnboundScript> script_for_cache;
    {
      v8::HandleScope handle_scope(isolate());
      ScriptOrigin origin = default_origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSource), &origin)
              .ToLocalChecked();
      CHECK(!script->Run(context()).IsEmpty());
      Local<Value> inner_val = RunGlobalFunc("G");
      CHECK(inner_val->IsFunction());
      script_for_cache.Reset(isolate(), script->GetUnboundScript());

      i::Tagged<i::SharedFunctionInfo> inner_sfi =
          GetSharedFunctionInfo(kept_inner_fn.Get(isolate()));
      CHECK(!inner_sfi->HasBytecodeArray());
      CHECK(inner_sfi->HasScopeInfo());
#if V8_STATIC_ROOTS_BOOL
      CHECK(i::HeapLayout::InReadOnlySpace(i::Cast<i::HeapObject>(
          inner_sfi->scope_info()->inferred_function_name())));
#endif
    }

    // 5. Run CreateCodeCache on the main thread while pausing right before
    // OutputRawData copies ScopeInfo raw bytes, and execute the background
    // merge (which mutates ScopeInfo::inferred_function_name to the heap string
    // "G.copyWithin") during that pause.
    class RaceMergeThread : public base::Thread {
     public:
      RaceMergeThread(ScriptCompiler::ConsumeCodeCacheTask* task,
                      base::Semaphore* armed)
          : Thread(base::Thread::Options("RaceMergeThread")),
            task_(task),
            armed_(armed) {}

      void Run() override {
        base::SynchronizationPointSupport::Get()->RequestBlockAt(
            "SerializerOutputRawDataScopeInfo",
            base::TimeDelta::FromSeconds(10));
        armed_->Signal();
        CHECK(base::SynchronizationPointSupport::Get()->WaitUntilBlocked(
            "SerializerOutputRawDataScopeInfo",
            base::TimeDelta::FromSeconds(10)));
        task_->MergeWithExistingScript();
        CHECK(base::SynchronizationPointSupport::Get()->Resume(
            "SerializerOutputRawDataScopeInfo"));
      }

     private:
      ScriptCompiler::ConsumeCodeCacheTask* task_;
      base::Semaphore* armed_;
    };

    {
      i::ManualGCScope manual_gc_scope(i_isolate);
      base::Semaphore armed(0);
      RaceMergeThread merge_thread(task.get(), &armed);
      CHECK(merge_thread.Start());
      armed.Wait();

      {
        v8::HandleScope handle_scope(isolate());
        raced_cache.reset(
            ScriptCompiler::CreateCodeCache(script_for_cache.Get(isolate())));
      }

      merge_thread.Join();
    }

    // Verify the background merge actually overwrote inferred_function_name
    // with the mutable heap string "G.copyWithin".
    {
      v8::HandleScope handle_scope(isolate());
      i::Tagged<i::SharedFunctionInfo> inner_sfi =
          GetSharedFunctionInfo(kept_inner_fn.Get(isolate()));
      i::Tagged<i::Object> inferred =
          inner_sfi->scope_info()->inferred_function_name();
      CHECK(i::IsString(inferred));
      CHECK(!i::HeapLayout::InReadOnlySpace(i::Cast<i::HeapObject>(inferred)));
    }

    CHECK_NOT_NULL(raced_cache.get());
  }

  // 6. Isolate 1 (and its heap string "G.copyWithin") is now disposed.
  // Deserialize raced_cache in a fresh Isolate 2 and verify ScopeInfo is
  // valid and passes heap verification.
  {
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    // Match the GC flags active during CreateCodeCache so the code-cache flag
    // hash check succeeds.
    i::ManualGCScope manual_gc_scope(i_isolate);
    PersistentScriptOrigin default_origin(
        isolate(), NewString("http://example.com/race.js"));

    v8::HandleScope handle_scope(isolate());
    ScriptCompiler::Source source(NewString(kSource),
                                  default_origin.AsScriptOrigin(),
                                  raced_cache.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!source.GetCachedData()->rejected);
    CHECK(!script->Run(context()).IsEmpty());

    Local<Value> inner_val = RunGlobalFunc("G");
    CHECK(inner_val->IsFunction());
    Local<Function> inner_fn = inner_val.As<Function>();
    i::Tagged<i::SharedFunctionInfo> inner_sfi =
        GetSharedFunctionInfo(inner_fn);
    CHECK(inner_sfi->HasScopeInfo());
    CHECK(inner_sfi->scope_info()->HasInferredFunctionName());
    i::Tagged<i::Object> inferred =
        inner_sfi->scope_info()->inferred_function_name();
    CHECK(i::IsString(inferred));
    std::unique_ptr<char[]> name_cstr =
        i::Cast<i::String>(inferred)->ToCString();
    std::string_view name_view(name_cstr.get());
    CHECK(name_view == "G.copyWithin" || name_view == "copyWithin");

    Local<Value> stack_val =
        inner_fn->Call(context(), Undefined(isolate()), 0, nullptr)
            .ToLocalChecked();
    CHECK(stack_val->IsString());

#ifdef VERIFY_HEAP
    i::HeapVerifier::VerifyHeap(i_isolate->heap());
#endif
  }
}

// Regression test: when old_script discovers a canonical ScopeInfo for a
// standalone eval site between BeginMergeInBackground and
// CompleteMergeInForeground (while old_script->infos()[eval_idx] is still
// empty), CompleteMergeInForeground forwards and publishes it.
TEST_F(MergeDeserializedCodeTest, ForegroundStandaloneEvalScopeInfo) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.flush_bytecode.Set(true);

  std::string chain = "y";
  for (int i = 0; i < 250; ++i) chain += " + 1 - 1";
  std::string src =
      "function outer() { return (function inner() { "
      "let y = 1; return eval('y') + " +
      chain + "; }); } globalThis.makeOuter = outer;";
  constexpr char kUrl[] = "http://example.com/fg_eval.js";

  auto cached_data = MakeCodeCache(src.c_str(), kUrl, "makeOuter()()");

  {
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    PersistentScriptOrigin origin(isolate(), NewString(kUrl));
    {
      v8::HandleScope hs(isolate());
      ScriptOrigin script_origin = origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(src.c_str()), &script_origin)
              .ToLocalChecked();
      CHECK(!script->Run(context()).IsEmpty());
      i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
    }
    FlushWithMajorGC(i_isolate);

    auto task =
        StartMergeTask(src.c_str(), origin.AsScriptOrigin(), cached_data.get());

    // Trigger a stack-overflowed partial eager compile of `outer()` on
    // old_script so `inner` gets an uncompiled SFI with ScopeInfo S_old while
    // old_script->infos()[eval_idx] stays empty. Adjust the C++ stack limit
    // directly rather than recursing in JS so this works identically under
    // simulator builds where JS frames live on a separate stack.
    {
      v8::HandleScope hs(isolate());
      Local<Function> make_outer = context()
                                       ->Global()
                                       ->Get(context(), NewString("makeOuter"))
                                       .ToLocalChecked()
                                       .As<Function>();
      i::Handle<i::SharedFunctionInfo> outer_sfi(
          GetSharedFunctionInfo(make_outer), i_isolate);
      CHECK(!outer_sfi->is_compiled());
      const uintptr_t original_limit = i_isolate->stack_guard()->real_climit();
      const uintptr_t current_sp = i::GetCurrentStackPosition();
      CHECK_GT(current_sp, original_limit);
      const size_t max_budget = current_sp - original_limit;
      for (size_t budget = 2 * i::KB;
           budget < max_budget && !outer_sfi->is_compiled();
           budget += 2 * i::KB) {
        i_isolate->stack_guard()->SetStackLimit(current_sp - budget);
        i::IsCompiledScope is_compiled_scope;
        i::Compiler::Compile(i_isolate, outer_sfi, i::Compiler::CLEAR_EXCEPTION,
                             &is_compiled_scope);
      }
      i_isolate->stack_guard()->SetStackLimit(original_limit);
    }

    {
      v8::HandleScope hs(isolate());
      Local<Value> make_outer = context()
                                    ->Global()
                                    ->Get(context(), NewString("makeOuter"))
                                    .ToLocalChecked();
      i::DisallowGarbageCollection no_gc;
      i::Tagged<i::Script> s = i::Cast<i::Script>(
          GetSharedFunctionInfo(make_outer.As<Function>())->script());
      bool saw_uncompiled_inner_with_si = false;
      bool saw_standalone_si = false;
      for (uint32_t i = 1; i < s->infos()->ulength().value(); ++i) {
        i::Tagged<i::HeapObject> obj;
        if (!s->infos()->get(i).GetHeapObjectIfWeak(&obj)) continue;
        if (i::Is<i::ScopeInfo>(obj)) {
          saw_standalone_si = true;
        } else if (i::Is<i::SharedFunctionInfo>(obj)) {
          auto sfi = i::Cast<i::SharedFunctionInfo>(obj);
          if (!sfi->is_compiled() && sfi->HasScopeInfo() &&
              sfi->Name()->IsEqualTo(base::OneByteVector("inner", 5))) {
            saw_uncompiled_inner_with_si = true;
          }
        }
      }
      CHECK(saw_uncompiled_inner_with_si);
      CHECK(!saw_standalone_si);
    }

    ScriptCompiler::Source source(NewString(src.c_str()),
                                  origin.AsScriptOrigin(),
                                  cached_data.release(), task.release());
    Local<Script> merged =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!source.GetCachedData()->rejected);
    i::HeapVerifier::VerifyHeap(i_isolate->heap());

    {
      i::DisallowGarbageCollection no_gc;
      i::Tagged<i::Script> s =
          i::Cast<i::Script>(GetSharedFunctionInfo(merged)->script());
      i::Tagged<i::ScopeInfo> inner_si;
      i::Tagged<i::ScopeInfo> standalone_si;
      for (uint32_t i = 1; i < s->infos()->ulength().value(); ++i) {
        i::Tagged<i::HeapObject> obj;
        if (!s->infos()->get(i).GetHeapObjectIfWeak(&obj)) continue;
        if (i::Is<i::ScopeInfo>(obj)) {
          standalone_si = i::Cast<i::ScopeInfo>(obj);
        } else if (i::Is<i::SharedFunctionInfo>(obj)) {
          auto sfi = i::Cast<i::SharedFunctionInfo>(obj);
          if (sfi->HasScopeInfo() &&
              sfi->Name()->IsEqualTo(base::OneByteVector("inner", 5))) {
            inner_si = sfi->scope_info();
          }
        }
      }
      CHECK(!inner_si.is_null());
      CHECK_EQ(standalone_si, inner_si);
    }

    CHECK(!merged->Run(context()).IsEmpty());
    CHECK_EQ(2, Eval("makeOuter()()")->Int32Value(context()).FromJust());
  }
}

// Regression test: an uncompiled cached SFI can acquire a DebugInfo (e.g. via
// v8::debug::GetDebuggingId) between BeginMergeInBackground and
// CompleteMergeInForeground.
TEST_F(MergeDeserializedCodeTest, MergeWithDebugInfoOnUncompiledCachedSfi) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.flush_bytecode.Set(true);

  constexpr char kSrc[] = "function f() { return 42; } globalThis.f = f;";
  constexpr char kUrl[] = "http://example.com/debug_info_merge.js";
  auto cached_data = MakeCodeCache(kSrc, kUrl, "f()");

  {
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    PersistentScriptOrigin origin(isolate(), NewString(kUrl));
    int old_script_id = 0;
    {
      v8::HandleScope hs(isolate());
      ScriptOrigin script_origin = origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSrc), &script_origin)
              .ToLocalChecked();
      old_script_id = script->GetUnboundScript()->ScriptId();
      CHECK(!script->Run(context()).IsEmpty());
      i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
    }
    FlushWithMajorGC(i_isolate);

    auto task =
        StartMergeTask(kSrc, origin.AsScriptOrigin(), cached_data.get());

    // Attach a DebugDelegate and a DebugInfo to uncompiled `f` before
    // CompleteMergeInForeground (matching DevTools attach mid-merge).
    v8::debug::DebugDelegate debug_delegate;
    v8::debug::SetDebugDelegate(isolate(), &debug_delegate);
    int debugging_id = 0;
    int32_t unique_id = 0;
    {
      v8::HandleScope hs(isolate());
      Local<Function> f = context()
                              ->Global()
                              ->Get(context(), NewString("f"))
                              .ToLocalChecked()
                              .As<Function>();
      CHECK(!GetSharedFunctionInfo(f)->is_compiled());
      debugging_id = v8::debug::GetDebuggingId(f);
      CHECK_NE(0, debugging_id);
      unique_id = GetSharedFunctionInfo(f)->unique_id();
      CHECK(GetSharedFunctionInfo(f)->HasDebugInfo(i_isolate));
      CHECK(!GetSharedFunctionInfo(f)->is_compiled());
    }

    ScriptCompiler::Source source(NewString(kSrc), origin.AsScriptOrigin(),
                                  cached_data.release(), task.release());
    Local<Script> merged =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!source.GetCachedData()->rejected);
    CHECK_EQ(old_script_id, merged->GetUnboundScript()->ScriptId());

    {
      v8::HandleScope hs(isolate());
      Local<Function> f = context()
                              ->Global()
                              ->Get(context(), NewString("f"))
                              .ToLocalChecked()
                              .As<Function>();
      CHECK(!GetSharedFunctionInfo(f)->is_compiled());
      CHECK(GetSharedFunctionInfo(f)->HasDebugInfo(i_isolate));
      CHECK_EQ(debugging_id, v8::debug::GetDebuggingId(f));
      CHECK_EQ(unique_id, GetSharedFunctionInfo(f)->unique_id());
    }

    CHECK(!merged->Run(context()).IsEmpty());
    CHECK_EQ(42, RunGlobalFunc("f")->Int32Value(context()).FromJust());

    {
      v8::HandleScope hs(isolate());
      Local<Function> f = context()
                              ->Global()
                              ->Get(context(), NewString("f"))
                              .ToLocalChecked()
                              .As<Function>();
      CHECK(GetSharedFunctionInfo(f)->HasDebugInfo(i_isolate));
      CHECK_EQ(debugging_id, v8::debug::GetDebuggingId(f));
      CHECK_EQ(unique_id, GetSharedFunctionInfo(f)->unique_id());
    }

    // Detaching the debugger runs Debug::Unload() and must cleanly delete
    // `f`'s DebugInfo from DebugInfoCollection without hitting
    // SBXCHECK(it != map_.end()) from a clobbered unique_id.
    v8::debug::SetDebugDelegate(isolate(), nullptr);
    {
      v8::HandleScope hs(isolate());
      Local<Function> f = context()
                              ->Global()
                              ->Get(context(), NewString("f"))
                              .ToLocalChecked()
                              .As<Function>();
      CHECK(!GetSharedFunctionInfo(f)->HasDebugInfo(i_isolate));
    }
  }
}

// Regression test: if coverage mode is toggled while an off-thread merge task
// is in flight (even if toggled back to kBestEffort before Compile finishes),
// FinishOffThreadDeserialize must abort the merge rather than merging scripts
// compiled under different coverage settings.
TEST_F(MergeDeserializedCodeTest, AbortOffThreadMergeWhenCoverageModeChanges) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.flush_bytecode.Set(true);

  constexpr char kSrc[] =
      "function outer() { { let y = 12; try {} catch { return () => y; } "
      "return y; } } globalThis.outer = outer;";
  constexpr char kUrl[] = "http://example.com/coverage_merge.js";
  auto cached_data = MakeCodeCache(kSrc, kUrl, "outer()");

  {
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    PersistentScriptOrigin origin(isolate(), NewString(kUrl));
    int old_script_id = 0;
    {
      v8::HandleScope hs(isolate());
      ScriptOrigin script_origin = origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSrc), &script_origin)
              .ToLocalChecked();
      old_script_id = script->GetUnboundScript()->ScriptId();
      CHECK(!script->Run(context()).IsEmpty());
      i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
    }
    FlushWithMajorGC(i_isolate);

    auto task =
        StartMergeTask(kSrc, origin.AsScriptOrigin(), cached_data.get());

    // Toggle block coverage ON, compile `outer` with block coverage, then
    // toggle coverage back OFF before finishing the in-flight merge task.
    debug::Coverage::SelectMode(isolate(), debug::CoverageMode::kBlockCount);
    CHECK_EQ(12, RunGlobalFunc("outer")->Int32Value(context()).FromJust());
    debug::Coverage::SelectMode(isolate(), debug::CoverageMode::kBestEffort);

    ScriptCompiler::Source source(NewString(kSrc), origin.AsScriptOrigin(),
                                  cached_data.release(), task.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK_NE(old_script_id, script->GetUnboundScript()->ScriptId());
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(12, RunGlobalFunc("outer")->Int32Value(context()).FromJust());
  }
}

// Regression test: coverage mode changes and lazy compiles on old_script
// between SourceTextAvailable and MergeWithExistingScript (before BG merge
// runs) must also safely abort in FinishOffThreadDeserialize.
TEST_F(MergeDeserializedCodeTest,
       AbortOffThreadMergeWhenCoverageModeChangesBeforeMerge) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.flush_bytecode.Set(true);

  constexpr char kSrc[] =
      "function outer() { { let y = 12; try {} catch { return () => y; } "
      "return y; } } globalThis.outer = outer;";
  constexpr char kUrl[] = "http://example.com/coverage_merge_before_bg.js";
  auto cached_data = MakeCodeCache(kSrc, kUrl, "outer()");

  {
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    PersistentScriptOrigin origin(isolate(), NewString(kUrl));
    int old_script_id = 0;
    {
      v8::HandleScope hs(isolate());
      ScriptOrigin script_origin = origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSrc), &script_origin)
              .ToLocalChecked();
      old_script_id = script->GetUnboundScript()->ScriptId();
      CHECK(!script->Run(context()).IsEmpty());
      i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
    }
    FlushWithMajorGC(i_isolate);

    auto task = StartDeserializeBeforeMerge(kSrc, origin.AsScriptOrigin(),
                                            cached_data.get());

    // Toggle block coverage ON and compile `outer` BEFORE
    // MergeWithExistingScript runs on the background thread, then toggle back
    // to kBestEffort.
    debug::Coverage::SelectMode(isolate(), debug::CoverageMode::kBlockCount);
    CHECK_EQ(12, RunGlobalFunc("outer")->Int32Value(context()).FromJust());
    debug::Coverage::SelectMode(isolate(), debug::CoverageMode::kBestEffort);

    MergeThread merge_thread(task.get());
    CHECK(merge_thread.Start());
    merge_thread.Join();

    ScriptCompiler::Source source(NewString(kSrc), origin.AsScriptOrigin(),
                                  cached_data.release(), task.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK_NE(old_script_id, script->GetUnboundScript()->ScriptId());
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(12, RunGlobalFunc("outer")->Int32Value(context()).FromJust());
  }
}

// Regression test: bytecode compiled while coverage is enabled survives after
// toggling coverage back off, so CodeSerializer must not serialize it into a
// code cache that a fresh isolate could later merge into a flushed SFI.
TEST_F(MergeDeserializedCodeTest, NoCodeCacheAfterCoverageModeChange) {
  i::v8_flags.merge_background_deserialized_script_with_compilation_cache.Set(
      true);
  i::v8_flags.flush_bytecode.Set(true);

  constexpr char kSrc[] =
      "function mk() {"
      "  return function outer() {"
      "    'use strict';"
      "    function unused() { return 1; }"
      "    return 12;"
      "  };"
      "}"
      "globalThis.mk = mk;";
  constexpr char kUrl[] = "http://example.com/coverage_code_cache.js";

  // Produce a code cache after toggling precise coverage ON, compiling `outer`
  // (which emits CreateClosure for `unused`), and toggling coverage back OFF.
  std::unique_ptr<ScriptCompiler::CachedData> cached_data;
  {
    IsolateAndContextScope scope(this);
    v8::HandleScope hs(isolate());
    debug::Coverage::SelectMode(isolate(), debug::CoverageMode::kPreciseCount);
    ScriptOrigin origin(NewString(kUrl));
    Local<Script> script =
        Script::Compile(context(), NewString(kSrc), &origin).ToLocalChecked();
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(12, Eval("mk()()")->Int32Value(context()).FromJust());
    debug::Coverage::SelectMode(isolate(), debug::CoverageMode::kBestEffort);
    cached_data.reset(
        ScriptCompiler::CreateCodeCache(script->GetUnboundScript()));
    CHECK_NOT_NULL(cached_data);
    CHECK_EQ(i::SerializedCodeData::kHeaderSize,
             static_cast<uint32_t>(cached_data->length));
  }

  // Consume the cache in a fresh isolate where `outer` has a live closure with
  // a 0-length ClosureFeedbackCellArray and its SFI has been flushed.
  {
    IsolateAndContextScope scope(this);
    i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate());
    PersistentScriptOrigin origin(isolate(), NewString(kUrl));
    int old_script_id = -1;
    {
      v8::HandleScope hs(isolate());
      ScriptOrigin script_origin = origin.AsScriptOrigin();
      Local<Script> script =
          Script::Compile(context(), NewString(kSrc), &script_origin)
              .ToLocalChecked();
      old_script_id = script->GetUnboundScript()->ScriptId();
      CHECK(!script->Run(context()).IsEmpty());
      CHECK_EQ(
          12,
          Eval("globalThis.o = mk(); o();")->Int32Value(context()).FromJust());
      i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(script));
      Local<Function> o = context()
                              ->Global()
                              ->Get(context(), NewString("o"))
                              .ToLocalChecked()
                              .As<Function>();
      i::SharedFunctionInfo::EnsureOldForTesting(GetSharedFunctionInfo(o));
    }
    FlushWithMajorGC(i_isolate);

    {
      v8::HandleScope hs(isolate());
      Local<Function> o = context()
                              ->Global()
                              ->Get(context(), NewString("o"))
                              .ToLocalChecked()
                              .As<Function>();
      CHECK(!GetSharedFunctionInfo(o)->is_compiled());
    }

    ScriptCompiler::Source source(NewString(kSrc), origin.AsScriptOrigin(),
                                  cached_data.release());
    Local<Script> script =
        ScriptCompiler::Compile(context(), &source,
                                ScriptCompiler::kConsumeCodeCache)
            .ToLocalChecked();
    CHECK(!source.GetCachedData()->rejected);
    CHECK_EQ(ScriptCompiler::InMemoryCacheResult::kPartial,
             source.GetCompilationDetails().in_memory_cache_result);
    CHECK_EQ(old_script_id, script->GetUnboundScript()->ScriptId());
    // Coverage-compiled bytecode must not have been serialized and merged into
    // `o`; `o` should remain uncompiled and compile lazily under best-effort.
    {
      v8::HandleScope hs(isolate());
      Local<Function> o = context()
                              ->Global()
                              ->Get(context(), NewString("o"))
                              .ToLocalChecked()
                              .As<Function>();
      CHECK(!GetSharedFunctionInfo(o)->is_compiled());
    }
    CHECK(!script->Run(context()).IsEmpty());
    CHECK_EQ(12, RunGlobalFunc("o")->Int32Value(context()).FromJust());
  }
}

}  // namespace v8
