// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CALL_INTERFACE_DESCRIPTOR_H_
#define V8_CALL_INTERFACE_DESCRIPTOR_H_

#include "src/assembler.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

class PlatformInterfaceDescriptor;

class CallInterfaceDescriptor {
 public:
  CallInterfaceDescriptor() : register_param_count_(-1) {}

  // A copy of the passed in registers and param_representations is made
  // and owned by the CallInterfaceDescriptor.

  // TODO(mvstanton): Instead of taking parallel arrays register and
  // param_representations, how about a struct that puts the representation
  // and register side by side (eg, RegRep(r1, Representation::Tagged()).
  // The same should go for the CodeStubInterfaceDescriptor class.
  void Initialize(int register_parameter_count, Register* registers,
                  Representation* param_representations,
                  PlatformInterfaceDescriptor* platform_descriptor = NULL);

  bool IsInitialized() const { return register_param_count_ >= 0; }

  int GetEnvironmentLength() const { return register_param_count_; }

  int GetRegisterParameterCount() const { return register_param_count_; }

  Register GetParameterRegister(int index) const {
    return register_params_[index];
  }

  Representation GetParameterRepresentation(int index) const {
    DCHECK(index < register_param_count_);
    if (register_param_representations_.get() == NULL) {
      return Representation::Tagged();
    }

    return register_param_representations_[index];
  }

  // "Environment" versions of parameter functions. The first register
  // parameter (context) is not included.
  int GetEnvironmentParameterCount() const {
    return GetEnvironmentLength() - 1;
  }

  Register GetEnvironmentParameterRegister(int index) const {
    return GetParameterRegister(index + 1);
  }

  Representation GetEnvironmentParameterRepresentation(int index) const {
    return GetParameterRepresentation(index + 1);
  }

  // Some platforms have extra information to associate with the descriptor.
  PlatformInterfaceDescriptor* platform_specific_descriptor() const {
    return platform_specific_descriptor_;
  }

  static const Register ContextRegister();

 private:
  int register_param_count_;

  // The Register params are allocated dynamically by the
  // InterfaceDescriptor, and freed on destruction. This is because static
  // arrays of Registers cause creation of runtime static initializers
  // which we don't want.
  SmartArrayPointer<Register> register_params_;
  // Specifies Representations for the stub's parameter. Points to an array of
  // Representations of the same length of the numbers of parameters to the
  // stub, or if NULL (the default value), Representation of each parameter
  // assumed to be Tagged().
  SmartArrayPointer<Representation> register_param_representations_;

  PlatformInterfaceDescriptor* platform_specific_descriptor_;

  DISALLOW_COPY_AND_ASSIGN(CallInterfaceDescriptor);
};


enum CallDescriptorKey {
  LoadICCall,
  StoreICCall,
  ElementTransitionAndStoreCall,
  InstanceofCall,
  VectorLoadICCall,
  FastNewClosureCall,
  FastNewContextCall,
  ToNumberCall,
  NumberToStringCall,
  FastCloneShallowArrayCall,
  FastCloneShallowObjectCall,
  CreateAllocationSiteCall,
  CallFunctionCall,
  CallConstructCall,
  RegExpConstructResultCall,
  TransitionElementsKindCall,
  ArrayConstructorConstantArgCountCall,
  ArrayConstructorCall,
  InternalArrayConstructorConstantArgCountCall,
  InternalArrayConstructorCall,
  CompareNilCall,
  ToBooleanCall,
  BinaryOpCall,
  BinaryOpWithAllocationSiteCall,
  StringAddCall,
  KeyedCall,
  NamedCall,
  CallHandler,
  ArgumentAdaptorCall,
  ApiFunctionCall,
  NUMBER_OF_CALL_DESCRIPTORS
};


class CallDescriptors {
 public:
  static void InitializeForIsolate(Isolate* isolate);

 private:
  static void InitializeForIsolateAllPlatforms(Isolate* isolate);
};
}
}  // namespace v8::internal

#if V8_TARGET_ARCH_ARM64
#include "src/arm64/interface-descriptors-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/arm/interface-descriptors-arm.h"
#endif

#endif  // V8_CALL_INTERFACE_DESCRIPTOR_H_
