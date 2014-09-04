// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

void CallInterfaceDescriptorData::Initialize(
    int register_parameter_count, Register* registers,
    Representation* register_param_representations,
    PlatformInterfaceDescriptor* platform_descriptor) {
  platform_specific_descriptor_ = platform_descriptor;
  register_param_count_ = register_parameter_count;

  // An interface descriptor must have a context register.
  DCHECK(register_parameter_count > 0 &&
         registers[0].is(CallInterfaceDescriptor::ContextRegister()));

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


void LoadDescriptor::Initialize(CallInterfaceDescriptorData* data) {
  Register registers[] = {ContextRegister(), ReceiverRegister(),
                          NameRegister()};
  data->Initialize(arraysize(registers), registers, NULL);
}


void StoreDescriptor::Initialize(CallInterfaceDescriptorData* data) {
  Register registers[] = {ContextRegister(), ReceiverRegister(), NameRegister(),
                          ValueRegister()};
  data->Initialize(arraysize(registers), registers, NULL);
}


void ElementTransitionAndStoreDescriptor::Initialize(
    CallInterfaceDescriptorData* data) {
  Register registers[] = {ContextRegister(), ValueRegister(), MapRegister(),
                          NameRegister(), ReceiverRegister()};
  data->Initialize(arraysize(registers), registers, NULL);
}


void InstanceofDescriptor::Initialize(CallInterfaceDescriptorData* data) {
  Register registers[] = {ContextRegister(), left(), right()};
  data->Initialize(arraysize(registers), registers, NULL);
}


void VectorLoadICDescriptor::Initialize(CallInterfaceDescriptorData* data) {
  Register registers[] = {ContextRegister(), ReceiverRegister(), NameRegister(),
                          SlotRegister(), VectorRegister()};
  data->Initialize(arraysize(registers), registers, NULL);
}

}
}  // namespace v8::internal
