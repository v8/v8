// Copyright 2006-2009 the V8 project authors. All rights reserved.
//
// Tests of profiler-related functions from log.h

#include <stdlib.h>

#include "v8.h"

#include "log.h"
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


static v8::Persistent<v8::Context> env;


static struct {
  StackTracer* tracer;
  TickSample* sample;
} trace_env;


static void InitTraceEnv(StackTracer* tracer, TickSample* sample) {
  trace_env.tracer = tracer;
  trace_env.sample = sample;
}


static void DoTrace(unsigned int fp) {
  trace_env.sample->fp = fp;
  // something that is less than fp
  trace_env.sample->sp = trace_env.sample->fp - sizeof(unsigned int);
  trace_env.tracer->Trace(trace_env.sample);
}


static void CFuncDoTrace() {
  unsigned int fp;
#ifdef __GNUC__
  fp = reinterpret_cast<unsigned int>(__builtin_frame_address(0));
#elif defined _MSC_VER
  __asm mov [fp], ebp
#endif
  DoTrace(fp);
}


static void CFunc(int i) {
  for (int j = i; j >= 0; --j) {
    CFuncDoTrace();
  }
}


static void CheckRetAddrIsInFunction(unsigned int ret_addr,
                                     unsigned int func_start_addr,
                                     unsigned int func_len) {
  printf("CheckRetAddrIsInFunction: %08x %08x %08x\n",
         func_start_addr, ret_addr, func_start_addr + func_len);
  CHECK_GE(ret_addr, func_start_addr);
  CHECK_GE(func_start_addr + func_len, ret_addr);
}


#ifdef DEBUG
static const int kMaxCFuncLen = 0x40; // seems enough for a small C function

static void CheckRetAddrIsInCFunction(unsigned int ret_addr,
                                      unsigned int func_start_addr) {
  CheckRetAddrIsInFunction(ret_addr, func_start_addr, kMaxCFuncLen);
}
#endif


TEST(PureCStackTrace) {
  TickSample sample;
  StackTracer tracer(reinterpret_cast<unsigned int>(&sample));
  InitTraceEnv(&tracer, &sample);
  CFunc(0);
#ifdef DEBUG
  // C stack trace works only in debug mode, in release mode EBP is
  // usually treated as a general-purpose register
  CheckRetAddrIsInCFunction(reinterpret_cast<unsigned int>(sample.stack[0]),
                            reinterpret_cast<unsigned int>(&CFunc));
  CHECK_EQ(0, sample.stack[1]);
#endif
}


// --- T r a c e   E x t e n s i o n ---

class TraceExtension : public v8::Extension {
 public:
  TraceExtension() : v8::Extension("v8/trace", kSource) { }
  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<v8::String> name);
  static v8::Handle<v8::Value> Trace(const v8::Arguments& args);
 private:
  static const char* kSource;
};


const char* TraceExtension::kSource = "native function trace();";


v8::Handle<v8::FunctionTemplate> TraceExtension::GetNativeFunction(
    v8::Handle<v8::String> str) {
  return v8::FunctionTemplate::New(TraceExtension::Trace);
}


v8::Handle<v8::Value> TraceExtension::Trace(const v8::Arguments& args) {
  CHECK_EQ(1, args.Length());
  unsigned int fp = args[0]->Int32Value() << 2;
  printf("Trace: %08x\n", fp);
  DoTrace(fp);
  return v8::Undefined();
}


static TraceExtension kTraceExtension;
v8::DeclareExtension kTraceExtensionDeclaration(&kTraceExtension);


static void InitializeVM() {
  if (env.IsEmpty()) {
    v8::HandleScope scope;
    const char* extensions[] = { "v8/trace" };
    v8::ExtensionConfiguration config(1, extensions);
    env = v8::Context::New(&config);
  }
  v8::HandleScope scope;
  env->Enter();
}


static Handle<JSFunction> CompileFunction(const char* source) {
  return v8::Utils::OpenHandle(*Script::Compile(String::New(source)));
}


static void CompileRun(const char* source) {
  Script::Compile(String::New(source))->Run();
}


static Local<Value> GetGlobalProperty(const char* name) {
  return env->Global()->Get(String::New(name));
}


static void SetGlobalProperty(const char* name, Local<Value> value) {
  env->Global()->Set(String::New(name), value);
}


static bool Patch(byte* from, size_t num, byte* original, byte* patch, size_t patch_len) {
  byte* to = from + num;
  do {
    from = (byte*)memchr(from, *original, to - from);
    CHECK(from != NULL);
    if (memcmp(original, from, patch_len) == 0) {
      memcpy(from, patch, patch_len);
      return true;
    } else {
      from++;
    }
  } while (to - from > 0);
  return false;
}


TEST(PureJSStackTrace) {
  TickSample sample;
  StackTracer tracer(reinterpret_cast<unsigned int>(&sample));
  InitTraceEnv(&tracer, &sample);

  InitializeVM();
  v8::HandleScope scope;
  Handle<JSFunction> call_trace = CompileFunction("trace(0x6666);");
  CHECK(!call_trace.is_null());
  v8::internal::Code* call_trace_code = call_trace->code();
  CHECK(call_trace_code->IsCode());

  byte original[] = { 0x68, 0xcc, 0xcc, 0x00, 0x00 }; // push 0xcccc (= 0x6666 << 1)
  byte patch[] = { 0x89, 0xe8, 0xd1, 0xe8, 0x50 }; // mov eax,ebp; shr eax; push eax;
  // Patch generated code to replace pushing of a constant with
  // pushing of ebp contents in a Smi
  CHECK(Patch(call_trace_code->instruction_start(),
              call_trace_code->instruction_size(),
              original, patch, sizeof(patch)));

  SetGlobalProperty("JSFuncDoTrace", v8::ToApi<Value>(call_trace));

  CompileRun(
      "function JSTrace() {"
      "  JSFuncDoTrace();"
      "};\n"
      "JSTrace();");
  Handle<JSFunction> js_trace(JSFunction::cast(*(v8::Utils::OpenHandle(
      *GetGlobalProperty("JSTrace")))));
  v8::internal::Code* js_trace_code = js_trace->code();
  CheckRetAddrIsInFunction(reinterpret_cast<unsigned int>(sample.stack[0]),
                           reinterpret_cast<unsigned int>(js_trace_code->instruction_start()),
                           js_trace_code->instruction_size());
  CHECK_EQ(0, sample.stack[1]);
}
