// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_ARM

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return cp; }


const Register LoadDescriptor::ReceiverRegister() { return r1; }
const Register LoadDescriptor::NameRegister() { return r2; }


const Register VectorLoadICDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}


const Register VectorLoadICDescriptor::NameRegister() {
  return LoadDescriptor::NameRegister();
}


const Register VectorLoadICDescriptor::SlotRegister() { return r0; }
const Register VectorLoadICDescriptor::VectorRegister() { return r3; }


const Register StoreDescriptor::ReceiverRegister() { return r1; }
const Register StoreDescriptor::NameRegister() { return r2; }
const Register StoreDescriptor::ValueRegister() { return r0; }


const Register ElementTransitionAndStoreDescriptor::ReceiverRegister() {
  return StoreDescriptor::ReceiverRegister();
}


const Register ElementTransitionAndStoreDescriptor::NameRegister() {
  return StoreDescriptor::NameRegister();
}


const Register ElementTransitionAndStoreDescriptor::ValueRegister() {
  return StoreDescriptor::ValueRegister();
}


const Register ElementTransitionAndStoreDescriptor::MapRegister() { return r3; }


const Register InstanceofDescriptor::left() { return r0; }
const Register InstanceofDescriptor::right() { return r1; }


void FastNewClosureDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastNewContextDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToNumberDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void NumberToStringDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastCloneShallowArrayDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r3, r2, r1};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(), Representation::Smi(),
      Representation::Tagged()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void FastCloneShallowObjectDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r3, r2, r1, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CreateAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r2, r3};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallFunctionDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallConstructDescriptor::Initialize(Isolate* isolate) {
  // r0 : number of arguments
  // r1 : the function to call
  // r2 : feedback vector
  // r3 : (only if r2 is not the megamorphic symbol) slot in feedback
  //      vector (Smi)
  // TODO(turbofan): So far we don't gather type feedback and hence skip the
  // slot parameter, but ArrayConstructStub needs the vector to be undefined.
  Register registers[] = {cp, r0, r1, r2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void RegExpConstructResultDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r2, r1, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void TransitionElementsKindDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r0, r1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorConstantArgCountDescriptor::Initialize(Isolate* isolate) {
  // register state
  // cp -- context
  // r0 -- number of arguments
  // r1 -- function
  // r2 -- allocation site with elements kind
  Register registers[] = {cp, r1, r2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {cp, r1, r2, r0};
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
  // r0 -- number of arguments
  // r1 -- constructor function
  Register registers[] = {cp, r1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void InternalArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {cp, r1, r0};
  Representation representations[] = {Representation::Tagged(),
                                      Representation::Tagged(),
                                      Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void CompareNilDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToBooleanDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r1, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpWithAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r2, r1, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void StringAddDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {cp, r1, r0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void KeyedDescriptor::Initialize(Isolate* isolate) {
  static PlatformInterfaceDescriptor noInlineDescriptor =
      PlatformInterfaceDescriptor(NEVER_INLINE_TARGET_ADDRESS);

  Register registers[] = {
      cp,  // context
      r2,  // key
  };
  Representation representations[] = {
      Representation::Tagged(),  // context
      Representation::Tagged(),  // key
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations, &noInlineDescriptor);
}


void NamedDescriptor::Initialize(Isolate* isolate) {
  static PlatformInterfaceDescriptor noInlineDescriptor =
      PlatformInterfaceDescriptor(NEVER_INLINE_TARGET_ADDRESS);

  Register registers[] = {
      cp,  // context
      r2,  // name
  };
  Representation representations[] = {
      Representation::Tagged(),  // context
      Representation::Tagged(),  // name
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations, &noInlineDescriptor);
}


void CallHandlerDescriptor::Initialize(Isolate* isolate) {
  static PlatformInterfaceDescriptor default_descriptor =
      PlatformInterfaceDescriptor(CAN_INLINE_TARGET_ADDRESS);

  Register registers[] = {
      cp,  // context
      r0,  // receiver
  };
  Representation representations[] = {
      Representation::Tagged(),  // context
      Representation::Tagged(),  // receiver
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations, &default_descriptor);
}


void ArgumentAdaptorDescriptor::Initialize(Isolate* isolate) {
  static PlatformInterfaceDescriptor default_descriptor =
      PlatformInterfaceDescriptor(CAN_INLINE_TARGET_ADDRESS);

  Register registers[] = {
      cp,  // context
      r1,  // JSFunction
      r0,  // actual number of arguments
      r2,  // expected number of arguments
  };
  Representation representations[] = {
      Representation::Tagged(),     // context
      Representation::Tagged(),     // JSFunction
      Representation::Integer32(),  // actual number of arguments
      Representation::Integer32(),  // expected number of arguments
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations, &default_descriptor);
}


void ApiFunctionDescriptor::Initialize(Isolate* isolate) {
  static PlatformInterfaceDescriptor default_descriptor =
      PlatformInterfaceDescriptor(CAN_INLINE_TARGET_ADDRESS);

  Register registers[] = {
      cp,  // context
      r0,  // callee
      r4,  // call_data
      r2,  // holder
      r1,  // api_function_address
  };
  Representation representations[] = {
      Representation::Tagged(),    // context
      Representation::Tagged(),    // callee
      Representation::Tagged(),    // call_data
      Representation::Tagged(),    // holder
      Representation::External(),  // api_function_address
  };
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations, &default_descriptor);
}
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_ARM
