// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return rsi; }


const Register LoadDescriptor::ReceiverRegister() { return rdx; }
const Register LoadDescriptor::NameRegister() { return rcx; }


const Register VectorLoadICDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}


const Register VectorLoadICDescriptor::NameRegister() {
  return LoadDescriptor::NameRegister();
}


const Register VectorLoadICDescriptor::SlotRegister() { return rax; }
const Register VectorLoadICDescriptor::VectorRegister() { return rbx; }


const Register StoreDescriptor::ReceiverRegister() { return rdx; }
const Register StoreDescriptor::NameRegister() { return rcx; }
const Register StoreDescriptor::ValueRegister() { return rax; }


const Register ElementTransitionAndStoreDescriptor::ReceiverRegister() {
  return StoreDescriptor::ReceiverRegister();
}


const Register ElementTransitionAndStoreDescriptor::NameRegister() {
  return StoreDescriptor::NameRegister();
}


const Register ElementTransitionAndStoreDescriptor::ValueRegister() {
  return StoreDescriptor::ValueRegister();
}


const Register ElementTransitionAndStoreDescriptor::MapRegister() {
  return rbx;
}


const Register InstanceofDescriptor::left() { return rax; }
const Register InstanceofDescriptor::right() { return rdx; }


void FastNewClosureDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rbx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastNewContextDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rdi};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToNumberDescriptor::Initialize(Isolate* isolate) {
  // ToNumberStub invokes a function, and therefore needs a context.
  Register registers[] = {rsi, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void NumberToStringDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastCloneShallowArrayDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rax, rbx, rcx};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(), Representation::Smi(),
      Representation::Tagged()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void FastCloneShallowObjectDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rax, rbx, rcx, rdx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CreateAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rbx, rdx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallFunctionDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rdi};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallConstructDescriptor::Initialize(Isolate* isolate) {
  // rax : number of arguments
  // rbx : feedback vector
  // rdx : (only if rbx is not the megamorphic symbol) slot in feedback
  //       vector (Smi)
  // rdi : constructor function
  // TODO(turbofan): So far we don't gather type feedback and hence skip the
  // slot parameter, but ArrayConstructStub needs the vector to be undefined.
  Register registers[] = {rsi, rax, rdi, rbx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void RegExpConstructResultDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rcx, rbx, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void TransitionElementsKindDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rax, rbx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorConstantArgCountDescriptor::Initialize(Isolate* isolate) {
  // register state
  // rax -- number of arguments
  // rdi -- function
  // rbx -- allocation site with elements kind
  Register registers[] = {rsi, rdi, rbx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {rsi, rdi, rbx, rax};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(),
      Representation::Tagged(), Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void InternalArrayConstructorConstantArgCountDescriptor::Initialize(
    Isolate* isolate) {
  // register state
  // rsi -- context
  // rax -- number of arguments
  // rdi -- constructor function
  Register registers[] = {rsi, rdi};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void InternalArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {rsi, rdi, rax};
  Representation representations[] = {Representation::Tagged(),
                                      Representation::Tagged(),
                                      Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void CompareNilDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToBooleanDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rdx, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpWithAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rcx, rdx, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void StringAddDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {rsi, rdx, rax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void KeyedDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {
      rsi,  // context
      rcx,  // key
  };
  Representation representations[] = {
      Representation::Tagged(),  // context
      Representation::Tagged(),  // key
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void NamedDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {
      rsi,  // context
      rcx,  // name
  };
  Representation representations[] = {
      Representation::Tagged(),  // context
      Representation::Tagged(),  // name
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void CallHandlerDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {
      rsi,  // context
      rdx,  // receiver
  };
  Representation representations[] = {
      Representation::Tagged(),  // context
      Representation::Tagged(),  // receiver
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void ArgumentAdaptorDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {
      rsi,  // context
      rdi,  // JSFunction
      rax,  // actual number of arguments
      rbx,  // expected number of arguments
  };
  Representation representations[] = {
      Representation::Tagged(),     // context
      Representation::Tagged(),     // JSFunction
      Representation::Integer32(),  // actual number of arguments
      Representation::Integer32(),  // expected number of arguments
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void ApiFunctionDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {
      rsi,  // context
      rax,  // callee
      rbx,  // call_data
      rcx,  // holder
      rdx,  // api_function_address
  };
  Representation representations[] = {
      Representation::Tagged(),    // context
      Representation::Tagged(),    // callee
      Representation::Tagged(),    // call_data
      Representation::Tagged(),    // holder
      Representation::External(),  // api_function_address
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X64
