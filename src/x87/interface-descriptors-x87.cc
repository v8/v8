// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X87

#include "src/ic/ic-conventions.h"
#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return esi; }


void CallDescriptors::InitializeForIsolate(Isolate* isolate) {
  InitializeForIsolateAllPlatforms(isolate);

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewClosureCall);
    Register registers[] = {esi, ebx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewContextCall);
    Register registers[] = {esi, edi};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToNumberCall);
    // ToNumberStub invokes a function, and therefore needs a context.
    Register registers[] = {esi, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NumberToStringCall);
    Register registers[] = {esi, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowArrayCall);
    Register registers[] = {esi, eax, ebx, ecx};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Smi(), Representation::Tagged()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowObjectCall);
    Register registers[] = {esi, eax, ebx, ecx, edx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CreateAllocationSiteCall);
    Register registers[] = {esi, ebx, edx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallFunctionCall);
    Register registers[] = {esi, edi};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallConstructCall);
    // eax : number of arguments
    // ebx : feedback vector
    // edx : (only if ebx is not the megamorphic symbol) slot in feedback
    //       vector (Smi)
    // edi : constructor function
    // TODO(turbofan): So far we don't gather type feedback and hence skip the
    // slot parameter, but ArrayConstructStub needs the vector to be undefined.
    Register registers[] = {esi, eax, edi, ebx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::RegExpConstructResultCall);
    Register registers[] = {esi, ecx, ebx, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::TransitionElementsKindCall);
    Register registers[] = {esi, eax, ebx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::ArrayConstructorConstantArgCountCall);
    // register state
    // eax -- number of arguments
    // edi -- function
    // ebx -- allocation site with elements kind
    Register registers[] = {esi, edi, ebx};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {esi, edi, ebx, eax};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Tagged(), Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorConstantArgCountCall);
    // register state
    // eax -- number of arguments
    // edi -- function
    Register registers[] = {esi, edi};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {esi, edi, eax};
    Representation representations[] = {Representation::Tagged(),
                                        Representation::Tagged(),
                                        Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CompareNilCall);
    Register registers[] = {esi, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToBooleanCall);
    Register registers[] = {esi, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::BinaryOpCall);
    Register registers[] = {esi, edx, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::BinaryOpWithAllocationSiteCall);
    Register registers[] = {esi, ecx, edx, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::StringAddCall);
    Register registers[] = {esi, edx, eax};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArgumentAdaptorCall);
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
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::KeyedCall);
    Register registers[] = {
        esi,  // context
        ecx,  // key
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
        esi,  // context
        ecx,  // name
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
        esi,  // context
        edx,  // name
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
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
}
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X87
