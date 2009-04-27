// Copyright 2006-2009 the V8 project authors. All rights reserved.
//
// Tests of profiler-related functions from log.h

#ifdef ENABLE_LOGGING_AND_PROFILING

#include <stdlib.h>

#include "v8.h"

#include "log.h"
#include "top.h"
#include "cctest.h"

using v8::Function;
using v8::Local;
using v8::Object;
using v8::Script;
using v8::String;
using v8::Value;

using v8::internal::byte;
using v8::internal::Handle;
using v8::internal::JSFunction;
using v8::internal::StackTracer;
using v8::internal::TickSample;
using v8::internal::Top;


static v8::Persistent<v8::Context> env;


static struct {
  StackTracer* tracer;
  TickSample* sample;
} trace_env = { NULL, NULL };


static void InitTraceEnv(StackTracer* tracer, TickSample* sample) {
  trace_env.tracer = tracer;
  trace_env.sample = sample;
}


static void DoTrace(unsigned int fp) {
  trace_env.sample->fp = fp;
  // sp is only used to define stack high bound
  trace_env.sample->sp =
      reinterpret_cast<unsigned int>(trace_env.sample) - 10240;
  trace_env.tracer->Trace(trace_env.sample);
}


// Hide c_entry_fp to emulate situation when sampling is done while
// pure JS code is being executed
static void DoTraceHideCEntryFPAddress(unsigned int fp) {
  v8::internal::Address saved_c_frame_fp = *(Top::c_entry_fp_address());
  CHECK(saved_c_frame_fp);
  *(Top::c_entry_fp_address()) = 0;
  DoTrace(fp);
  *(Top::c_entry_fp_address()) = saved_c_frame_fp;
}


// --- T r a c e   E x t e n s i o n ---

class TraceExtension : public v8::Extension {
 public:
  TraceExtension() : v8::Extension("v8/trace", kSource) { }
  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<String> name);
  static v8::Handle<v8::Value> Trace(const v8::Arguments& args);
  static v8::Handle<v8::Value> JSTrace(const v8::Arguments& args);
 private:
  static unsigned int GetFP(const v8::Arguments& args);
  static const char* kSource;
};


const char* TraceExtension::kSource =
    "native function trace();"
    "native function js_trace();";


v8::Handle<v8::FunctionTemplate> TraceExtension::GetNativeFunction(
    v8::Handle<String> name) {
  if (name->Equals(String::New("trace"))) {
    return v8::FunctionTemplate::New(TraceExtension::Trace);
  } else if (name->Equals(String::New("js_trace"))) {
    return v8::FunctionTemplate::New(TraceExtension::JSTrace);
  } else {
    CHECK(false);
    return v8::Handle<v8::FunctionTemplate>();
  }
}


unsigned int TraceExtension::GetFP(const v8::Arguments& args) {
  CHECK_EQ(1, args.Length());
  unsigned int fp = args[0]->Int32Value() << 2;
  printf("Trace: %08x\n", fp);
  return fp;
}


v8::Handle<v8::Value> TraceExtension::Trace(const v8::Arguments& args) {
  DoTrace(GetFP(args));
  return v8::Undefined();
}


v8::Handle<v8::Value> TraceExtension::JSTrace(const v8::Arguments& args) {
  DoTraceHideCEntryFPAddress(GetFP(args));
  return v8::Undefined();
}


static TraceExtension kTraceExtension;
v8::DeclareExtension kTraceExtensionDeclaration(&kTraceExtension);


static void CFuncDoTrace() {
  unsigned int fp;
#ifdef __GNUC__
  fp = reinterpret_cast<unsigned int>(__builtin_frame_address(0));
#elif defined _MSC_VER
  __asm mov [fp], ebp  // NOLINT
#endif
  DoTrace(fp);
}


static int CFunc(int depth) {
  if (depth <= 0) {
    CFuncDoTrace();
    return 0;
  } else {
    return CFunc(depth - 1) + 1;
  }
}


TEST(PureCStackTrace) {
  TickSample sample;
  StackTracer tracer(reinterpret_cast<unsigned int>(&sample));
  InitTraceEnv(&tracer, &sample);
  // Check that sampler doesn't crash
  CHECK_EQ(10, CFunc(10));
}


#endif  // ENABLE_LOGGING_AND_PROFILING
