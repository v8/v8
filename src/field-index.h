// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FIELD_INDEX_H_
#define V8_FIELD_INDEX_H_

#include "src/property-details.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

class Map;

// Wrapper class to hold a field index, usually but not necessarily generated
// from a property index. When available, the wrapper class captures additional
// information to allow the field index to be translated back into the property
// index it was originally generated from.
class FieldIndex V8_FINAL {
 public:
  static FieldIndex ForPropertyIndex(Map* map,
                                     int index,
                                     bool is_double = false);
  static FieldIndex ForInObjectOffset(int offset, Map* map = NULL);
  static FieldIndex ForLookupResult(const LookupResult* result);
  static FieldIndex ForDescriptor(Map* map, int descriptor_index);
  static FieldIndex ForLoadByFieldIndex(Map* map, int index);
  static FieldIndex ForKeyedLookupCacheIndex(Map* map, int index);

  bool is_inobject() const {
    return IsInObjectBits::decode(bit_field_);
  }

  bool is_double() const {
    return IsDoubleBits::decode(bit_field_);
  }

  int offset() const {
    return index() * kPointerSize;
  }

  int index() const {
    return IndexBits::decode(bit_field_);
  }

  int outobject_array_index() const {
    ASSERT(!is_inobject());
    return index() - first_inobject_property_offset() / kPointerSize;
  }

  int property_index() const {
    ASSERT(!IsHiddenField::decode(bit_field_));
    int result = index() - first_inobject_property_offset() / kPointerSize;
    if (!is_inobject()) {
      result += InObjectPropertyBits::decode(bit_field_);
    }
    return result;
  }

  int GetLoadByFieldIndex() const {
    // For efficiency, the LoadByFieldIndex instruction takes an index that is
    // optimized for quick access. If the property is inline, the index is
    // positive. If it's out-of-line, the encoded index is -raw_index - 1 to
    // disambiguate the zero out-of-line index from the zero inobject case.
    // The index itself is shifted up by one bit, the lower-most bit
    // signifying if the field is a mutable double box (1) or not (0).
    int result = index() - first_inobject_property_offset() / kPointerSize;
    if (!is_inobject()) {
      result = -result - 1;
    }
    result <<= 1;
    return is_double() ? (result | 1) : result;
  }

  int GetKeyedLookupCacheIndex() const;

  int GetLoadFieldStubKey() const {
    return bit_field_ &
        (IsInObjectBits::kMask | IsDoubleBits::kMask | IndexBits::kMask);
  }

 private:
  FieldIndex(bool is_inobject, int local_index, bool is_double,
             int inobject_properties, int first_inobject_property_offset,
             bool is_hidden = false) {
    ASSERT((first_inobject_property_offset & (kPointerSize - 1)) == 0);
    bit_field_ = IsInObjectBits::encode(is_inobject) |
      IsDoubleBits::encode(is_double) |
      FirstInobjectPropertyOffsetBits::encode(first_inobject_property_offset) |
      IsHiddenField::encode(is_hidden) |
      IndexBits::encode(local_index) |
      InObjectPropertyBits::encode(inobject_properties);
  }

  int first_inobject_property_offset() const {
    ASSERT(!IsHiddenField::decode(bit_field_));
    return FirstInobjectPropertyOffsetBits::decode(bit_field_);
  }

  static const int kIndexBitsSize = kDescriptorIndexBitCount + 1;

  class IndexBits: public BitField<int, 0, kIndexBitsSize> {};
  class IsInObjectBits: public BitField<bool, IndexBits::kNext, 1> {};
  class IsDoubleBits: public BitField<bool, IsInObjectBits::kNext, 1> {};
  class InObjectPropertyBits: public BitField<int, IsDoubleBits::kNext,
                                              kDescriptorIndexBitCount> {};
  class FirstInobjectPropertyOffsetBits:
      public BitField<int, InObjectPropertyBits::kNext, 7> {};
  class IsHiddenField:
      public BitField<bool, FirstInobjectPropertyOffsetBits::kNext, 1> {};
  STATIC_ASSERT(IsHiddenField::kNext <= 32);

  int bit_field_;
};

} }  // namespace v8::internal

#endif
