// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "debug-helper-internal.h"
#include "heap-constants.h"
#include "include/v8-internal.h"
#include "src/common/ptr-compr-inl.h"
#include "src/objects/string-inl.h"
#include "torque-generated/class-debug-readers-tq.h"

namespace i = v8::internal;

namespace v8_debug_helper_internal {

// INSTANCE_TYPE_CHECKERS_SINGLE_BASE, trimmed down to only classes that have
// layouts defined in .tq files (this subset relationship is asserted below).
// For now, this is a hand-maintained list.
// TODO(v8:7793): Torque should know enough about instance types to generate
// this list.
#define TQ_INSTANCE_TYPES_SINGLE_BASE(V)                       \
  V(ByteArray, BYTE_ARRAY_TYPE)                                \
  V(BytecodeArray, BYTECODE_ARRAY_TYPE)                        \
  V(CallHandlerInfo, CALL_HANDLER_INFO_TYPE)                   \
  V(Cell, CELL_TYPE)                                           \
  V(DescriptorArray, DESCRIPTOR_ARRAY_TYPE)                    \
  V(EmbedderDataArray, EMBEDDER_DATA_ARRAY_TYPE)               \
  V(FeedbackCell, FEEDBACK_CELL_TYPE)                          \
  V(FeedbackVector, FEEDBACK_VECTOR_TYPE)                      \
  V(FixedDoubleArray, FIXED_DOUBLE_ARRAY_TYPE)                 \
  V(Foreign, FOREIGN_TYPE)                                     \
  V(FreeSpace, FREE_SPACE_TYPE)                                \
  V(HeapNumber, HEAP_NUMBER_TYPE)                              \
  V(JSArgumentsObject, JS_ARGUMENTS_TYPE)                      \
  V(JSArray, JS_ARRAY_TYPE)                                    \
  V(JSArrayBuffer, JS_ARRAY_BUFFER_TYPE)                       \
  V(JSArrayIterator, JS_ARRAY_ITERATOR_TYPE)                   \
  V(JSAsyncFromSyncIterator, JS_ASYNC_FROM_SYNC_ITERATOR_TYPE) \
  V(JSAsyncFunctionObject, JS_ASYNC_FUNCTION_OBJECT_TYPE)      \
  V(JSAsyncGeneratorObject, JS_ASYNC_GENERATOR_OBJECT_TYPE)    \
  V(JSBoundFunction, JS_BOUND_FUNCTION_TYPE)                   \
  V(JSDataView, JS_DATA_VIEW_TYPE)                             \
  V(JSDate, JS_DATE_TYPE)                                      \
  V(JSFunction, JS_FUNCTION_TYPE)                              \
  V(JSGlobalObject, JS_GLOBAL_OBJECT_TYPE)                     \
  V(JSGlobalProxy, JS_GLOBAL_PROXY_TYPE)                       \
  V(JSMap, JS_MAP_TYPE)                                        \
  V(JSMessageObject, JS_MESSAGE_OBJECT_TYPE)                   \
  V(JSModuleNamespace, JS_MODULE_NAMESPACE_TYPE)               \
  V(JSPromise, JS_PROMISE_TYPE)                                \
  V(JSProxy, JS_PROXY_TYPE)                                    \
  V(JSRegExp, JS_REGEXP_TYPE)                                  \
  V(JSRegExpStringIterator, JS_REGEXP_STRING_ITERATOR_TYPE)    \
  V(JSSet, JS_SET_TYPE)                                        \
  V(JSStringIterator, JS_STRING_ITERATOR_TYPE)                 \
  V(JSTypedArray, JS_TYPED_ARRAY_TYPE)                         \
  V(JSPrimitiveWrapper, JS_PRIMITIVE_WRAPPER_TYPE)             \
  V(JSFinalizationGroup, JS_FINALIZATION_GROUP_TYPE)           \
  V(JSFinalizationGroupCleanupIterator,                        \
    JS_FINALIZATION_GROUP_CLEANUP_ITERATOR_TYPE)               \
  V(JSWeakMap, JS_WEAK_MAP_TYPE)                               \
  V(JSWeakRef, JS_WEAK_REF_TYPE)                               \
  V(JSWeakSet, JS_WEAK_SET_TYPE)                               \
  V(Map, MAP_TYPE)                                             \
  V(Oddball, ODDBALL_TYPE)                                     \
  V(PreparseData, PREPARSE_DATA_TYPE)                          \
  V(PropertyArray, PROPERTY_ARRAY_TYPE)                        \
  V(PropertyCell, PROPERTY_CELL_TYPE)                          \
  V(SharedFunctionInfo, SHARED_FUNCTION_INFO_TYPE)             \
  V(Symbol, SYMBOL_TYPE)                                       \
  V(WasmExceptionObject, WASM_EXCEPTION_TYPE)                  \
  V(WasmGlobalObject, WASM_GLOBAL_TYPE)                        \
  V(WasmMemoryObject, WASM_MEMORY_TYPE)                        \
  V(WasmModuleObject, WASM_MODULE_TYPE)                        \
  V(WasmTableObject, WASM_TABLE_TYPE)                          \
  V(WeakArrayList, WEAK_ARRAY_LIST_TYPE)                       \
  V(WeakCell, WEAK_CELL_TYPE)
#ifdef V8_INTL_SUPPORT

#define TQ_INSTANCE_TYPES_SINGLE_NOSTRUCTS(V)                \
  TQ_INSTANCE_TYPES_SINGLE_BASE(V)                           \
  V(JSV8BreakIterator, JS_INTL_V8_BREAK_ITERATOR_TYPE)       \
  V(JSCollator, JS_INTL_COLLATOR_TYPE)                       \
  V(JSDateTimeFormat, JS_INTL_DATE_TIME_FORMAT_TYPE)         \
  V(JSListFormat, JS_INTL_LIST_FORMAT_TYPE)                  \
  V(JSLocale, JS_INTL_LOCALE_TYPE)                           \
  V(JSNumberFormat, JS_INTL_NUMBER_FORMAT_TYPE)              \
  V(JSPluralRules, JS_INTL_PLURAL_RULES_TYPE)                \
  V(JSRelativeTimeFormat, JS_INTL_RELATIVE_TIME_FORMAT_TYPE) \
  V(JSSegmentIterator, JS_INTL_SEGMENT_ITERATOR_TYPE)        \
  V(JSSegmenter, JS_INTL_SEGMENTER_TYPE)

#else

#define TQ_INSTANCE_TYPES_SINGLE_NOSTRUCTS(V) TQ_INSTANCE_TYPES_SINGLE_BASE(V)

#endif  // V8_INTL_SUPPORT

enum class InstanceTypeCheckersSingle {
#define ENUM_VALUE(ClassName, INSTANCE_TYPE) k##ClassName = i::INSTANCE_TYPE,
  INSTANCE_TYPE_CHECKERS_SINGLE(ENUM_VALUE)
#undef ENUM_VALUE
};

#define CHECK_VALUE(ClassName, INSTANCE_TYPE)                            \
  static_assert(                                                         \
      static_cast<i::InstanceType>(                                      \
          InstanceTypeCheckersSingle::k##ClassName) == i::INSTANCE_TYPE, \
      "TQ_INSTANCE_TYPES_SINGLE_NOSTRUCTS must be subset of "            \
      "INSTANCE_TYPE_CHECKERS_SINGLE. Invalid class: " #ClassName);
TQ_INSTANCE_TYPES_SINGLE_NOSTRUCTS(CHECK_VALUE)
#undef CHECK_VALUE

// Adapts one STRUCT_LIST_GENERATOR entry to (Name, NAME) format.
#define STRUCT_INSTANCE_TYPE_ADAPTER(V, NAME, Name, name) V(Name, NAME)

#define TQ_INSTANCE_TYPES_SINGLE(V)     \
  TQ_INSTANCE_TYPES_SINGLE_NOSTRUCTS(V) \
  STRUCT_LIST_GENERATOR(STRUCT_INSTANCE_TYPE_ADAPTER, V)

// Likewise, these are the subset of INSTANCE_TYPE_CHECKERS_RANGE that have
// definitions in .tq files, rearranged with more specific things first. Also
// includes JSObject and JSReceiver, which in the runtime are optimized to use
// a one-sided check.
#define TQ_INSTANCE_TYPES_RANGE(V)                                           \
  V(Context, FIRST_CONTEXT_TYPE, LAST_CONTEXT_TYPE)                          \
  V(FixedArray, FIRST_FIXED_ARRAY_TYPE, LAST_FIXED_ARRAY_TYPE)               \
  V(Microtask, FIRST_MICROTASK_TYPE, LAST_MICROTASK_TYPE)                    \
  V(String, FIRST_STRING_TYPE, LAST_STRING_TYPE)                             \
  V(Name, FIRST_NAME_TYPE, LAST_NAME_TYPE)                                   \
  V(WeakFixedArray, FIRST_WEAK_FIXED_ARRAY_TYPE, LAST_WEAK_FIXED_ARRAY_TYPE) \
  V(JSObject, FIRST_JS_OBJECT_TYPE, LAST_JS_OBJECT_TYPE)                     \
  V(JSReceiver, FIRST_JS_RECEIVER_TYPE, LAST_JS_RECEIVER_TYPE)

std::string AppendAddressAndType(const std::string& brief, uintptr_t address,
                                 const char* type) {
  std::stringstream brief_stream;
  brief_stream << "0x" << std::hex << address << " <" << type << ">";
  return brief.empty() ? brief_stream.str()
                       : brief + " (" + brief_stream.str() + ")";
}

struct TypeNameAndProps {
  TypeNameAndProps(d::TypeCheckResult type_check_result, std::string&& type,
                   std::vector<std::unique_ptr<ObjectProperty>>&& properties)
      : type_check_result(type_check_result),
        type_name(std::move(type)),
        props(std::move(properties)) {}
  explicit TypeNameAndProps(d::TypeCheckResult type_check_result)
      : type_check_result(type_check_result) {}
  d::TypeCheckResult type_check_result;
  std::string type_name;
  std::vector<std::unique_ptr<ObjectProperty>> props;
};

TypeNameAndProps GetTypeNameAndPropsByHint(uintptr_t address,
                                           d::MemoryAccessor accessor,
                                           std::string type_hint_string) {
#define TYPE_NAME_CASE(ClassName, ...)                       \
  if (type_hint_string == "v8::internal::" #ClassName) {     \
    return {d::TypeCheckResult::kUsedTypeHint, #ClassName,   \
            Tq##ClassName(address).GetProperties(accessor)}; \
  }

  TQ_INSTANCE_TYPES_SINGLE(TYPE_NAME_CASE)
  TQ_INSTANCE_TYPES_RANGE(TYPE_NAME_CASE)

#undef TYPE_NAME_CASE

  return TypeNameAndProps(d::TypeCheckResult::kUnknownTypeHint);
}

TypeNameAndProps GetTypeNameAndPropsForString(uintptr_t address,
                                              d::MemoryAccessor accessor,
                                              i::InstanceType type) {
  class StringGetDispatcher : public i::AllStatic {
   public:
#define DEFINE_METHOD(ClassName)                             \
  static inline TypeNameAndProps Handle##ClassName(          \
      uintptr_t address, d::MemoryAccessor accessor) {       \
    return {d::TypeCheckResult::kUsedMap, #ClassName,        \
            Tq##ClassName(address).GetProperties(accessor)}; \
  }
    STRING_CLASS_TYPES(DEFINE_METHOD)
#undef DEFINE_METHOD
    static inline TypeNameAndProps HandleInvalidString(
        uintptr_t address, d::MemoryAccessor accessor) {
      return TypeNameAndProps(d::TypeCheckResult::kUnknownInstanceType);
    }
  };

  return i::StringShape(type)
      .DispatchToSpecificTypeWithoutCast<StringGetDispatcher, TypeNameAndProps>(
          address, accessor);
}

std::unique_ptr<ObjectPropertiesResult> GetHeapObjectProperties(
    uintptr_t address, d::MemoryAccessor accessor, Value<i::InstanceType> type,
    const char* type_hint, std::string brief) {
  TypeNameAndProps tnp(d::TypeCheckResult::kUsedMap);

  if (type.validity == d::MemoryAccessResult::kOk) {
    // Dispatch to the appropriate method for each instance type. After calling
    // the generated method to fetch properties, we can add custom properties.
    switch (type.value) {
#define INSTANCE_TYPE_CASE(ClassName, INSTANCE_TYPE)            \
  case i::INSTANCE_TYPE:                                        \
    tnp.type_name = #ClassName;                                 \
    tnp.props = Tq##ClassName(address).GetProperties(accessor); \
    break;
      TQ_INSTANCE_TYPES_SINGLE(INSTANCE_TYPE_CASE)
#undef INSTANCE_TYPE_CASE

      default:

        // Special case: concrete subtypes of String are not included in the
        // main instance type list because they use the low bits of the instance
        // type enum as flags.
        if (type.value <= i::LAST_STRING_TYPE) {
          tnp = GetTypeNameAndPropsForString(address, accessor, type.value);
          break;
        }

#define INSTANCE_RANGE_CASE(ClassName, FIRST_TYPE, LAST_TYPE)      \
  if (type.value >= i::FIRST_TYPE && type.value <= i::LAST_TYPE) { \
    tnp.type_name = #ClassName;                                    \
    tnp.props = Tq##ClassName(address).GetProperties(accessor);    \
    break;                                                         \
  }
        TQ_INSTANCE_TYPES_RANGE(INSTANCE_RANGE_CASE)
#undef INSTANCE_RANGE_CASE

        tnp.type_check_result = d::TypeCheckResult::kUnknownInstanceType;
        break;
    }
  } else if (type_hint != nullptr) {
    // Try to use the provided type hint, since the real instance type is
    // unavailable.
    tnp = GetTypeNameAndPropsByHint(address, accessor, type_hint);
  } else {
    // TODO(v8:9376): Use known maps here. If known map is just a guess (because
    // root pointers weren't provided), then create a synthetic property with
    // the more specific type. Then the caller could presumably ask us again
    // with the type hint we provided. Otherwise, just go ahead and use it to
    // generate properties.
    tnp.type_check_result =
        type.validity == d::MemoryAccessResult::kAddressNotValid
            ? d::TypeCheckResult::kMapPointerInvalid
            : d::TypeCheckResult::kMapPointerValidButInaccessible;
  }

  if (tnp.type_name.empty()) {
    tnp.type_name = "Object";
  }

  // TODO(v8:9376): Many object types need additional data that is not included
  // in their Torque layout definitions. For example, JSObject has an array of
  // in-object properties after its Torque-defined fields, which at a minimum
  // should be represented as an array in this response. If the relevant memory
  // is available, we should instead represent those properties (and any out-of-
  // object properties) using their JavaScript property names.

  brief = AppendAddressAndType(brief, address, tnp.type_name.c_str());

  return v8::base::make_unique<ObjectPropertiesResult>(
      tnp.type_check_result, brief, "v8::internal::" + tnp.type_name,
      std::move(tnp.props));
}

#undef STRUCT_INSTANCE_TYPE_ADAPTER
#undef TQ_INSTANCE_TYPES_SINGLE_BASE
#undef TQ_INSTANCE_TYPES_SINGLE
#undef TQ_INSTANCE_TYPES_SINGLE_NOSTRUCTS
#undef TQ_INSTANCE_TYPES_RANGE

std::unique_ptr<ObjectPropertiesResult> GetHeapObjectProperties(
    uintptr_t address, d::MemoryAccessor memory_accessor, const d::Roots& roots,
    const char* type_hint) {
  // Try to figure out the heap range, for pointer compression (this is unused
  // if pointer compression is disabled).
  uintptr_t any_uncompressed_ptr = 0;
  if (!IsPointerCompressed(address)) any_uncompressed_ptr = address;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.any_heap_pointer;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.map_space;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.old_space;
  if (any_uncompressed_ptr == 0) any_uncompressed_ptr = roots.read_only_space;
  if (any_uncompressed_ptr == 0) {
    // We can't figure out the heap range. Just check for known objects.
    std::string brief = FindKnownObject(address, roots);
    brief = AppendAddressAndType(brief, address, "v8::internal::TaggedValue");
    return v8::base::make_unique<ObjectPropertiesResult>(
        d::TypeCheckResult::kUnableToDecompress, brief,
        "v8::internal::TaggedValue",
        std::vector<std::unique_ptr<ObjectProperty>>());
  }

  // TODO(v8:9376): It seems that the space roots are at predictable offsets
  // within the heap reservation block when pointer compression is enabled, so
  // we should be able to set those here.

  address = Decompress(address, any_uncompressed_ptr);
  // From here on all addresses should be decompressed.

  // Regardless of whether we can read the object itself, maybe we can find its
  // pointer in the list of known objects.
  std::string brief = FindKnownObject(address, roots);

  TqHeapObject heap_object(address);
  Value<uintptr_t> map_ptr = heap_object.GetMapValue(memory_accessor);
  if (map_ptr.validity != d::MemoryAccessResult::kOk) {
    brief = AppendAddressAndType(brief, address, "v8::internal::Object");
    return v8::base::make_unique<ObjectPropertiesResult>(
        map_ptr.validity == d::MemoryAccessResult::kAddressNotValid
            ? d::TypeCheckResult::kObjectPointerInvalid
            : d::TypeCheckResult::kObjectPointerValidButInaccessible,
        brief, "v8::internal::Object",
        std::vector<std::unique_ptr<ObjectProperty>>());
  }
  Value<i::InstanceType> instance_type =
      TqMap(map_ptr.value).GetInstanceTypeValue(memory_accessor);
  return GetHeapObjectProperties(address, memory_accessor, instance_type,
                                 type_hint, brief);
}

std::unique_ptr<ObjectPropertiesResult> GetObjectPropertiesImpl(
    uintptr_t address, d::MemoryAccessor memory_accessor, const d::Roots& roots,
    const char* type_hint) {
  std::vector<std::unique_ptr<ObjectProperty>> props;
  if (static_cast<uint32_t>(address) == i::kClearedWeakHeapObjectLower32) {
    return v8::base::make_unique<ObjectPropertiesResult>(
        d::TypeCheckResult::kWeakRef, "cleared weak ref",
        "v8::internal::HeapObject", std::move(props));
  }
  bool is_weak = (address & i::kHeapObjectTagMask) == i::kWeakHeapObjectTag;
  if (is_weak) {
    address &= ~i::kWeakHeapObjectMask;
  }
  if (i::Internals::HasHeapObjectTag(address)) {
    std::unique_ptr<ObjectPropertiesResult> result =
        GetHeapObjectProperties(address, memory_accessor, roots, type_hint);
    if (is_weak) {
      result->Prepend("weak ref to ");
    }
    return result;
  }

  // For smi values, construct a response with a description representing the
  // untagged value.
  int32_t value = i::PlatformSmiTagging::SmiToInt(address);
  std::stringstream stream;
  stream << value << " (0x" << std::hex << value << ")";
  return v8::base::make_unique<ObjectPropertiesResult>(
      d::TypeCheckResult::kSmi, stream.str(), "v8::internal::Smi",
      std::move(props));
}

}  // namespace v8_debug_helper_internal

namespace di = v8_debug_helper_internal;

extern "C" {
V8_DEBUG_HELPER_EXPORT d::ObjectPropertiesResult*
_v8_debug_helper_GetObjectProperties(uintptr_t object,
                                     d::MemoryAccessor memory_accessor,
                                     const d::Roots& heap_roots,
                                     const char* type_hint) {
  return di::GetObjectPropertiesImpl(object, memory_accessor, heap_roots,
                                     type_hint)
      .release()
      ->GetPublicView();
}
V8_DEBUG_HELPER_EXPORT void _v8_debug_helper_Free_ObjectPropertiesResult(
    d::ObjectPropertiesResult* result) {
  std::unique_ptr<di::ObjectPropertiesResult> ptr(
      static_cast<di::ObjectPropertiesResultExtended*>(result)->base);
}
}
