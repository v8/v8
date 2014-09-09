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

#define INTERFACE_DESCRIPTOR_LIST(V)          \
  V(Load)                                     \
  V(Store)                                    \
  V(ElementTransitionAndStore)                \
  V(Instanceof)                               \
  V(VectorLoadIC)                             \
  V(FastNewClosure)                           \
  V(FastNewContext)                           \
  V(ToNumber)                                 \
  V(NumberToString)                           \
  V(FastCloneShallowArray)                    \
  V(FastCloneShallowObject)                   \
  V(CreateAllocationSite)                     \
  V(CallFunction)                             \
  V(CallConstruct)                            \
  V(RegExpConstructResult)                    \
  V(TransitionElementsKind)                   \
  V(ArrayConstructorConstantArgCount)         \
  V(ArrayConstructor)                         \
  V(InternalArrayConstructorConstantArgCount) \
  V(InternalArrayConstructor)                 \
  V(CompareNil)                               \
  V(ToBoolean)                                \
  V(BinaryOp)                                 \
  V(BinaryOpWithAllocationSite)               \
  V(StringAdd)                                \
  V(Keyed)                                    \
  V(Named)                                    \
  V(CallHandler)                              \
  V(ArgumentAdaptor)                          \
  V(ApiFunction)


class CallInterfaceDescriptorData {
 public:
  CallInterfaceDescriptorData() : register_param_count_(-1) {}

  // A copy of the passed in registers and param_representations is made
  // and owned by the CallInterfaceDescriptorData.

  // TODO(mvstanton): Instead of taking parallel arrays register and
  // param_representations, how about a struct that puts the representation
  // and register side by side (eg, RegRep(r1, Representation::Tagged()).
  // The same should go for the CodeStubDescriptor class.
  void Initialize(int register_parameter_count, Register* registers,
                  Representation* param_representations,
                  PlatformInterfaceDescriptor* platform_descriptor = NULL);

  bool IsInitialized() const { return register_param_count_ >= 0; }

  int register_param_count() const { return register_param_count_; }
  Register register_param(int index) const { return register_params_[index]; }
  Register* register_params() const { return register_params_.get(); }
  Representation register_param_representation(int index) const {
    return register_param_representations_[index];
  }
  Representation* register_param_representations() const {
    return register_param_representations_.get();
  }
  PlatformInterfaceDescriptor* platform_specific_descriptor() const {
    return platform_specific_descriptor_;
  }

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

  DISALLOW_COPY_AND_ASSIGN(CallInterfaceDescriptorData);
};


class CallDescriptors {
 public:
  enum Key {
#define DEF_ENUM(name) name,
    INTERFACE_DESCRIPTOR_LIST(DEF_ENUM)
#undef DEF_ENUM
    NUMBER_OF_DESCRIPTORS
  };
};


class CallInterfaceDescriptor {
 public:
  CallInterfaceDescriptor() : data_(NULL) {}

  CallInterfaceDescriptor(Isolate* isolate, CallDescriptors::Key key)
      : data_(isolate->call_descriptor_data(key)) {}

  int GetEnvironmentLength() const { return data()->register_param_count(); }

  int GetRegisterParameterCount() const {
    return data()->register_param_count();
  }

  Register GetParameterRegister(int index) const {
    return data()->register_param(index);
  }

  Representation GetParameterRepresentation(int index) const {
    DCHECK(index < data()->register_param_count());
    if (data()->register_param_representations() == NULL) {
      return Representation::Tagged();
    }

    return data()->register_param_representation(index);
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
    return data()->platform_specific_descriptor();
  }

  static const Register ContextRegister();

  const char* DebugName(Isolate* isolate);

 protected:
  const CallInterfaceDescriptorData* data() const { return data_; }

 private:
  const CallInterfaceDescriptorData* data_;
};


#define DECLARE_DESCRIPTOR(name)                                              \
  explicit name(Isolate* isolate) : CallInterfaceDescriptor(isolate, key()) { \
    if (!data()->IsInitialized())                                             \
      Initialize(isolate->call_descriptor_data(key()));                       \
  }                                                                           \
  static inline CallDescriptors::Key key();                                   \
  void Initialize(CallInterfaceDescriptorData* data);


class LoadDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(LoadDescriptor)

  enum ParameterIndices { kReceiverIndex, kNameIndex };
  static const Register ReceiverRegister();
  static const Register NameRegister();
};


class StoreDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(StoreDescriptor)

  enum ParameterIndices {
    kReceiverIndex,
    kNameIndex,
    kValueIndex,
    kParameterCount
  };
  static const Register ReceiverRegister();
  static const Register NameRegister();
  static const Register ValueRegister();
};


class ElementTransitionAndStoreDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ElementTransitionAndStoreDescriptor)

  static const Register ReceiverRegister();
  static const Register NameRegister();
  static const Register ValueRegister();
  static const Register MapRegister();
};


class InstanceofDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(InstanceofDescriptor)

  enum ParameterIndices { kLeftIndex, kRightIndex, kParameterCount };
  static const Register left();
  static const Register right();
};


class VectorLoadICDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(VectorLoadICDescriptor)

  enum ParameterIndices {
    kReceiverIndex,
    kNameIndex,
    kSlotIndex,
    kVectorIndex,
    kParameterCount
  };

  static const Register ReceiverRegister();
  static const Register NameRegister();
  static const Register SlotRegister();
  static const Register VectorRegister();
};


class FastNewClosureDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(FastNewClosureDescriptor)
};


class FastNewContextDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(FastNewContextDescriptor)
};


class ToNumberDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ToNumberDescriptor)
};


class NumberToStringDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(NumberToStringDescriptor)
};


class FastCloneShallowArrayDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(FastCloneShallowArrayDescriptor)
};


class FastCloneShallowObjectDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(FastCloneShallowObjectDescriptor)
};


class CreateAllocationSiteDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(CreateAllocationSiteDescriptor)
};


class CallFunctionDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(CallFunctionDescriptor)
};


class CallConstructDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(CallConstructDescriptor)
};


class RegExpConstructResultDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(RegExpConstructResultDescriptor)
};


class TransitionElementsKindDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(TransitionElementsKindDescriptor)
};


class ArrayConstructorConstantArgCountDescriptor
    : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ArrayConstructorConstantArgCountDescriptor)
};


class ArrayConstructorDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ArrayConstructorDescriptor)
};


class InternalArrayConstructorConstantArgCountDescriptor
    : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(InternalArrayConstructorConstantArgCountDescriptor)
};


class InternalArrayConstructorDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(InternalArrayConstructorDescriptor)
};


class CompareNilDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(CompareNilDescriptor)
};


class ToBooleanDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ToBooleanDescriptor)
};


class BinaryOpDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(BinaryOpDescriptor)
};


class BinaryOpWithAllocationSiteDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(BinaryOpWithAllocationSiteDescriptor)
};


class StringAddDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(StringAddDescriptor)
};


class KeyedDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(KeyedDescriptor)
};


class NamedDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(NamedDescriptor)
};


class CallHandlerDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(CallHandlerDescriptor)
};


class ArgumentAdaptorDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ArgumentAdaptorDescriptor)
};


class ApiFunctionDescriptor : public CallInterfaceDescriptor {
 public:
  DECLARE_DESCRIPTOR(ApiFunctionDescriptor)
};

#undef DECLARE_DESCRIPTOR


// We define the association between CallDescriptors::Key and the specialized
// descriptor here to reduce boilerplate and mistakes.
#define DEF_KEY(name) \
  CallDescriptors::Key name##Descriptor::key() { return CallDescriptors::name; }
INTERFACE_DESCRIPTOR_LIST(DEF_KEY)
#undef DEF_KEY
}
}  // namespace v8::internal


#if V8_TARGET_ARCH_ARM64
#include "src/arm64/interface-descriptors-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/arm/interface-descriptors-arm.h"
#endif

#endif  // V8_CALL_INTERFACE_DESCRIPTOR_H_
