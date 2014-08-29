// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_ARM64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register InterfaceDescriptor::ContextRegister() { return cp; }


void CallDescriptors::InitializeForIsolate(Isolate* isolate) {
  static PlatformInterfaceDescriptor default_descriptor =
      PlatformInterfaceDescriptor(CAN_INLINE_TARGET_ADDRESS);

  static PlatformInterfaceDescriptor noInlineDescriptor =
      PlatformInterfaceDescriptor(NEVER_INLINE_TARGET_ADDRESS);

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
