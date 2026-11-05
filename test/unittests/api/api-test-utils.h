// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_API_API_TEST_UTILS_H_
#define V8_UNITTESTS_API_API_TEST_UTILS_H_

#include <cstring>

#include "include/libplatform/libplatform.h"
#include "include/v8-array-buffer.h"
#include "include/v8-context.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-primitive.h"
#include "include/v8-script.h"
#include "include/v8-unwinder.h"
#include "src/api/api-inl.h"
#include "src/debug/debug-interface.h"
#include "src/execution/isolate.h"
#include "src/init/v8.h"
#include "test/unittests/test-utils.h"

namespace v8 {

// -----------------------------------------------------------------------------
// Context and script helpers that work on the isolate entered on the current
// thread.

// A LocalContext holds a reference to a v8::Context and enters it for its
// lifetime.
class LocalContext {
 public:
  LocalContext(v8::Isolate* isolate,
               v8::ExtensionConfiguration* extensions = nullptr,
               v8::Local<v8::ObjectTemplate> global_template =
                   v8::Local<v8::ObjectTemplate>(),
               v8::Local<v8::Value> global_object = v8::Local<v8::Value>())
      : isolate_(isolate) {
    Initialize(extensions, global_template, global_object);
  }

  LocalContext(v8::ExtensionConfiguration* extensions = nullptr,
               v8::Local<v8::ObjectTemplate> global_template =
                   v8::Local<v8::ObjectTemplate>(),
               v8::Local<v8::Value> global_object = v8::Local<v8::Value>())
      : LocalContext(v8::Isolate::GetCurrent(), extensions, global_template,
                     global_object) {}

  virtual ~LocalContext() {
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Context>::New(isolate_, context_)->Exit();
    context_.Reset();
  }

  v8::Context* operator->() { return i::ValueHelper::HandleAsValue(context_); }
  v8::Context* operator*() { return operator->(); }
  bool IsReady() { return !context_.IsEmpty(); }

  v8::Isolate* isolate() const { return isolate_; }
  i::Isolate* i_isolate() const {
    return reinterpret_cast<i::Isolate*>(isolate_);
  }
  v8::Local<v8::Context> local() const {
    return v8::Local<v8::Context>::New(isolate_, context_);
  }

 private:
  void Initialize(v8::ExtensionConfiguration* extensions,
                  v8::Local<v8::ObjectTemplate> global_template,
                  v8::Local<v8::Value> global_object) {
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Context> context =
        v8::Context::New(isolate_, extensions, global_template, global_object);
    context_.Reset(isolate_, context);
    context->Enter();
  }

  v8::Persistent<v8::Context> context_;
  v8::Isolate* const isolate_;
};

inline v8::Local<v8::Number> v8_num(double x) {
  return v8::Number::New(v8::Isolate::GetCurrent(), x);
}

inline v8::Local<v8::Integer> v8_int(int32_t x) {
  return v8::Integer::New(v8::Isolate::GetCurrent(), x);
}

inline v8::Local<v8::Integer> v8_uint(uint32_t x) {
  return v8::Integer::NewFromUnsigned(v8::Isolate::GetCurrent(), x);
}

inline v8::Local<v8::String> v8_str(const char* x) {
  return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), x).ToLocalChecked();
}

inline v8::Local<v8::String> v8_str(v8::Isolate* isolate, const char* x) {
  return v8::String::NewFromUtf8(isolate, x).ToLocalChecked();
}

inline v8::Local<v8::Symbol> v8_symbol(const char* name) {
  return v8::Symbol::New(v8::Isolate::GetCurrent(), v8_str(name));
}

inline v8::Local<v8::Script> v8_compile(v8::Local<v8::String> x) {
  v8::Local<v8::Script> result;
  CHECK(v8::Script::Compile(v8::Isolate::GetCurrent()->GetCurrentContext(), x)
            .ToLocal(&result));
  return result;
}

inline v8::Local<v8::Script> v8_compile(const char* x) {
  return v8_compile(v8_str(x));
}

inline v8::MaybeLocal<v8::Script> v8_try_compile(v8::Local<v8::String> x) {
  return v8::Script::Compile(v8::Isolate::GetCurrent()->GetCurrentContext(), x);
}

inline v8::MaybeLocal<v8::Script> v8_try_compile(const char* x) {
  return v8_try_compile(v8_str(x));
}

inline v8::Local<v8::Script> CompileWithOrigin(v8::Local<v8::String> source,
                                               v8::Local<v8::String> origin_url,
                                               bool is_shared_cross_origin) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::ScriptOrigin origin(origin_url, 0, 0, is_shared_cross_origin);
  v8::ScriptCompiler::Source script_source(source, origin);
  return v8::ScriptCompiler::Compile(isolate->GetCurrentContext(),
                                     &script_source)
      .ToLocalChecked();
}

inline v8::Local<v8::Script> CompileWithOrigin(v8::Local<v8::String> source,
                                               const char* origin_url,
                                               bool is_shared_cross_origin) {
  return CompileWithOrigin(source, v8_str(origin_url), is_shared_cross_origin);
}

inline v8::Local<v8::Script> CompileWithOrigin(const char* source,
                                               const char* origin_url,
                                               bool is_shared_cross_origin) {
  return CompileWithOrigin(v8_str(source), v8_str(origin_url),
                           is_shared_cross_origin);
}

// Helper functions that compile and run the source.
inline v8::MaybeLocal<v8::Value> CompileRun(v8::Local<v8::Context> context,
                                            const char* source) {
  return v8::Script::Compile(context, v8_str(source))
      .ToLocalChecked()
      ->Run(context);
}

inline v8::Local<v8::Value> CompileRunChecked(v8::Isolate* isolate,
                                              const char* source) {
  v8::Local<v8::String> source_string =
      v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, source_string).ToLocalChecked();
  return script->Run(context).ToLocalChecked();
}

inline v8::Local<v8::Value> CompileRunChecked(const char* source) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  return CompileRunChecked(isolate, source);
}

// Unlike RunJS() in test-utils.h, CompileRun() returns an empty handle instead
// of crashing if the script throws.
inline v8::Local<v8::Value> CompileRun(v8::Local<v8::String> source) {
  v8::Local<v8::Value> result;
  if (v8_compile(source)
          ->Run(v8::Isolate::GetCurrent()->GetCurrentContext())
          .ToLocal(&result)) {
    return result;
  }
  return v8::Local<v8::Value>();
}

inline v8::Local<v8::Value> CompileRun(const char* source) {
  return CompileRun(v8_str(source));
}

inline int32_t v8_run_int32value(v8::Local<v8::Script> script) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  return script->Run(context).ToLocalChecked()->Int32Value(context).FromJust();
}

inline int32_t v8_run_int32(const char* source) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  return CompileRunChecked(isolate, source)->Int32Value(context).FromJust();
}

inline bool v8_run_bool(const char* source) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  return CompileRunChecked(isolate, source)->BooleanValue(isolate);
}

inline v8::Local<v8::Value> CompileRun(
    v8::Local<v8::Context> context, v8::ScriptCompiler::Source* script_source,
    v8::ScriptCompiler::CompileOptions options) {
  v8::Local<v8::Value> result;
  if (v8::ScriptCompiler::Compile(context, script_source, options)
          .ToLocalChecked()
          ->Run(context)
          .ToLocal(&result)) {
    return result;
  }
  return v8::Local<v8::Value>();
}

// Helper functions that compile and run the source with given origin.
inline v8::Local<v8::Value> CompileRunWithOrigin(const char* source,
                                                 const char* origin_url,
                                                 int line_number,
                                                 int column_number) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::ScriptOrigin origin(v8_str(origin_url), line_number, column_number);
  v8::ScriptCompiler::Source script_source(v8_str(source), origin);
  return CompileRun(context, &script_source,
                    v8::ScriptCompiler::CompileOptions());
}

inline v8::Local<v8::Value> CompileRunWithOrigin(v8::Local<v8::String> source,
                                                 const char* origin_url) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::ScriptCompiler::Source script_source(
      source, v8::ScriptOrigin(v8_str(origin_url)));
  return CompileRun(context, &script_source,
                    v8::ScriptCompiler::CompileOptions());
}

inline v8::Local<v8::Value> CompileRunWithOrigin(const char* source,
                                                 const char* origin_url) {
  return CompileRunWithOrigin(v8_str(source), origin_url);
}

inline void ExpectString(const char* code, const char* expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsString());
  v8::String::Utf8Value utf8(v8::Isolate::GetCurrent(), result);
  CHECK_EQ(0, strcmp(expected, *utf8));
}

inline void ExpectInt32(const char* code, int expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsInt32());
  CHECK_EQ(expected,
           result->Int32Value(v8::Isolate::GetCurrent()->GetCurrentContext())
               .FromJust());
}

inline void ExpectBoolean(const char* code, bool expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsBoolean());
  CHECK_EQ(expected, result->BooleanValue(v8::Isolate::GetCurrent()));
}

inline void ExpectTrue(const char* code) { ExpectBoolean(code, true); }

inline void ExpectFalse(const char* code) { ExpectBoolean(code, false); }

inline void ExpectObject(const char* code, v8::Local<v8::Value> expected) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->SameValue(expected));
}

inline void ExpectUndefined(const char* code) {
  v8::Local<v8::Value> result = CompileRun(code);
  CHECK(result->IsUndefined());
}

inline void CheckDoubleEquals(double expected, double actual) {
  const double kEpsilon = 1e-10;
  CHECK_LE(expected, actual + kEpsilon);
  CHECK_GE(expected, actual - kEpsilon);
}

inline void EnableDebugger(v8::Isolate* isolate) {
  static v8::debug::DebugDelegate dummy_delegate;
  v8::debug::SetDebugDelegate(isolate, &dummy_delegate);
}

inline void DisableDebugger(v8::Isolate* isolate) {
  v8::debug::SetDebugDelegate(isolate, nullptr);
}

inline void EmptyMessageQueues(v8::Isolate* isolate) {
  while (v8::platform::PumpMessageLoop(i::V8::GetCurrentPlatform(), isolate)) {
  }
}

// Process-wide ArrayBuffer allocator shared by all isolates that tests create
// themselves (e.g. in TestWithPlatform-based fixtures).
inline v8::ArrayBuffer::Allocator* DefaultArrayBufferAllocator() {
  static v8::ArrayBuffer::Allocator* allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  return allocator;
}

}  // namespace v8

#endif  // V8_UNITTESTS_API_API_TEST_UTILS_H_
