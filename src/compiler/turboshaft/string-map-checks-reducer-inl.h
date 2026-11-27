// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STRING_MAP_CHECKS_REDUCER_INL_H_
#define V8_COMPILER_TURBOSHAFT_STRING_MAP_CHECKS_REDUCER_INL_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/objects/instance-type-inl.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// Provides string map predicates to the lowering reducers that inherit from
// it. It reduces no operations itself.
template <class Next>
class StringMapChecksReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(StringMapChecks)

 protected:
#if V8_STATIC_ROOTS_BOOL
  // Whether {map_bits}, the compressed address of a map, lies in {range}.
  V<Word32> IsMapInRange(V<Word32> map_bits,
                         InstanceTypeChecker::TaggedAddressRange range) {
    DCHECK_LE(range.first, range.second);
    if (range.first == 0) {
      return __ Uint32LessThanOrEqual(map_bits, range.second);
    }
    return __ Uint32LessThanOrEqual(__ Word32Sub(map_bits, range.first),
                                    range.second - range.first);
  }
#endif  // V8_STATIC_ROOTS_BOOL

  // Accepts the sequential, internalized and shared one-byte string maps
  // alike, matching InstanceTypeChecker::IsSeqOneByteString(Tagged<Map>).
  // Guards on a string constant's map need the whole set: the string can be
  // internalized or shared in place afterwards, which stores one of the other
  // maps without changing the layout the guarded code reads.
  V<Word32> IsSeqOneByteStringMap(V<Map> map) {
#if V8_STATIC_ROOTS_BOOL
    V<Word32> map_bits = CompressedMapBits(map);
    return __ Word32BitwiseAnd(
        IsMapInRange(map_bits, StringTypeRange::kSeqString),
        IsOneByteStringMapBits(map_bits));
#else
    return __ Word32Equal(
        __ Word32BitwiseAnd(
            __ LoadInstanceTypeField(map),
            kIsNotStringMask | kStringRepresentationAndEncodingMask),
        kStringTag | kSeqOneByteStringTag);
#endif  // V8_STATIC_ROOTS_BOOL
  }

  V<Word32> IsCachedExternalOneByteStringMap(V<Map> map) {
#if V8_STATIC_ROOTS_BOOL
    V<Word32> map_bits = CompressedMapBits(map);
    V<Word32> is_external =
        IsMapInRange(map_bits, StringTypeRange::kExternalString);
    // The negation of IsMapInRange, spelled as a single comparison.
    static_assert(StringTypeRange::kUncachedExternalString.first <=
                  StringTypeRange::kUncachedExternalString.second);
    V<Word32> is_cached = __ Uint32LessThan(
        StringTypeRange::kUncachedExternalString.second -
            StringTypeRange::kUncachedExternalString.first,
        __ Word32Sub(map_bits, StringTypeRange::kUncachedExternalString.first));
    return __ Word32BitwiseAnd(__ Word32BitwiseAnd(is_external, is_cached),
                               IsOneByteStringMapBits(map_bits));
#else
    return __ Word32Equal(
        __ Word32BitwiseAnd(__ LoadInstanceTypeField(map),
                            kIsNotStringMask |
                                kStringRepresentationAndEncodingMask |
                                kUncachedExternalStringMask),
        kStringTag | kExternalOneByteStringTag);
#endif  // V8_STATIC_ROOTS_BOOL
  }

  V<Word32> IsSlicedStringMap(V<Map> map) {
#if V8_STATIC_ROOTS_BOOL
    return IsMapInRange(CompressedMapBits(map), StringTypeRange::kSlicedString);
#else
    return __ Word32Equal(
        __ Word32BitwiseAnd(__ LoadInstanceTypeField(map),
                            kIsNotStringMask | kStringRepresentationMask),
        kStringTag | kSlicedStringTag);
#endif  // V8_STATIC_ROOTS_BOOL
  }

  V<Word32> IsThinStringMap(V<Map> map) {
#if V8_STATIC_ROOTS_BOOL
    return IsMapInRange(CompressedMapBits(map), StringTypeRange::kThinString);
#else
    return __ Word32Equal(
        __ Word32BitwiseAnd(__ LoadInstanceTypeField(map),
                            kIsNotStringMask | kStringRepresentationMask),
        kStringTag | kThinStringTag);
#endif  // V8_STATIC_ROOTS_BOOL
  }

#if V8_STATIC_ROOTS_BOOL
 private:
  using StringTypeRange = InstanceTypeChecker::kUniqueMapRangeOfStringType;

  V<Word32> CompressedMapBits(V<Map> map) {
    return __ TruncateWordPtrToWord32(__ BitcastTaggedToWordPtr(map));
  }

  V<Word32> IsOneByteStringMapBits(V<Word32> map_bits) {
    return __ Word32Equal(
        __ Word32BitwiseAnd(map_bits,
                            InstanceTypeChecker::kStringMapEncodingMask),
        InstanceTypeChecker::kOneByteStringMapBit);
  }
#endif  // V8_STATIC_ROOTS_BOOL
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_STRING_MAP_CHECKS_REDUCER_INL_H_
