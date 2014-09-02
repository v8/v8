// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return rsi; }


void CallDescriptors::InitializeForIsolate(Isolate* isolate) {
  InitializeForIsolateAllPlatforms(isolate);

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewClosureCall);
    Register registers[] = {rsi, rbx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewContextCall);
    Register registers[] = {rsi, rdi};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToNumberCall);
    // ToNumberStub invokes a function, and therefore needs a context.
    Register registers[] = {rsi, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NumberToStringCall);
    Register registers[] = {rsi, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowArrayCall);
    Register registers[] = {rsi, rax, rbx, rcx};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Smi(), Representation::Tagged()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowObjectCall);
    Register registers[] = {rsi, rax, rbx, rcx, rdx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CreateAllocationSiteCall);
    Register registers[] = {rsi, rbx, rdx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallFunctionCall);
    Register registers[] = {rsi, rdi};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallConstructCall);
    // rax : number of arguments
    // rbx : feedback vector
    // rdx : (only if rbx is not the megamorphic symbol) slot in feedback
    //       vector (Smi)
    // rdi : constructor function
    // TODO(turbofan): So far we don't gather type feedback and hence skip the
    // slot parameter, but ArrayConstructStub needs the vector to be undefined.
    Register registers[] = {rsi, rax, rdi, rbx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::RegExpConstructResultCall);
    Register registers[] = {rsi, rcx, rbx, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::TransitionElementsKindCall);
    Register registers[] = {rsi, rax, rbx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::ArrayConstructorConstantArgCountCall);
    // register state
    // rax -- number of arguments
    // rdi -- function
    // rbx -- allocation site with elements kind
    Register registers[] = {rsi, rdi, rbx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {rsi, rdi, rbx, rax};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Tagged(), Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorConstantArgCountCall);
    // register state
    // rsi -- context
    // rax -- number of arguments
    // rdi -- constructor function
    Register registers[] = {rsi, rdi};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {rsi, rdi, rax};
    Representation representations[] = {Representation::Tagged(),
                                        Representation::Tagged(),
                                        Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CompareNilCall);
    Register registers[] = {rsi, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToBooleanCall);
    Register registers[] = {rsi, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::BinaryOpCall);
    Register registers[] = {rsi, rdx, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::BinaryOpWithAllocationSiteCall);
    Register registers[] = {rsi, rcx, rdx, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::StringAddCall);
    Register registers[] = {rsi, rdx, rax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArgumentAdaptorCall);
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
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::KeyedCall);
    Register registers[] = {
        rsi,  // context
        rcx,  // key
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // key
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NamedCall);
    Register registers[] = {
        rsi,  // context
        rcx,  // name
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // name
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallHandler);
    Register registers[] = {
        rsi,  // context
        rdx,  // receiver
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // receiver
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ApiFunctionCall);
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
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
}
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X64
