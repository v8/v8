// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_ARM64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return cp; }


void CallDescriptors::InitializeForIsolate(Isolate* isolate) {
  static PlatformInterfaceDescriptor default_descriptor =
      PlatformInterfaceDescriptor(CAN_INLINE_TARGET_ADDRESS);

  static PlatformInterfaceDescriptor noInlineDescriptor =
      PlatformInterfaceDescriptor(NEVER_INLINE_TARGET_ADDRESS);

  InitializeForIsolateAllPlatforms(isolate);

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewClosureCall);
    // cp: context
    // x2: function info
    Register registers[] = {cp, x2};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewContextCall);
    // cp: context
    // x1: function
    Register registers[] = {cp, x1};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToNumberCall);
    // cp: context
    // x0: value
    Register registers[] = {cp, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NumberToStringCall);
    // cp: context
    // x0: value
    Register registers[] = {cp, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowArrayCall);
    // cp: context
    // x3: array literals array
    // x2: array literal index
    // x1: constant elements
    Register registers[] = {cp, x3, x2, x1};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Smi(), Representation::Tagged()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowObjectCall);
    // cp: context
    // x3: object literals array
    // x2: object literal index
    // x1: constant properties
    // x0: object literal flags
    Register registers[] = {cp, x3, x2, x1, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CreateAllocationSiteCall);
    // cp: context
    // x2: feedback vector
    // x3: call feedback slot
    Register registers[] = {cp, x2, x3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallFunctionCall);
    // x1  function    the function to call
    Register registers[] = {cp, x1};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallConstructCall);
    // x0 : number of arguments
    // x1 : the function to call
    // x2 : feedback vector
    // x3 : slot in feedback vector (smi) (if r2 is not the megamorphic symbol)
    // TODO(turbofan): So far we don't gather type feedback and hence skip the
    // slot parameter, but ArrayConstructStub needs the vector to be undefined.
    Register registers[] = {cp, x0, x1, x2};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::RegExpConstructResultCall);
    // cp: context
    // x2: length
    // x1: index (of last match)
    // x0: string
    Register registers[] = {cp, x2, x1, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::TransitionElementsKindCall);
    // cp: context
    // x0: value (js_array)
    // x1: to_map
    Register registers[] = {cp, x0, x1};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::ArrayConstructorConstantArgCountCall);
    // cp: context
    // x1: function
    // x2: allocation site with elements kind
    // x0: number of arguments to the constructor function
    Register registers[] = {cp, x1, x2};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {cp, x1, x2, x0};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Tagged(), Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorConstantArgCountCall);
    // cp: context
    // x1: constructor function
    // x0: number of arguments to the constructor function
    Register registers[] = {cp, x1};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {cp, x1, x0};
    Representation representations[] = {Representation::Tagged(),
                                        Representation::Tagged(),
                                        Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CompareNilCall);
    // cp: context
    // x0: value to compare
    Register registers[] = {cp, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToBooleanCall);
    // cp: context
    // x0: value
    Register registers[] = {cp, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::BinaryOpCall);
    // cp: context
    // x1: left operand
    // x0: right operand
    Register registers[] = {cp, x1, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::BinaryOpWithAllocationSiteCall);
    // cp: context
    // x2: allocation site
    // x1: left operand
    // x0: right operand
    Register registers[] = {cp, x2, x1, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::StringAddCall);
    // cp: context
    // x1: left operand
    // x0: right operand
    Register registers[] = {cp, x1, x0};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArgumentAdaptorCall);
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
    descriptor->Initialize(arraysize(registers), registers, representations,
                           &default_descriptor);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::KeyedCall);
    Register registers[] = {
        cp,  // context
        x2,  // key
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // key
    };
    descriptor->Initialize(arraysize(registers), registers, representations,
                           &noInlineDescriptor);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NamedCall);
    Register registers[] = {
        cp,  // context
        x2,  // name
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // name
    };
    descriptor->Initialize(arraysize(registers), registers, representations,
                           &noInlineDescriptor);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallHandler);
    Register registers[] = {
        cp,  // context
        x0,  // receiver
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // receiver
    };
    descriptor->Initialize(arraysize(registers), registers, representations,
                           &default_descriptor);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ApiFunctionCall);
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
    descriptor->Initialize(arraysize(registers), registers, representations,
                           &default_descriptor);
  }
}
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_ARM64
