// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CCTEST_COMPILER_CALL_TESTER_H_
#define V8_CCTEST_COMPILER_CALL_TESTER_H_

#include "src/handles.h"
#include "src/objects/code.h"
#include "src/simulator.h"
#include "test/cctest/compiler/c-signature.h"

#if V8_TARGET_ARCH_IA32
#if __GNUC__
#define V8_CDECL __attribute__((cdecl))
#else
#define V8_CDECL __cdecl
#endif
#else
#define V8_CDECL
#endif

namespace v8 {
namespace internal {
namespace compiler {

template <typename R>
class CallHelper {
 public:
  explicit CallHelper(Isolate* isolate, MachineSignature* csig)
      : csig_(csig), isolate_(isolate) {
    USE(isolate_);
  }
  virtual ~CallHelper() {}

  template <typename... Params>
  R Call(Params... args) {
    using FType = R(V8_CDECL*)(Params...);
    CSignature::VerifyParams<Params...>(csig_);
    return DoCall(FUNCTION_CAST<FType>(Generate()), args...);
  }

 protected:
  MachineSignature* csig_;

  virtual byte* Generate() = 0;

 private:
#if USE_SIMULATOR
  template <typename F, typename... Params>
  R DoCall(F* f, Params... args) {
    Simulator* simulator = Simulator::current(isolate_);
    return simulator->Call<R>(FUNCTION_ADDR(f), args...);
  }
#else
  template <typename F, typename... Params>
  R DoCall(F* f, Params... args) {
    return f(args...);
  }
#endif

  Isolate* isolate_;
};

// A call helper that calls the given code object assuming C calling convention.
template <typename T>
class CodeRunner : public CallHelper<T> {
 public:
  CodeRunner(Isolate* isolate, Handle<Code> code, MachineSignature* csig)
      : CallHelper<T>(isolate, csig), code_(code) {}
  virtual ~CodeRunner() {}

  virtual byte* Generate() { return code_->entry(); }

 private:
  Handle<Code> code_;
};


}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_COMPILER_CALL_TESTER_H_
