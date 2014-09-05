// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X87

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return esi; }


const Register LoadDescriptor::ReceiverRegister() { return edx; }
const Register LoadDescriptor::NameRegister() { return ecx; }


const Register VectorLoadICDescriptor::ReceiverRegister() {
  return LoadDescriptor::ReceiverRegister();
}


const Register VectorLoadICDescriptor::NameRegister() {
  return LoadDescriptor::NameRegister();
}


const Register VectorLoadICDescriptor::SlotRegister() { return eax; }
const Register VectorLoadICDescriptor::VectorRegister() { return ebx; }


const Register StoreDescriptor::ReceiverRegister() { return edx; }
const Register StoreDescriptor::NameRegister() { return ecx; }
const Register StoreDescriptor::ValueRegister() { return eax; }


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
  return ebx;
}


const Register InstanceofDescriptor::left() { return eax; }
const Register InstanceofDescriptor::right() { return edx; }


void FastNewClosureDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, ebx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastNewContextDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, edi};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToNumberDescriptor::Initialize(Isolate* isolate) {
  // ToNumberStub invokes a function, and therefore needs a context.
  Register registers[] = {esi, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void NumberToStringDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void FastCloneShallowArrayDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, eax, ebx, ecx};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(), Representation::Smi(),
      Representation::Tagged()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void FastCloneShallowObjectDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, eax, ebx, ecx, edx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CreateAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, ebx, edx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallFunctionDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, edi};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void CallConstructDescriptor::Initialize(Isolate* isolate) {
  // eax : number of arguments
  // ebx : feedback vector
  // edx : (only if ebx is not the megamorphic symbol) slot in feedback
  //       vector (Smi)
  // edi : constructor function
  // TODO(turbofan): So far we don't gather type feedback and hence skip the
  // slot parameter, but ArrayConstructStub needs the vector to be undefined.
  Register registers[] = {esi, eax, edi, ebx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void RegExpConstructResultDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, ecx, ebx, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void TransitionElementsKindDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, eax, ebx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorConstantArgCountDescriptor::Initialize(Isolate* isolate) {
  // register state
  // eax -- number of arguments
  // edi -- function
  // ebx -- allocation site with elements kind
  Register registers[] = {esi, edi, ebx};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {esi, edi, ebx, eax};
  Representation representations[] = {
      Representation::Tagged(), Representation::Tagged(),
      Representation::Tagged(), Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void InternalArrayConstructorConstantArgCountDescriptor::Initialize(
    Isolate* isolate) {
  // register state
  // eax -- number of arguments
  // edi -- function
  Register registers[] = {esi, edi};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void InternalArrayConstructorDescriptor::Initialize(Isolate* isolate) {
  // stack param count needs (constructor pointer, and single argument)
  Register registers[] = {esi, edi, eax};
  Representation representations[] = {Representation::Tagged(),
                                      Representation::Tagged(),
                                      Representation::Integer32()};
  InitializeData(isolate, key(), arraysize(registers), registers,
                 representations);
}


void CompareNilDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void ToBooleanDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, edx, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void BinaryOpWithAllocationSiteDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, ecx, edx, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void StringAddDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {esi, edx, eax};
  InitializeData(isolate, key(), arraysize(registers), registers, NULL);
}


void KeyedDescriptor::Initialize(Isolate* isolate) {
  Register registers[] = {
      esi,  // context
      ecx,  // key
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
      esi,  // context
      ecx,  // name
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
      esi,  // context
      edx,  // name
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
      esi,  // context
      edi,  // JSFunction
      eax,  // actual number of arguments
      ebx,  // expected number of arguments
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
      esi,  // context
      eax,  // callee
      ebx,  // call_data
      ecx,  // holder
      edx,  // api_function_address
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

#endif  // V8_TARGET_ARCH_X87
