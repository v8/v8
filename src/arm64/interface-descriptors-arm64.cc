// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_ARM64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return cp; }


const Register LoadDescriptor::ReceiverRegister() { return x1; }
const Register LoadDescriptor::NameRegister() { return x2; }


const Register VectorLoadICDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}


const Register VectorLoadICDescriptor::NameRegister() {
  return LoadDescriptor::NameRegister();
}


const Register VectorLoadICDescriptor::SlotRegister() { return x0; }
const Register VectorLoadICDescriptor::VectorRegister() { return x3; }


const Register StoreDescriptor::ReceiverRegister() { return x1; }
const Register StoreDescriptor::NameRegister() { return x2; }
const Register StoreDescriptor::ValueRegister() { return x0; }


const Register ElementTransitionAndStoreDescriptor::ReceiverRegister() {
  return StoreDescriptor::ReceiverRegister();
}


const Register ElementTransitionAndStoreDescriptor::NameRegister() {
  return StoreDescriptor::NameRegister();
}


const Register ElementTransitionAndStoreDescriptor::ValueRegister() {
  return StoreDescriptor::ValueRegister();
}


const Register ElementTransitionAndStoreDescriptor::MapRegister() { return x3; }


const Register InstanceofDescriptor::left() {
  // Object to check (instanceof lhs).
  return x11;
}


const Register InstanceofDescriptor::right() {
  // Constructor function (instanceof rhs).
  return x10;
}


void FastNewClosureDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x2: function info
  Register registers[] = {cp, x2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastNewContextDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x1: function
  Register registers[] = {cp, x1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToNumberDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x0: value
  Register registers[] = {cp, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void NumberToStringDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x0: value
  Register registers[] = {cp, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastCloneShallowArrayDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x3: array literals array
  // x2: array literal index
  // x1: constant elements
  Register registers[] = {cp, x3, x2, x1};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(), Representation::Smi(),
      Representation::Tagged()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void FastCloneShallowObjectDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x3: object literals array
  // x2: object literal index
  // x1: constant properties
  // x0: object literal flags
  Register registers[] = {cp, x3, x2, x1, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CreateAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x2: feedback vector
  // x3: call feedback slot
  Register registers[] = {cp, x2, x3};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallFunctionDescriptor::Initialize(Isolate* isolate) {
  // x1  function    the function to call
  Register registers[] = {cp, x1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallConstructDescriptor::Initialize(Isolate* isolate) {
  // x0 : number of arguments
  // x1 : the function to call
  // x2 : feedback vector
  // x3 : slot in feedback vector (smi) (if r2 is not the megamorphic symbol)
  // TODO(turbofan): So far we don't gather type feedback and hence skip the
  // slot parameter, but ArrayConstructStub needs the vector to be undefined.
  Register registers[] = {cp, x0, x1, x2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void RegExpConstructResultDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x2: length
  // x1: index (of last match)
  // x0: string
  Register registers[] = {cp, x2, x1, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void TransitionElementsKindDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x0: value (js_array)
  // x1: to_map
  Register registers[] = {cp, x0, x1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorConstantArgCountDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x1: function
  // x2: allocation site with elements kind
  // x0: number of arguments to the constructor function
  Register registers[] = {cp, x1, x2};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {cp, x1, x2, x0};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(),
      Representation::Tagged(), Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void InternalArrayConstructorConstantArgCountDescriptor::Initialize(
    Isolate* isolate) {
  // cp: context
  // x1: constructor function
  // x0: number of arguments to the constructor function
  Register registers[] = {cp, x1};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void InternalArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {cp, x1, x0};
  Representation representations[] = {Representation::Tagged(),
                                      Representation::Tagged(),
                                      Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void CompareNilDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x0: value to compare
  Register registers[] = {cp, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToBooleanDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x0: value
  Register registers[] = {cp, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x1: left operand
  // x0: right operand
  Register registers[] = {cp, x1, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpWithAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x2: allocation site
  // x1: left operand
  // x0: right operand
  Register registers[] = {cp, x2, x1, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void StringAddDescriptor::Initialize(Isolate* isolate) {
  // cp: context
  // x1: left operand
  // x0: right operand
  Register registers[] = {cp, x1, x0};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void KeyedDescriptor::Initialize(Isolate* isolate) {
  static PlatformInterfaceDescriptor noInlineDescriptor =
      PlatformInterfaceDescriptor(NEVER_INLINE_TARGET_ADDRESS);

  Register registers[] = {
      cp,  // context
      x2,  // key
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
      x2,  // name
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
      x0,  // receiver
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
      x1,  // JSFunction
      x0,  // actual number of arguments
      x2,  // expected number of arguments
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
      x0,  // callee
      x4,  // call_data
      x2,  // holder
      x1,  // api_function_address
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

#endif  // V8_TARGET_ARCH_ARM64
