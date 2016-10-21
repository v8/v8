// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_IC_HANDLER_CONFIGURATION_H_
#define V8_IC_HANDLER_CONFIGURATION_H_

#include "src/elements-kind.h"
#include "src/field-index.h"
#include "src/globals.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

// A set of bit fields representing Smi handlers for loads.
class LoadHandler {
 public:
  enum Kind { kForElements, kForFields, kForConstants };
  class KindBits : public BitField<Kind, 0, 2> {};

  //
  // Encoding when KindBits contains kForConstants.
  //

  // +2 here is because each descriptor entry occupies 3 slots in array.
  class DescriptorValueIndexBits
      : public BitField<unsigned, KindBits::kNext,
                        kDescriptorIndexBitCount + 2> {};
  // Make sure we don't overflow the smi.
  STATIC_ASSERT(DescriptorValueIndexBits::kNext <= kSmiValueSize);

  //
  // Encoding when KindBits contains kForFields.
  //
  class IsInobjectBits : public BitField<bool, KindBits::kNext, 1> {};
  class IsDoubleBits : public BitField<bool, IsInobjectBits::kNext, 1> {};
  // +1 here is to cover all possible JSObject header sizes.
  class FieldOffsetBits
      : public BitField<unsigned, IsDoubleBits::kNext,
                        kDescriptorIndexBitCount + 1 + kPointerSizeLog2> {};
  // Make sure we don't overflow the smi.
  STATIC_ASSERT(FieldOffsetBits::kNext <= kSmiValueSize);

  //
  // Encoding when KindBits contains kForElements.
  //
  class IsJsArrayBits : public BitField<bool, KindBits::kNext, 1> {};
  class ConvertHoleBits : public BitField<bool, IsJsArrayBits::kNext, 1> {};
  class ElementsKindBits
      : public BitField<ElementsKind, ConvertHoleBits::kNext, 8> {};
  // Make sure we don't overflow the smi.
  STATIC_ASSERT(ElementsKindBits::kNext <= kSmiValueSize);

  // Creates a Smi-handler for loading a field from fast object.
  static inline Handle<Object> LoadField(Isolate* isolate,
                                         FieldIndex field_index);

  // Creates a Smi-handler for loading a constant from fast object.
  static inline Handle<Object> LoadConstant(Isolate* isolate, int descriptor);

  // Creates a Smi-handler for loading an element.
  static inline Handle<Object> LoadElement(Isolate* isolate,
                                           ElementsKind elements_kind,
                                           bool convert_hole_to_undefined,
                                           bool is_js_array);
};

// A set of bit fields representing Smi handlers for stores.
class StoreHandler {
 public:
  enum Kind { kForElements, kForFields };
  class KindBits : public BitField<Kind, 0, 1> {};

  enum FieldRepresentation { kSmi, kDouble, kHeapObject, kTagged };

  //
  // Encoding when KindBits contains kForFields.
  //
  class IsInobjectBits : public BitField<bool, KindBits::kNext, 1> {};
  class FieldRepresentationBits
      : public BitField<FieldRepresentation, IsInobjectBits::kNext, 2> {};
  // +2 here is because each descriptor entry occupies 3 slots in array.
  class DescriptorValueIndexBits
      : public BitField<unsigned, FieldRepresentationBits::kNext,
                        kDescriptorIndexBitCount + 2> {};
  // +1 here is to cover all possible JSObject header sizes.
  class FieldOffsetBits
      : public BitField<unsigned, DescriptorValueIndexBits::kNext,
                        kDescriptorIndexBitCount + 1 + kPointerSizeLog2> {};
  // Make sure we don't overflow the smi.
  STATIC_ASSERT(FieldOffsetBits::kNext <= kSmiValueSize);

  // Creates a Smi-handler for storing a field to fast object.
  static inline Handle<Object> StoreField(Isolate* isolate, int descriptor,
                                          FieldIndex field_index,
                                          Representation representation);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_IC_HANDLER_CONFIGURATION_H_
