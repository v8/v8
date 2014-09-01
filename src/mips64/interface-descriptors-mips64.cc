// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_MIPS64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return cp; }


void CallDescriptors::InitializeForIsolate(Isolate* isolate) {
  InitializeForIsolateAllPlatforms(isolate);

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewClosureCall);
    Register registers[] = {cp, a2};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewContextCall);
    Register registers[] = {cp, a1};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToNumberCall);
    Register registers[] = {cp, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NumberToStringCall);
    Register registers[] = {cp, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowArrayCall);
    Register registers[] = {cp, a3, a2, a1};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Smi(), Representation::Tagged()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowObjectCall);
    Register registers[] = {cp, a3, a2, a1, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CreateAllocationSiteCall);
    Register registers[] = {cp, a2, a3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::RegExpConstructResultCall);
    Register registers[] = {cp, a2, a1, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::TransitionElementsKindCall);
    Register registers[] = {cp, a0, a1};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::ArrayConstructorConstantArgCountCall);
    // register state
    // cp -- context
    // a0 -- number of arguments
    // a1 -- function
    // a2 -- allocation site with elements kind
    Register registers[] = {cp, a1, a2};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {cp, a1, a2, a0};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Tagged(), Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorConstantArgCountCall);
    // register state
    // cp -- context
    // a0 -- number of arguments
    // a1 -- constructor function
    Register registers[] = {cp, a1};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {cp, a1, a0};
    Representation representations[] = {Representation::Tagged(),
                                        Representation::Tagged(),
                                        Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CompareNilCall);
    Register registers[] = {cp, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToBooleanCall);
    Register registers[] = {cp, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::BinaryOpCall);
    Register registers[] = {cp, a1, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::BinaryOpWithAllocationSiteCall);
    Register registers[] = {cp, a2, a1, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::StringAddCall);
    Register registers[] = {cp, a1, a0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArgumentAdaptorCall);
    Register registers[] = { cp,  // context
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
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::KeyedCall);
    Register registers[] = { cp,  // context
                             a2,  // key
    };
    Representation representations[] = {
        Representation::Tagged(),     // context
        Representation::Tagged(),     // key
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NamedCall);
    Register registers[] = { cp,  // context
                             a2,  // name
    };
    Representation representations[] = {
        Representation::Tagged(),     // context
        Representation::Tagged(),     // name
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallHandler);
    Register registers[] = { cp,  // context
                             a0,  // receiver
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
    Register registers[] = { cp,  // context
                             a0,  // callee
                             a4,  // call_data
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
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
}
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS64
