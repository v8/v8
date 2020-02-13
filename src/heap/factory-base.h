// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_BASE_H_
#define V8_HEAP_FACTORY_BASE_H_

#include "src/base/export-template.h"
#include "src/common/globals.h"
#include "src/handles/handle-for.h"
#include "src/objects/function-kind.h"
#include "src/objects/instance-type.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

class HeapObject;
class SharedFunctionInfo;
class FunctionLiteral;
class SeqOneByteString;
class SeqTwoByteString;
class FreshlyAllocatedBigInt;
class ObjectBoilerplateDescription;
class ArrayBoilerplateDescription;
class TemplateObjectDescription;
class SourceTextModuleInfo;
class PreparseData;
class UncompiledDataWithoutPreparseData;
class UncompiledDataWithPreparseData;

template <typename Impl>
class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) FactoryBase {
 public:
  // Converts the given boolean condition to JavaScript boolean value.
  inline HandleFor<Impl, Oddball> ToBoolean(bool value);

  // Numbers (e.g. literals) are pretenured by the parser.
  // The return value may be a smi or a heap number.
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, Object> NewNumber(double value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, Object> NewNumberFromInt(int32_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, Object> NewNumberFromUint(uint32_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, Object> NewNumberFromSize(size_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, Object> NewNumberFromInt64(int64_t value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, HeapNumber> NewHeapNumber(double value);
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, HeapNumber> NewHeapNumberFromBits(uint64_t bits);
  template <AllocationType allocation = AllocationType::kYoung>
  inline HandleFor<Impl, HeapNumber> NewHeapNumberWithHoleNaN();

  template <AllocationType allocation>
  HandleFor<Impl, HeapNumber> NewHeapNumber();

  HandleFor<Impl, Struct> NewStruct(
      InstanceType type, AllocationType allocation = AllocationType::kYoung);

  // Allocates a fixed array initialized with undefined values.
  HandleFor<Impl, FixedArray> NewFixedArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a fixed array-like object with given map and initialized with
  // undefined values.
  HandleFor<Impl, FixedArray> NewFixedArrayWithMap(
      Map map, int length, AllocationType allocation = AllocationType::kYoung);

  // Allocate a new fixed array with non-existing entries (the hole).
  HandleFor<Impl, FixedArray> NewFixedArrayWithHoles(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocate a new uninitialized fixed double array.
  // The function returns a pre-allocated empty fixed array for length = 0,
  // so the return type must be the general fixed array class.
  HandleFor<Impl, FixedArrayBase> NewFixedDoubleArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a weak fixed array-like object with given map and initialized
  // with undefined values.
  HandleFor<Impl, WeakFixedArray> NewWeakFixedArrayWithMap(
      Map map, int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a fixed array which may contain in-place weak references. The
  // array is initialized with undefined values
  HandleFor<Impl, WeakFixedArray> NewWeakFixedArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a fixed array for name-value pairs of boilerplate properties and
  // calculates the number of properties we need to store in the backing store.
  HandleFor<Impl, ObjectBoilerplateDescription> NewObjectBoilerplateDescription(
      int boilerplate, int all_properties, int index_keys, bool has_seen_proto);

  // Create a new ArrayBoilerplateDescription struct.
  HandleFor<Impl, ArrayBoilerplateDescription> NewArrayBoilerplateDescription(
      ElementsKind elements_kind,
      HandleFor<Impl, FixedArrayBase> constant_values);

  // Create a new TemplateObjectDescription struct.
  HandleFor<Impl, TemplateObjectDescription> NewTemplateObjectDescription(
      HandleFor<Impl, FixedArray> raw_strings,
      HandleFor<Impl, FixedArray> cooked_strings);

  HandleFor<Impl, Script> NewScript(HandleFor<Impl, String> source);
  HandleFor<Impl, Script> NewScriptWithId(HandleFor<Impl, String> source,
                                          int script_id);

  HandleFor<Impl, SharedFunctionInfo> NewSharedFunctionInfoForLiteral(
      FunctionLiteral* literal, HandleFor<Impl, Script> script,
      bool is_toplevel);

  HandleFor<Impl, PreparseData> NewPreparseData(int data_length,
                                                int children_length);

  HandleFor<Impl, UncompiledDataWithoutPreparseData>
  NewUncompiledDataWithoutPreparseData(HandleFor<Impl, String> inferred_name,
                                       int32_t start_position,
                                       int32_t end_position);

  HandleFor<Impl, UncompiledDataWithPreparseData>
  NewUncompiledDataWithPreparseData(HandleFor<Impl, String> inferred_name,
                                    int32_t start_position,
                                    int32_t end_position,
                                    HandleFor<Impl, PreparseData>);

  HandleFor<Impl, SeqOneByteString> NewOneByteInternalizedString(
      const Vector<const uint8_t>& str, uint32_t hash_field);
  HandleFor<Impl, SeqTwoByteString> NewTwoByteInternalizedString(
      const Vector<const uc16>& str, uint32_t hash_field);

  HandleFor<Impl, SeqOneByteString> AllocateRawOneByteInternalizedString(
      int length, uint32_t hash_field);
  HandleFor<Impl, SeqTwoByteString> AllocateRawTwoByteInternalizedString(
      int length, uint32_t hash_field);

  // Allocates and partially initializes an one-byte or two-byte String. The
  // characters of the string are uninitialized. Currently used in regexp code
  // only, where they are pretenured.
  V8_WARN_UNUSED_RESULT MaybeHandleFor<Impl, SeqOneByteString>
  NewRawOneByteString(int length,
                      AllocationType allocation = AllocationType::kYoung);
  V8_WARN_UNUSED_RESULT MaybeHandleFor<Impl, SeqTwoByteString>
  NewRawTwoByteString(int length,
                      AllocationType allocation = AllocationType::kYoung);
  // Create a new cons string object which consists of a pair of strings.
  V8_WARN_UNUSED_RESULT MaybeHandleFor<Impl, String> NewConsString(
      HandleFor<Impl, String> left, HandleFor<Impl, String> right,
      AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT HandleFor<Impl, String> NewConsString(
      HandleFor<Impl, String> left, HandleFor<Impl, String> right, int length,
      bool one_byte, AllocationType allocation = AllocationType::kYoung);

  // Allocates a new BigInt with {length} digits. Only to be used by
  // MutableBigInt::New*.
  HandleFor<Impl, FreshlyAllocatedBigInt> NewBigInt(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Create a serialized scope info.
  HandleFor<Impl, ScopeInfo> NewScopeInfo(
      int length, AllocationType type = AllocationType::kOld);

  HandleFor<Impl, SourceTextModuleInfo> NewSourceTextModuleInfo();

 protected:
  // Allocate memory for an uninitialized array (e.g., a FixedArray or similar).
  HeapObject AllocateRawArray(int size, AllocationType allocation);
  HeapObject AllocateRawFixedArray(int length, AllocationType allocation);
  HeapObject AllocateRawWeakArrayList(int length, AllocationType allocation);

  HeapObject AllocateRawWithImmortalMap(
      int size, AllocationType allocation, Map map,
      AllocationAlignment alignment = kWordAligned);
  HeapObject NewWithImmortalMap(Map map, AllocationType allocation);

  HandleFor<Impl, FixedArray> NewFixedArrayWithFiller(
      Map map, int length, Oddball filler, AllocationType allocation);

  HandleFor<Impl, SharedFunctionInfo> NewSharedFunctionInfo();
  HandleFor<Impl, SharedFunctionInfo> NewSharedFunctionInfo(
      MaybeHandleFor<Impl, String> maybe_name,
      MaybeHandleFor<Impl, HeapObject> maybe_function_data,
      int maybe_builtin_index, FunctionKind kind = kNormalFunction);

 private:
  Impl* impl() { return static_cast<Impl*>(this); }
  auto isolate() { return impl()->isolate(); }
  ReadOnlyRoots read_only_roots() { return impl()->read_only_roots(); }

  HeapObject AllocateRaw(int size, AllocationType allocation,
                         AllocationAlignment alignment = kWordAligned);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_BASE_H_
