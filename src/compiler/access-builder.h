// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ACCESS_BUILDER_H_
#define V8_COMPILER_ACCESS_BUILDER_H_

#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

// This access builder provides a set of static methods constructing commonly
// used FieldAccess and ElementAccess descriptors. These descriptors server as
// parameters to simplified load/store operators.
class AccessBuilder : public AllStatic {
 public:
  // Provides access to JSObject::elements() field.
  static FieldAccess ForJSObjectElements() {
    return {kTaggedBase, JSObject::kElementsOffset, Handle<Name>(),
            Type::Internal(), kMachAnyTagged};
  }

  // Provides access to ExternalArray::external_pointer() field.
  static FieldAccess ForExternalArrayPointer() {
    return {kTaggedBase, ExternalArray::kExternalPointerOffset, Handle<Name>(),
            Type::UntaggedPtr(), kMachPtr};
  }

  // Provides access to Fixed{type}TypedArray and External{type}Array elements.
  static ElementAccess ForTypedArrayElement(ExternalArrayType type,
                                            bool is_external) {
    BaseTaggedness taggedness = is_external ? kUntaggedBase : kTaggedBase;
    int header_size = is_external ? 0 : FixedTypedArrayBase::kDataOffset;
    switch (type) {
      case kExternalInt8Array:
        return {taggedness, header_size, Type::Signed32(), kMachInt8};
      case kExternalUint8Array:
      case kExternalUint8ClampedArray:
        return {taggedness, header_size, Type::Unsigned32(), kMachUint8};
      case kExternalInt16Array:
        return {taggedness, header_size, Type::Signed32(), kMachInt16};
      case kExternalUint16Array:
        return {taggedness, header_size, Type::Unsigned32(), kMachUint16};
      case kExternalInt32Array:
        return {taggedness, header_size, Type::Signed32(), kMachInt32};
      case kExternalUint32Array:
        return {taggedness, header_size, Type::Unsigned32(), kMachUint32};
      case kExternalFloat32Array:
        return {taggedness, header_size, Type::Number(), kRepFloat32};
      case kExternalFloat64Array:
        return {taggedness, header_size, Type::Number(), kRepFloat64};
    }
    UNREACHABLE();
    return {kUntaggedBase, 0, Type::None(), kMachNone};
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(AccessBuilder);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ACCESS_BUILDER_H_
