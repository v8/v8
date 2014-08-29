// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

InterfaceDescriptor::InterfaceDescriptor() : register_param_count_(-1) {}


void InterfaceDescriptor::Initialize(
    int register_parameter_count, Register* registers,
    Representation* register_param_representations,
    PlatformInterfaceDescriptor* platform_descriptor) {
  platform_specific_descriptor_ = platform_descriptor;
  register_param_count_ = register_parameter_count;

  // An interface descriptor must have a context register.
  DCHECK(register_parameter_count > 0 && registers[0].is(ContextRegister()));

  // InterfaceDescriptor owns a copy of the registers array.
  register_params_.Reset(NewArray<Register>(register_parameter_count));
  for (int i = 0; i < register_parameter_count; i++) {
    register_params_[i] = registers[i];
  }

  // If a representations array is specified, then the descriptor owns that as
  // well.
  if (register_param_representations != NULL) {
    register_param_representations_.Reset(
        NewArray<Representation>(register_parameter_count));
    for (int i = 0; i < register_parameter_count; i++) {
      // If there is a context register, the representation must be tagged.
      DCHECK(
          i != 0 ||
          register_param_representations[i].Equals(Representation::Tagged()));
      register_param_representations_[i] = register_param_representations[i];
    }
  }
}


void CallInterfaceDescriptor::Initialize(
    int register_parameter_count, Register* registers,
    Representation* param_representations,
    PlatformInterfaceDescriptor* platform_descriptor) {
  InterfaceDescriptor::Initialize(register_parameter_count, registers,
                                  param_representations, platform_descriptor);
}
}
}  // namespace v8::internal
