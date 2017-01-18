// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FFI_FFI_COMPILER_H_
#define SRC_FFI_FFI_COMPILER_H_

#include "src/code-stub-assembler.h"
#include "src/machine-type.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;
typedef compiler::CodeAssemblerState CodeAssemblerState;

void InstallFFIMap(Isolate* isolate);

namespace ffi {

typedef Signature<MachineType> FFISignature;

struct NativeFunction {
  FFISignature* sig;
  uint8_t* start;
};

Handle<JSFunction> CompileJSToNativeWrapper(Isolate* isolate,
                                            Handle<String> name,
                                            NativeFunction func);
}  // namespace ffi
}  // namespace internal
}  // namespace v8

#endif  // SRC_FFI_FFI_COMPILER_H_
