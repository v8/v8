// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/assembler.h"
#include "src/code-stubs.h"
#include "src/compiler/linkage.h"
#include "src/compiler/linkage-impl.h"
#include "src/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

struct X64LinkageHelperTraits {
  static Register ReturnValueReg() { return rax; }
  static Register ReturnValue2Reg() { return rdx; }
  static Register JSCallFunctionReg() { return rdi; }
  static Register ContextReg() { return rsi; }
  static Register InterpreterBytecodeOffsetReg() { return r12; }
  static Register InterpreterBytecodeArrayReg() { return r14; }
  static Register InterpreterDispatchTableReg() { return r15; }
  static Register RuntimeCallFunctionReg() { return rbx; }
  static Register RuntimeCallArgCountReg() { return rax; }
};

typedef LinkageHelper<X64LinkageHelperTraits> LH;

CallDescriptor* Linkage::GetJSCallDescriptor(Zone* zone, bool is_osr,
                                             int parameter_count,
                                             CallDescriptor::Flags flags) {
  return LH::GetJSCallDescriptor(zone, is_osr, parameter_count, flags);
}


CallDescriptor* Linkage::GetRuntimeCallDescriptor(
    Zone* zone, Runtime::FunctionId function, int parameter_count,
    Operator::Properties properties) {
  return LH::GetRuntimeCallDescriptor(zone, function, parameter_count,
                                      properties);
}


CallDescriptor* Linkage::GetStubCallDescriptor(
    Isolate* isolate, Zone* zone, const CallInterfaceDescriptor& descriptor,
    int stack_parameter_count, CallDescriptor::Flags flags,
    Operator::Properties properties, MachineType return_type) {
  return LH::GetStubCallDescriptor(isolate, zone, descriptor,
                                   stack_parameter_count, flags, properties,
                                   return_type);
}


CallDescriptor* Linkage::GetInterpreterDispatchDescriptor(Zone* zone) {
  return LH::GetInterpreterDispatchDescriptor(zone);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
