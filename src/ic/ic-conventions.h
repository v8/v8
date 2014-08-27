// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_CONVENTIONS_H_
#define V8_IC_CONVENTIONS_H_

namespace v8 {
namespace internal {

class LoadConvention {
 public:
  enum ParameterIndices { kReceiverIndex, kNameIndex, kParameterCount };
  static const Register ReceiverRegister();
  static const Register NameRegister();
};


class VectorLoadConvention : public LoadConvention {
 public:
  enum ParameterIndices {
    kReceiverIndex,
    kNameIndex,
    kSlotIndex,
    kParameterCount
  };
  static const Register SlotRegister();
};


class FullVectorLoadConvention : public VectorLoadConvention {
 public:
  enum ParameterIndices {
    kReceiverIndex,
    kNameIndex,
    kSlotIndex,
    kVectorIndex,
    kParameterCount
  };
  static const Register VectorRegister();
};


class StoreConvention {
 public:
  enum ParameterIndices {
    kReceiverIndex,
    kNameIndex,
    kValueIndex,
    kParameterCount
  };
  static const Register ReceiverRegister();
  static const Register NameRegister();
  static const Register ValueRegister();

  // The map register isn't part of the normal call specification, but
  // ElementsTransitionAndStoreStub, used in polymorphic keyed store
  // stub implementations requires it to be initialized.
  static const Register MapRegister();
};
}
}  // namespace v8::internal

#endif  // V8_IC_CONVENTIONS_H_
