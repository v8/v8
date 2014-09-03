// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_MIPS

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return cp; }


const Register LoadDescriptor::ReceiverRegister() { return a1; }
const Register LoadDescriptor::NameRegister() { return a2; }


const Register VectorLoadICDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}


const Register VectorLoadICDescriptor::NameRegister() {
  return LoadDescriptor::NameRegister();
}


const Register VectorLoadICDescriptor::SlotRegister() { return a0; }
const Register VectorLoadICDescriptor::VectorRegister() { return a3; }


const Register StoreDescriptor::ReceiverRegister() { return a1; }
const Register StoreDescriptor::NameRegister() { return a2; }
const Register StoreDescriptor::ValueRegister() { return a0; }


const Register ElementTransitionAndStoreDescriptor::ReceiverRegister() {
  return StoreDescriptor::ReceiverRegister();
}


const Register ElementTransitionAndStoreDescriptor::NameRegister() {
  return StoreDescriptor::NameRegister();
}


const Register ElementTransitionAndStoreDescriptor::ValueRegister() {
  return StoreDescriptor::ValueRegister();
}


const Register ElementTransitionAndStoreDescriptor::MapRegister() { return a3; }


const Register InstanceofDescriptor::left() { return a0; }
const Register InstanceofDescriptor::right() { return a1; }


void FastNewClosureDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastNewContextDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToNumberDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void NumberToStringDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastCloneShallowArrayDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a3, a2, a1};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(), Representation::Smi(),
      Representation::Tagged()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void FastCloneShallowObjectDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a3, a2, a1, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CreateAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a2, a3};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallFunctionDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallConstructDescriptor::Initialize(Isolate* isolate) {
  // a0 : number of arguments
  // a1 : the function to call
  // a2 : feedback vector
  // a3 : (only if a2 is not the megamorphic symbol) slot in feedback
  //      vector (Smi)
  // TODO(turbofan): So far we don't gather type feedback and hence skip the
  // slot parameter, but ArrayConstructStub needs the vector to be undefined.
  Register registers[] = {cp, a0, a1, a2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void RegExpConstructResultDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a2, a1, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void TransitionElementsKindDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a0, a1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorConstantArgCountDescriptor::Initialize(Isolate* isolate) {
  // register state
  // cp -- context
  // a0 -- number of arguments
  // a1 -- function
  // a2 -- allocation site with elements kind
  Register registers[] = {cp, a1, a2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {cp, a1, a2, a0};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(),
      Representation::Tagged(), Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void InternalArrayConstructorConstantArgCountDescriptor::Initialize(
    Isolate* isolate) {
  // register state
  // cp -- context
  // a0 -- number of arguments
  // a1 -- constructor function
  Register registers[] = {cp, a1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void InternalArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {cp, a1, a0};
  Representation representations[] = {Representation::Tagged(),
                                      Representation::Tagged(),
                                      Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void CompareNilDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToBooleanDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a1, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpWithAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a2, a1, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void StringAddDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, a1, a0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void KeyedDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {
      cp,  // context
      a2,  // key
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
      cp,  // context
      a2,  // name
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
      cp,  // context
      a0,  // receiver
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
      cp,  // context
      a1,  // JSFunction
      a0,  // actual number of arguments
      a2,  // expected number of arguments
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
      cp,  // context
      a0,  // callee
      t0,  // call_data
      a2,  // holder
      a1,  // api_function_address
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

#endif  // V8_TARGET_ARCH_MIPS
