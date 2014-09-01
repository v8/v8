// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X64

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register InterfaceDescriptor::ContextRegister() { return rsi; }


void CallDescriptors::InitializeForIsolate(Isolate* isolate) {
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
