// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_FACTORY_H_
#define V8_HEAP_FACTORY_H_

// Clients of this interface shouldn't depend on lots of heap internals.
// Do not include anything from src/heap here!
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/baseline/baseline.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/execution/messages.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"
#include "src/heap/factory-base.h"
#include "src/heap/heap.h"
#include "src/objects/feedback-cell.h"
#include "src/objects/property-cell.h"
// TODO(leszeks): Remove this by forward declaring JSRegExp::Flags.
#include "src/objects/js-regexp.h"

namespace unibrow {
enum class Utf8Variant : uint8_t;
}

namespace v8 {
namespace internal {

// Forward declarations.
class AliasedArgumentsEntry;
class ObjectBoilerplateDescription;
class BasicBlockProfilerData;
class BreakPoint;
class BreakPointInfo;
class CallableTask;
class CallbackTask;
class CallSiteInfo;
class Expression;
class EmbedderDataArray;
class ArrayBoilerplateDescription;
class CoverageInfo;
class DebugInfo;
class DeoptimizationData;
class DeoptimizationLiteralArray;
class DictionaryTemplateInfo;
class EnumCache;
class FreshlyAllocatedBigInt;
class FunctionTemplateInfo;
class Isolate;
class JSArrayBufferView;
class JSDataView;
class JSDisposableStackBase;
class JSSyncDisposableStack;
class JSAsyncDisposableStack;
class JSGeneratorObject;
class JSMap;
class JSMapIterator;
class JSModuleNamespace;
class JSPromise;
class JSProxy;
class JSSet;
class JSSetIterator;
class JSTypedArray;
class JSWeakMap;
class LoadHandler;
class NativeContext;
class PromiseResolveThenableJobTask;
class RegExpMatchInfo;
class ScriptContextTable;
template <typename>
class Signature;
class SourceTextModule;
class StackFrameInfo;
class StackTraceInfo;
class StringSet;
class StoreHandler;
class SyntheticModule;
class TemplateObjectDescription;
class WasmCapiFunctionData;
class WasmExportedFunctionData;
class WasmJSFunctionData;
class WeakCell;

#if V8_ENABLE_WEBASSEMBLY
namespace wasm {
#if V8_ENABLE_DRUMBRAKE
class WasmInterpreterRuntime;
#endif  // V8_ENABLE_DRUMBRAKE

class ArrayType;
class StructType;
class ContType;
struct WasmElemSegment;
class WasmImportWrapperHandle;
class WasmValue;
enum class OnResume : int;
enum Suspend : int;
enum Promise : int;
struct CanonicalTypeIndex;
class CanonicalValueType;
class ValueType;
class CanonicalSig;
struct ModuleTypeIndex;
class StackMemory;
}  // namespace wasm
#endif

enum class SharedFlag : uint8_t;
enum class InitializedFlag : uint8_t;

enum FunctionMode {
  kWithNameBit = 1 << 0,
  kWithWritablePrototypeBit = 1 << 1,
  kWithReadonlyPrototypeBit = 1 << 2,
  kWithPrototypeBits = kWithWritablePrototypeBit | kWithReadonlyPrototypeBit,

  // Without prototype.
  FUNCTION_WITHOUT_PROTOTYPE = 0,
  METHOD_WITH_NAME = kWithNameBit,

  // With writable prototype.
  FUNCTION_WITH_WRITEABLE_PROTOTYPE = kWithWritablePrototypeBit,
  FUNCTION_WITH_NAME_AND_WRITEABLE_PROTOTYPE =
      kWithWritablePrototypeBit | kWithNameBit,

  // With readonly prototype.
  FUNCTION_WITH_READONLY_PROTOTYPE = kWithReadonlyPrototypeBit,
  FUNCTION_WITH_NAME_AND_READONLY_PROTOTYPE =
      kWithReadonlyPrototypeBit | kWithNameBit,
};

enum class ArrayStorageAllocationMode {
  DONT_INITIALIZE_ARRAY_ELEMENTS,
  INITIALIZE_ARRAY_ELEMENTS_WITH_HOLE
};

// Interface for handle based allocation.
class V8_EXPORT_PRIVATE Factory : public FactoryBase<Factory> {
 public:
  inline ReadOnlyRoots read_only_roots() const;

  DirectHandle<Hole> NewHole();

  // Allocates a property array initialized with undefined values.
  DirectHandle<PropertyArray> NewPropertyArray(
      int length, AllocationType allocation = AllocationType::kYoung);
  // Tries allocating a fixed array initialized with undefined values.
  // In case of an allocation failure (OOM) an empty handle is returned.
  // The caller has to manually signal an
  // v8::internal::Heap::FatalProcessOutOfMemory typically by calling
  // NewFixedArray as a fallback.
  V8_WARN_UNUSED_RESULT
  MaybeHandle<FixedArray> TryNewFixedArray(
      int length, AllocationType allocation = AllocationType::kYoung);

  // Allocates a feedback vector whose slots are initialized with undefined
  // values.
  Handle<FeedbackVector> NewFeedbackVector(
      DirectHandle<SharedFunctionInfo> shared,
      DirectHandle<ClosureFeedbackCellArray> closure_feedback_cell_array,
      DirectHandle<FeedbackCell> parent_feedback_cell);

  // Allocates a clean embedder data array with given capacity.
  DirectHandle<EmbedderDataArray> NewEmbedderDataArray(int length);

  // Allocate a new fixed double array with hole values.
  DirectHandle<FixedArrayBase> NewFixedDoubleArrayWithHoles(int size);

  // Allocates a NameDictionary with an internal capacity calculated such that
  // |at_least_space_for| entries can be added without reallocating.
  DirectHandle<NameDictionary> NewNameDictionary(int at_least_space_for);

  Handle<OrderedHashSet> NewOrderedHashSet();
  Handle<OrderedHashMap> NewOrderedHashMap();
  Handle<SmallOrderedHashSet> NewSmallOrderedHashSet(
      int capacity = kSmallOrderedHashSetMinCapacity,
      AllocationType allocation = AllocationType::kYoung);
  Handle<SmallOrderedHashMap> NewSmallOrderedHashMap(
      int capacity = kSmallOrderedHashMapMinCapacity,
      AllocationType allocation = AllocationType::kYoung);
  Handle<SmallOrderedNameDictionary> NewSmallOrderedNameDictionary(
      int capacity = kSmallOrderedHashMapMinCapacity,
      AllocationType allocation = AllocationType::kYoung);

  DirectHandle<SwissNameDictionary> CreateCanonicalEmptySwissNameDictionary();

  // Create a new PrototypeInfo struct.
  DirectHandle<PrototypeInfo> NewPrototypeInfo();

  // Create a new EnumCache struct.
  DirectHandle<EnumCache> NewEnumCache(
      DirectHandle<FixedArray> keys, DirectHandle<FixedArray> indices,
      AllocationType allocation = AllocationType::kOld);

  // Create a new Tuple2 struct.
  DirectHandle<Tuple2> NewTuple2Uninitialized(AllocationType allocation);
  DirectHandle<Tuple2> NewTuple2(DirectHandle<Object> value1,
                                 DirectHandle<Object> value2,
                                 AllocationType allocation);

  // Create a new PropertyDescriptorObject struct.
  DirectHandle<PropertyDescriptorObject> NewPropertyDescriptorObject();

  // Finds the internalized copy for string in the string table.
  // If not found, a new string is added to the table and returned.
  Handle<String> InternalizeUtf8String(base::Vector<const char> str);
  Handle<String> InternalizeUtf8String(const char* str) {
    return InternalizeUtf8String(base::CStrVector(str));
  }

  // Import InternalizeString overloads from base class.
  using FactoryBase::InternalizeString;

  Handle<String> InternalizeString(base::Vector<const char> str,
                                   bool convert_encoding = false) {
    return InternalizeString(base::Vector<const uint8_t>::cast(str),
                             convert_encoding);
  }

  Handle<String> InternalizeString(const char* str,
                                   bool convert_encoding = false) {
    return InternalizeString(base::OneByteVector(str), convert_encoding);
  }

  template <typename SeqString, template <typename> typename HandleType>
    requires(
        std::is_convertible_v<HandleType<SeqString>, DirectHandle<SeqString>>)
  Handle<String> InternalizeSubString(HandleType<SeqString>, uint32_t from,
                                      uint32_t length,
                                      bool convert_encoding = false);

  // Internalized strings are created in the old generation (data space).
  // TODO(b/42203211): InternalizeString and InternalizeName are templatized so
  // that passing a Handle<T> is not ambiguous when T is a subtype of String or
  // Name (it could be implicitly converted both to Handle<String> and to
  // DirectHandle<String>). Here, T should be a subtype of String, which is
  // enforced by the second template argument and the similar restriction on
  // Handle's constructor. When the migration to DirectHandle is complete,
  // these functions can accept simply a DirectHandle<String> or
  // DirectHandle<Name>.
  template <typename T>
    requires(std::is_convertible_v<Handle<T>, Handle<String>>)
  inline Handle<String> InternalizeString(Handle<T> string);

  template <typename T>
    requires(std::is_convertible_v<Handle<T>, Handle<Name>>)
  inline Handle<Name> InternalizeName(Handle<T> name);

  template <typename T>
    requires(std::is_convertible_v<DirectHandle<T>, DirectHandle<String>>)
  inline DirectHandle<String> InternalizeString(DirectHandle<T> string);

  template <typename T>
    requires(std::is_convertible_v<DirectHandle<T>, DirectHandle<Name>>)
  inline DirectHandle<Name> InternalizeName(DirectHandle<T> name);

  // String creation functions.  Most of the string creation functions take
  // an AllocationType argument to optionally request that they be
  // allocated in the old generation. Otherwise the default is
  // AllocationType::kYoung.
  //
  // Creates a new String object.  There are two String encodings: one-byte and
  // two-byte.  One should choose between the three string factory functions
  // based on the encoding of the string buffer that the string is
  // initialized from.
  //   - ...FromOneByte (defined in FactoryBase) initializes the string from a
  //     buffer that is Latin1 encoded (it does not check that the buffer is
  //     Latin1 encoded) and the result will be Latin1 encoded.
  //   - ...FromUtf8 initializes the string from a buffer that is UTF-8
  //     encoded.  If the characters are all ASCII characters, the result
  //     will be Latin1 encoded, otherwise it will converted to two-byte.
  //   - ...FromTwoByte initializes the string from a buffer that is two-byte
  //     encoded.  If the characters are all Latin1 characters, the result
  //     will be converted to Latin1, otherwise it will be left as two-byte.
  //
  // One-byte strings are pretenured when used as keys in the SourceCodeCache.
  template <size_t N>
  inline Handle<String> NewStringFromStaticChars(
      const char (&str)[N], AllocationType allocation = AllocationType::kYoung);

  // UTF8 strings are pretenured when used for regexp literal patterns and
  // flags in the parser.
  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewStringFromUtf8(
      base::Vector<const char> str,
      AllocationType allocation = AllocationType::kYoung);
  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewStringFromUtf8(
      std::string_view str,
      AllocationType allocation = AllocationType::kYoung) {
    return NewStringFromUtf8(base::StrVector(str), allocation);
  }

  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewStringFromUtf8(
      base::Vector<const uint8_t> str, unibrow::Utf8Variant utf8_variant,
      AllocationType allocation = AllocationType::kYoung);

#if V8_ENABLE_WEBASSEMBLY
  V8_WARN_UNUSED_RESULT MaybeDirectHandle<String> NewStringFromUtf8(
      DirectHandle<WasmArray> array, uint32_t begin, uint32_t end,
      unibrow::Utf8Variant utf8_variant,
      AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewStringFromUtf8(
      DirectHandle<ByteArray> array, uint32_t start, uint32_t end,
      unibrow::Utf8Variant utf8_variant,
      AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT MaybeDirectHandle<String> NewStringFromUtf16(
      DirectHandle<WasmArray> array, uint32_t start, uint32_t end,
      AllocationType allocation = AllocationType::kYoung);
#endif  // V8_ENABLE_WEBASSEMBLY

  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewStringFromUtf8SubString(
      Handle<SeqOneByteString> str, int begin, int end,
      AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewStringFromTwoByte(
      base::Vector<const base::uc16> str,
      AllocationType allocation = AllocationType::kYoung);

  V8_WARN_UNUSED_RESULT MaybeDirectHandle<String> NewStringFromTwoByte(
      const ZoneVector<base::uc16>* str,
      AllocationType allocation = AllocationType::kYoung);

#if V8_ENABLE_WEBASSEMBLY
  // Usually the two-byte encodings are in the native endianness, but for
  // WebAssembly linear memory, they are explicitly little-endian.
  V8_WARN_UNUSED_RESULT MaybeDirectHandle<String>
  NewStringFromTwoByteLittleEndian(
      base::Vector<const base::uc16> str,
      AllocationType allocation = AllocationType::kYoung);
#endif  // V8_ENABLE_WEBASSEMBLY

  DirectHandle<JSStringIterator> NewJSStringIterator(Handle<String> string);

  DirectHandle<String> NewInternalizedStringImpl(DirectHandle<String> string,
                                                 int len, uint32_t hash_field);

  // Compute the internalization strategy for the input string.
  //
  // Old-generation sequential strings can be internalized by mutating their map
  // and return kInPlace, along with the matching internalized string map for
  // string stored in internalized_map.
  //
  // Internalized strings return kAlreadyTransitioned.
  //
  // All other strings are internalized by flattening and copying and return
  // kCopy.
  V8_WARN_UNUSED_RESULT StringTransitionStrategy
  ComputeInternalizationStrategyForString(
      DirectHandle<String> string, MaybeDirectHandle<Map>* internalized_map);

  // Creates an internalized copy of an external string. |string| must be
  // of type StringClass.
  template <class StringClass>
  DirectHandle<StringClass> InternalizeExternalString(
      DirectHandle<String> string);

  // Compute the sharing strategy for the input string.
  //
  // Old-generation sequential and thin strings can be shared by mutating their
  // map and return kInPlace, along with the matching shared string map for the
  // string stored in shared_map.
  //
  // Already-shared strings return kAlreadyTransitioned.
  //
  // All other strings are shared by flattening and copying into a sequential
  // string then sharing that sequential string, and return kCopy.
  V8_WARN_UNUSED_RESULT StringTransitionStrategy
  ComputeSharingStrategyForString(DirectHandle<String> string,
                                  MaybeDirectHandle<Map>* shared_map);

  // Create or lookup a single character string made up of a utf16 surrogate
  // pair.
  DirectHandle<String> NewSurrogatePairString(uint16_t lead, uint16_t trail);

  // Create a new string object which holds a proper substring of a string.
  Handle<String> NewProperSubString(DirectHandle<String> str, uint32_t begin,
                                    uint32_t end);
  // Same, but always copies (never creates a SlicedString).
  // {str} must be flat, {length} must be non-zero.
  Handle<String> NewCopiedSubstring(DirectHandle<String> str, uint32_t begin,
                                    uint32_t length);

  // Create a new string object which holds a substring of a string.
  template <typename T, template <typename> typename HandleType>
    requires(std::is_convertible_v<HandleType<T>, HandleType<String>>)
  inline HandleType<String> NewSubString(HandleType<T> str, uint32_t begin,
                                         uint32_t end);

  // Creates a new external String object.  There are two String encodings
  // in the system: one-byte and two-byte.  Unlike other String types, it does
  // not make sense to have a UTF-8 factory function for external strings,
  // because we cannot change the underlying buffer.  Note that these strings
  // are backed by a string resource that resides outside the V8 heap.
  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewExternalStringFromOneByte(
      const v8::String::ExternalOneByteStringResource* resource);
  V8_WARN_UNUSED_RESULT MaybeHandle<String> NewExternalStringFromTwoByte(
      const v8::String::ExternalStringResource* resource);

  // Create a symbol in old or read-only space.
  Handle<Symbol> NewSymbol(AllocationType allocation = AllocationType::kOld);
  Handle<Symbol> NewPrivateSymbol(
      AllocationType allocation = AllocationType::kOld);
  DirectHandle<Symbol> NewPrivateNameSymbol(DirectHandle<String> name);

  // Create a global (but otherwise uninitialized) context.
  Handle<NativeContext> NewNativeContext();

  // Create a script context.
  DirectHandle<Context> NewScriptContext(DirectHandle<NativeContext> outer,
                                         DirectHandle<ScopeInfo> scope_info);

  // Create an empty script context table.
  Handle<ScriptContextTable> NewScriptContextTable();

  // Create a module context.
  DirectHandle<Context> NewModuleContext(DirectHandle<SourceTextModule> module,
                                         DirectHandle<NativeContext> outer,
                                         DirectHandle<ScopeInfo> scope_info);

  // Create a function or eval context.
  DirectHandle<Context> NewFunctionContext(DirectHandle<Context> outer,
                                           DirectHandle<ScopeInfo> scope_info);

  // Create a catch context.
  DirectHandle<Context> NewCatchContext(DirectHandle<Context> previous,
                                        DirectHandle<ScopeInfo> scope_info,
                                        DirectHandle<Object> thrown_object);

  // Create a 'with' context.
  Handle<Context> NewWithContext(DirectHandle<Context> previous,
                                 DirectHandle<ScopeInfo> scope_info,
                                 DirectHandle<JSReceiver> extension);

  Handle<Context> NewDebugEvaluateContext(DirectHandle<Context> previous,
                                          DirectHandle<ScopeInfo> scope_info,
                                          DirectHandle<JSReceiver> extension,
                                          DirectHandle<Context> wrapped);

  // Create a block context.
  DirectHandle<Context> NewBlockContext(DirectHandle<Context> previous,
                                        DirectHandle<ScopeInfo> scope_info);

  // Create a context cell with a const value.
  DirectHandle<ContextCell> NewContextCell(
      DirectHandle<JSAny> value,
      AllocationType allocation_type = AllocationType::kYoung);

  // Create a context that's used by builtin functions.
  //
  // These are similar to function context but don't have a previous
  // context or any scope info. These are used to store spec defined
  // context values.
  DirectHandle<Context> NewBuiltinContext(
      DirectHandle<NativeContext> native_context, int length);

  DirectHandle<AliasedArgumentsEntry> NewAliasedArgumentsEntry(
      int aliased_context_slot);

  DirectHandle<AccessorInfo> NewAccessorInfo();

  DirectHandle<InterceptorInfo> NewInterceptorInfo(
      AllocationType allocation = AllocationType::kOld);

  DirectHandle<ErrorStackData> NewErrorStackData(
      DirectHandle<UnionOf<JSAny, FixedArray>>
          call_site_infos_or_formatted_stack,
      DirectHandle<StackTraceInfo> stack_trace);

  Handle<Script> CloneScript(DirectHandle<Script> script,
                             DirectHandle<String> source);

  DirectHandle<BreakPointInfo> NewBreakPointInfo(int source_position);
  Handle<BreakPoint> NewBreakPoint(int id, DirectHandle<String> condition);

  Handle<CallSiteInfo> NewCallSiteInfo(
      DirectHandle<JSAny> receiver_or_instance,
      DirectHandle<UnionOf<Smi, JSFunction>> function,
      DirectHandle<HeapObject> code_object, int code_offset_or_source_position,
      int flags, DirectHandle<FixedArray> parameters);
  DirectHandle<StackFrameInfo> NewStackFrameInfo(
      DirectHandle<UnionOf<SharedFunctionInfo, Script>> shared_or_script,
      int bytecode_offset_or_source_position,
      DirectHandle<String> function_name, bool is_constructor);
  Handle<StackTraceInfo> NewStackTraceInfo(DirectHandle<FixedArray> frames);

  // Allocate various microtasks.
  DirectHandle<CallableTask> NewCallableTask(DirectHandle<JSReceiver> callable,
                                             DirectHandle<Context> context);
  DirectHandle<CallbackTask> NewCallbackTask(DirectHandle<Foreign> callback,
                                             DirectHandle<Foreign> data);
  DirectHandle<PromiseResolveThenableJobTask> NewPromiseResolveThenableJobTask(
      DirectHandle<JSPromise> promise_to_resolve,
      DirectHandle<JSReceiver> thenable, DirectHandle<JSReceiver> then,
      DirectHandle<Context> context);

  // Foreign objects are pretenured when allocated by the bootstrapper.
  template <ExternalPointerTag tag>
  Handle<Foreign> NewForeign(
      Address addr, AllocationType allocation_type = AllocationType::kYoung);

  Handle<TrustedForeign> NewTrustedForeign(Address addr, bool shared);

  Handle<Cell> NewCell(Tagged<Smi> value);
  Handle<Cell> NewCell();

  Handle<PropertyCell> NewPropertyCell(
      DirectHandle<Name> name, PropertyDetails details,
      DirectHandle<Object> value,
      AllocationType allocation = AllocationType::kOld);
  DirectHandle<PropertyCell> NewProtector();

  DirectHandle<FeedbackCell> NewNoClosuresCell();
  DirectHandle<FeedbackCell> NewOneClosureCell(
      DirectHandle<ClosureFeedbackCellArray> value);
  DirectHandle<FeedbackCell> NewManyClosuresCell(
      AllocationType allocation = AllocationType::kOld);

  DirectHandle<TransitionArray> NewTransitionArray(int number_of_transitions,
                                                   int slack = 0);

  // Allocate a tenured AllocationSite. Its payload is null.
  Handle<AllocationSite> NewAllocationSite(bool with_weak_next);

  // Allocates and initializes a new Map.
  Handle<Map> NewMap(DirectHandle<HeapObject> meta_map_holder,
                     InstanceType type, int instance_size,
                     ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
                     int inobject_properties = 0,
                     AllocationType allocation_type = AllocationType::kMap);

  DirectHandle<Map> NewMapWithMetaMap(
      DirectHandle<Map> meta_map, InstanceType type, int instance_size,
      ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      int inobject_properties = 0,
      AllocationType allocation_type = AllocationType::kMap);

  DirectHandle<Map> NewContextfulMap(
      DirectHandle<JSReceiver> creation_context_holder, InstanceType type,
      int instance_size,
      ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      int inobject_properties = 0,
      AllocationType allocation_type = AllocationType::kMap);

  DirectHandle<Map> NewContextfulMap(
      DirectHandle<NativeContext> native_context, InstanceType type,
      int instance_size,
      ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      int inobject_properties = 0,
      AllocationType allocation_type = AllocationType::kMap);

  Handle<Map> NewContextfulMapForCurrentContext(
      InstanceType type, int instance_size,
      ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      int inobject_properties = 0,
      AllocationType allocation_type = AllocationType::kMap);

  Handle<Map> NewContextlessMap(
      InstanceType type, int instance_size,
      ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      int inobject_properties = 0,
      AllocationType allocation_type = AllocationType::kMap);

  // Initializes the fields of a newly created Map using roots from the
  // passed-in Heap. Exposed for tests and heap setup; other code should just
  // call NewMap which takes care of it.
  Tagged<Map> InitializeMap(Tagged<Map> map, InstanceType type,
                            int instance_size, ElementsKind elements_kind,
                            int inobject_properties, ReadOnlyRoots roots);

  // Allocate a block of memory of the given AllocationType (filled with a
  // filler). Used as a fall-back for generated code when the space is full.
  DirectHandle<HeapObject> NewFillerObject(
      int size, AllocationAlignment alignment, AllocationType allocation,
      AllocationOrigin origin = AllocationOrigin::kRuntime);

  DirectHandle<JSObject> NewFunctionPrototype(
      DirectHandle<JSFunction> function);

  // Returns a deep copy of the JavaScript object.
  // Properties and elements are copied too.
  Handle<JSObject> CopyJSObject(DirectHandle<JSObject> object);
  // Same as above, but also takes an AllocationSite to be appended in an
  // AllocationMemento.
  Handle<JSObject> CopyJSObjectWithAllocationSite(
      DirectHandle<JSObject> object, DirectHandle<AllocationSite> site);

  Handle<FixedArray> CopyFixedArrayWithMap(
      DirectHandle<FixedArray> array, DirectHandle<Map> map,
      AllocationType allocation = AllocationType::kYoung);

  Handle<FixedArray> CopyFixedArrayAndGrow(
      DirectHandle<FixedArray> array, int grow_by,
      AllocationType allocation = AllocationType::kYoung);

  DirectHandle<WeakArrayList> NewWeakArrayList(
      int capacity, AllocationType allocation = AllocationType::kYoung);

  DirectHandle<WeakFixedArray> CopyWeakFixedArray(
      DirectHandle<WeakFixedArray> array);

  DirectHandle<WeakFixedArray> CopyWeakFixedArrayAndGrow(
      DirectHandle<WeakFixedArray> array, int grow_by);

  Handle<WeakArrayList> CopyWeakArrayListAndGrow(
      DirectHandle<WeakArrayList> array, int grow_by,
      AllocationType allocation = AllocationType::kYoung);

  DirectHandle<WeakArrayList> CompactWeakArrayList(
      DirectHandle<WeakArrayList> array, int new_capacity,
      AllocationType allocation = AllocationType::kYoung);

  DirectHandle<PropertyArray> CopyPropertyArrayAndGrow(
      DirectHandle<PropertyArray> array, int grow_by);

  Handle<FixedArray> CopyFixedArrayUpTo(
      DirectHandle<FixedArray> array, int new_len,
      AllocationType allocation = AllocationType::kYoung);

  Handle<FixedArray> CopyFixedArray(Handle<FixedArray> array);

  Handle<FixedDoubleArray> CopyFixedDoubleArray(Handle<FixedDoubleArray> array);

  // Creates a new HeapNumber in read-only space if possible otherwise old
  // space.
  Handle<HeapNumber> NewHeapNumberForCodeAssembler(double value);

  Handle<JSObject> NewArgumentsObject(DirectHandle<JSFunction> callee,
                                      int length);

  // Allocates and initializes a new JavaScript object based on a
  // constructor.
  // JS objects are pretenured when allocated by the bootstrapper and
  // runtime.
  Handle<JSObject> NewJSObject(
      DirectHandle<JSFunction> constructor,
      AllocationType allocation = AllocationType::kYoung,
      NewJSObjectType = NewJSObjectType::kNoAPIWrapper);
  // JSObject without a prototype.
  Handle<JSObject> NewJSObjectWithNullProto();
  // JSObject without a prototype, in dictionary mode.
  Handle<JSObject> NewSlowJSObjectWithNullProto();

  // Global objects are pretenured and initialized based on a constructor.
  Handle<JSGlobalObject> NewJSGlobalObject(
      DirectHandle<JSFunction> constructor);

  // Allocates and initializes a new JavaScript object based on a map.
  // Passing an allocation site means that a memento will be created that
  // points to the site.
  // JS objects are pretenured when allocated by the bootstrapper and
  // runtime.
  Handle<JSObject> NewJSObjectFromMap(
      DirectHandle<Map> map, AllocationType allocation = AllocationType::kYoung,
      DirectHandle<AllocationSite> allocation_site =
          DirectHandle<AllocationSite>::null(),
      NewJSObjectType = NewJSObjectType::kNoAPIWrapper);
  // Like NewJSObjectFromMap, but includes allocating a properties dictionary.);
  Handle<JSObject> NewSlowJSObjectFromMap(
      DirectHandle<Map> map, int number_of_slow_properties,
      AllocationType allocation = AllocationType::kYoung,
      DirectHandle<AllocationSite> allocation_site =
          DirectHandle<AllocationSite>::null(),
      NewJSObjectType = NewJSObjectType::kNoAPIWrapper);
  Handle<JSObject> NewSlowJSObjectFromMap(DirectHandle<Map> map);
  // Calls NewJSObjectFromMap or NewSlowJSObjectFromMap depending on whether the
  // map is a dictionary map.
  inline Handle<JSObject> NewFastOrSlowJSObjectFromMap(
      DirectHandle<Map> map, int number_of_slow_properties,
      AllocationType allocation = AllocationType::kYoung,
      DirectHandle<AllocationSite> allocation_site =
          DirectHandle<AllocationSite>::null(),
      NewJSObjectType = NewJSObjectType::kNoAPIWrapper);
  inline Handle<JSObject> NewFastOrSlowJSObjectFromMap(DirectHandle<Map> map);
  // Allocates and initializes a new JavaScript object with the given
  // {prototype} and {properties}. The newly created object will be
  // in dictionary properties mode. The {elements} can either be the
  // empty fixed array, in which case the resulting object will have
  // fast elements, or a NumberDictionary, in which case the resulting
  // object will have dictionary elements.
  DirectHandle<JSObject> NewSlowJSObjectWithPropertiesAndElements(
      DirectHandle<JSPrototype> prototype, DirectHandle<HeapObject> properties,
      DirectHandle<FixedArrayBase> elements);

  // JS arrays are pretenured when allocated by the parser.

  // Create a JSArray with a specified length and elements initialized
  // according to the specified mode.
  Handle<JSArray> NewJSArray(
      ElementsKind elements_kind, int length, int capacity,
      ArrayStorageAllocationMode mode =
          ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS,
      AllocationType allocation = AllocationType::kYoung);

  Handle<JSArray> NewJSArray(
      int capacity, ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      AllocationType allocation = AllocationType::kYoung) {
    if (capacity != 0) {
      elements_kind = GetHoleyElementsKind(elements_kind);
    }
    return NewJSArray(
        elements_kind, 0, capacity,
        ArrayStorageAllocationMode::INITIALIZE_ARRAY_ELEMENTS_WITH_HOLE,
        allocation);
  }

  // Create a JSArray with the given elements.
  Handle<JSArray> NewJSArrayWithElements(
      DirectHandle<FixedArrayBase> elements, ElementsKind elements_kind,
      int length, AllocationType allocation = AllocationType::kYoung);

  inline Handle<JSArray> NewJSArrayWithElements(
      DirectHandle<FixedArrayBase> elements,
      ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      AllocationType allocation = AllocationType::kYoung);

  DirectHandle<JSArray> NewJSArrayForTemplateLiteralArray(
      DirectHandle<FixedArray> cooked_strings,
      DirectHandle<FixedArray> raw_strings, int function_literal_id,
      int slot_id);

  void NewJSArrayStorage(
      DirectHandle<JSArray> array, int length, int capacity,
      ArrayStorageAllocationMode mode =
          ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS);

  Handle<JSWeakMap> NewJSWeakMap();

  DirectHandle<JSGeneratorObject> NewJSGeneratorObject(
      DirectHandle<JSFunction> function);

  DirectHandle<JSModuleNamespace> NewJSModuleNamespace();

  DirectHandle<JSWrappedFunction> NewJSWrappedFunction(
      DirectHandle<NativeContext> creation_context,
      DirectHandle<Object> target);

  DirectHandle<JSDisposableStackBase> NewJSDisposableStackBase();
  DirectHandle<JSSyncDisposableStack> NewJSSyncDisposableStack(
      DirectHandle<Map> map);
  DirectHandle<JSAsyncDisposableStack> NewJSAsyncDisposableStack(
      DirectHandle<Map> map);

#if V8_ENABLE_WEBASSEMBLY
  DirectHandle<WasmTrustedInstanceData> NewWasmTrustedInstanceData(bool shared);
  DirectHandle<WasmDispatchTable> NewWasmDispatchTable(
      int length, wasm::CanonicalValueType table_type, bool shared);
  DirectHandle<WasmTypeInfo> NewWasmTypeInfo(
      wasm::CanonicalValueType type, wasm::CanonicalValueType element_type,
      DirectHandle<Map> opt_parent, int num_supertypes, bool shared);
  DirectHandle<WasmInternalFunction> NewWasmInternalFunction(
      DirectHandle<TrustedObject> ref, int function_index, bool shared,
      WasmCodePointer call_target);
  DirectHandle<WasmFuncRef> NewWasmFuncRef(
      DirectHandle<WasmInternalFunction> internal_function,
      DirectHandle<Map> rtt, bool shared);
  DirectHandle<WasmCapiFunctionData> NewWasmCapiFunctionData(
      Address call_target, DirectHandle<Foreign> embedder_data,
      DirectHandle<Code> wrapper_code, DirectHandle<Map> rtt,
      wasm::CanonicalTypeIndex sig_index, const wasm::CanonicalSig* sig);
  DirectHandle<WasmExportedFunctionData> NewWasmExportedFunctionData(
      DirectHandle<Code> export_wrapper,
      DirectHandle<WasmTrustedInstanceData> instance_data,
      DirectHandle<WasmFuncRef> func_ref,
      DirectHandle<WasmInternalFunction> internal_function,
      const wasm::CanonicalSig* sig, wasm::CanonicalTypeIndex type_index,
      int wrapper_budget, wasm::Promise promise);
  DirectHandle<WasmImportData> NewWasmImportData(
      DirectHandle<HeapObject> callable, wasm::Suspend suspend,
      MaybeDirectHandle<WasmTrustedInstanceData> instance_data,
      const wasm::CanonicalSig* sig, bool shared);
  DirectHandle<WasmImportData> NewWasmImportData(
      DirectHandle<WasmImportData> ref, bool shared);

  DirectHandle<WasmFastApiCallData> NewWasmFastApiCallData(
      DirectHandle<HeapObject> signature, DirectHandle<Object> callback_data);

  // {opt_call_target} is kNullAddress for JavaScript functions, and
  // non-null for exported Wasm functions.
  DirectHandle<WasmJSFunctionData> NewWasmJSFunctionData(
      wasm::CanonicalTypeIndex sig_index, DirectHandle<JSReceiver> callable,
      DirectHandle<Code> wrapper_code, DirectHandle<Map> rtt,
      wasm::Suspend suspend, wasm::Promise promise,
      std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle);
  DirectHandle<WasmResumeData> NewWasmResumeData(
      DirectHandle<WasmSuspenderObject> suspender, wasm::OnResume on_resume);
  DirectHandle<WasmSuspenderObject> NewWasmSuspenderObject();
  DirectHandle<WasmStruct> NewWasmStruct(const wasm::StructType* type,
                                         wasm::WasmValue* args,
                                         DirectHandle<Map> map);
  // The resulting struct will be uninitialized, which means GC might fail for
  // reference structs until initialization. Follow this up with a
  // {DisallowGarbageCollection} scope until initialization.
  Handle<WasmStruct> NewWasmStructUninitialized(
      const wasm::StructType* type, DirectHandle<Map> map,
      AllocationType allocation = AllocationType::kYoung);

  DirectHandle<WasmArray> NewWasmArray(wasm::ValueType element_type,
                                       uint32_t length,
                                       wasm::WasmValue initial_value,
                                       DirectHandle<Map> map);
  DirectHandle<WasmArray> NewWasmArrayFromElements(
      const wasm::ArrayType* type, base::Vector<wasm::WasmValue> elements,
      DirectHandle<Map> map);
  DirectHandle<WasmArray> NewWasmArrayFromMemory(
      uint32_t length, DirectHandle<Map> map,
      wasm::CanonicalValueType element_type, Address source);
  // Returns a handle to a WasmArray if successful, or a Smi containing a
  // {MessageTemplate} if computing the array's elements leads to an error.
  DirectHandle<Object> NewWasmArrayFromElementSegment(
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      DirectHandle<WasmTrustedInstanceData> shared_trusted_instance_data,
      uint32_t segment_index, uint32_t start_offset, uint32_t length,
      DirectHandle<Map> map, wasm::CanonicalValueType element_type);

  DirectHandle<SharedFunctionInfo> NewSharedFunctionInfoForWasmExportedFunction(
      DirectHandle<String> name, DirectHandle<WasmExportedFunctionData> data,
      int len, AdaptArguments adapt);
  DirectHandle<SharedFunctionInfo> NewSharedFunctionInfoForWasmJSFunction(
      DirectHandle<String> name, DirectHandle<WasmJSFunctionData> data);
  DirectHandle<SharedFunctionInfo> NewSharedFunctionInfoForWasmResume(
      DirectHandle<WasmResumeData> data);
  DirectHandle<SharedFunctionInfo> NewSharedFunctionInfoForWasmCapiFunction(
      DirectHandle<WasmCapiFunctionData> data);
#endif  // V8_ENABLE_WEBASSEMBLY

  DirectHandle<SourceTextModule> NewSourceTextModule(
      DirectHandle<SharedFunctionInfo> code);
  Handle<SyntheticModule> NewSyntheticModule(
      DirectHandle<String> module_name, DirectHandle<FixedArray> export_names,
      v8::Module::SyntheticModuleEvaluationSteps evaluation_steps);

  Handle<JSArrayBuffer> NewJSArrayBuffer(
      std::shared_ptr<BackingStore> backing_store,
      AllocationType allocation = AllocationType::kYoung);

  MaybeHandle<JSArrayBuffer> NewJSArrayBufferAndBackingStore(
      size_t byte_length, InitializedFlag initialized,
      AllocationType allocation = AllocationType::kYoung);

  MaybeHandle<JSArrayBuffer> NewJSArrayBufferAndBackingStore(
      size_t byte_length, size_t max_byte_length, InitializedFlag initialized,
      ResizableFlag resizable = ResizableFlag::kNotResizable,
      AllocationType allocation = AllocationType::kYoung);

  Handle<JSArrayBuffer> NewJSSharedArrayBuffer(
      std::shared_ptr<BackingStore> backing_store);

  static void TypeAndSizeForElementsKind(ElementsKind kind,
                                         ExternalArrayType* array_type,
                                         size_t* element_size);

  // Creates a new JSTypedArray with the specified buffer.
  Handle<JSTypedArray> NewJSTypedArray(ExternalArrayType type,
                                       DirectHandle<JSArrayBuffer> buffer,
                                       size_t byte_offset, size_t length,
                                       bool is_length_tracking = false);

  Handle<JSDataViewOrRabGsabDataView> NewJSDataViewOrRabGsabDataView(
      DirectHandle<JSArrayBuffer> buffer, size_t byte_offset,
      size_t byte_length, bool is_length_tracking = false);

  DirectHandle<JSUint8ArraySetFromResult> NewJSUint8ArraySetFromResult(
      DirectHandle<Number> read, DirectHandle<Number> written);

  DirectHandle<JSIteratorResult> NewJSIteratorResult(DirectHandle<Object> value,
                                                     bool done);
  DirectHandle<JSAsyncFromSyncIterator> NewJSAsyncFromSyncIterator(
      DirectHandle<JSReceiver> sync_iterator, DirectHandle<Object> next);

  DirectHandle<JSMap> NewJSMap();
  DirectHandle<JSSet> NewJSSet();

  // Allocates a bound function. If direct handles are enabled, it is the
  // responsibility of the caller to ensure that the memory pointed to by
  // `bound_args` is scanned during CSS, e.g., it comes from a
  // `DirectHandleVector<Object>`.
  MaybeDirectHandle<JSBoundFunction> NewJSBoundFunction(
      DirectHandle<JSReceiver> target_function, DirectHandle<JSAny> bound_this,
      base::Vector<DirectHandle<Object>> bound_args,
      DirectHandle<JSPrototype> prototype);

  // Allocates a Harmony proxy.
  Handle<JSProxy> NewJSProxy(DirectHandle<JSReceiver> target,
                             DirectHandle<JSReceiver> handler, bool revocable);

  // Reinitialize an JSGlobalProxy based on a constructor.  The object
  // must have the same size as objects allocated using the
  // constructor.  The object is reinitialized and behaves as an
  // object that has been freshly allocated using the constructor.
  void ReinitializeJSGlobalProxy(DirectHandle<JSGlobalProxy> global,
                                 DirectHandle<JSFunction> constructor);

  Handle<JSGlobalProxy> NewUninitializedJSGlobalProxy(int size);

  // For testing only. Creates a sloppy function without code.
  Handle<JSFunction> NewFunctionForTesting(DirectHandle<String> name);

  // Create an External object for V8's external API.
  Handle<JSObject> NewExternal(
      void* value, AllocationType allocation = AllocationType::kYoung);

  // Create a CppHeapExternal object for V8's external API.
  Handle<CppHeapExternalObject> NewCppHeapExternal(
      AllocationType allocation = AllocationType::kYoung);

  // Allocates a new code object and initializes it to point to the given
  // off-heap entry point.
  //
  // Note it wouldn't be strictly necessary to create new Code objects, instead
  // Code::instruction_start and instruction_stream could be reset. But creating
  // a new Code object doesn't hurt much (it only makes mksnapshot slightly more
  // expensive) and has real benefits:
  // - it moves all special-casing to mksnapshot-time; at normal runtime, the
  //   system only sees a) non-builtin Code objects (not in RO space, with
  //   instruction_stream set) and b) builtin Code objects (maybe in RO space,
  //   without instruction_stream set).
  // - it's a convenient bottleneck to make the RO-space allocation decision.
  DirectHandle<Code> NewCodeObjectForEmbeddedBuiltin(DirectHandle<Code> code,
                                                     Address off_heap_entry);

  DirectHandle<BytecodeArray> CopyBytecodeArray(DirectHandle<BytecodeArray>);

  // Interface for creating error objects.
  Handle<JSObject> NewError(DirectHandle<JSFunction> constructor,
                            DirectHandle<String> message,
                            DirectHandle<Object> options = {});

  DirectHandle<Object> NewInvalidStringLengthError();

  inline DirectHandle<Object> NewURIError();

  Handle<JSObject> NewError(DirectHandle<JSFunction> constructor,
                            MessageTemplate template_index,
                            base::Vector<const DirectHandle<Object>> args);

  DirectHandle<JSObject> NewSuppressedErrorAtDisposal(
      Isolate* isolate, DirectHandle<Object> error,
      DirectHandle<Object> suppressed_error);

  template <typename... Args>
  DirectHandle<JSObject> NewError(DirectHandle<JSFunction> constructor,
                                  MessageTemplate template_index, Args... args)
    requires(
        std::conjunction_v<std::is_convertible<Args, DirectHandle<Object>>...>)
  {
    return NewError(constructor, template_index,
                    base::VectorOf<DirectHandle<Object>>({args...}));
  }

  // https://tc39.es/proposal-shadowrealm/#sec-create-type-error-copy
  DirectHandle<JSObject> ShadowRealmNewTypeErrorCopy(
      DirectHandle<Object> original, MessageTemplate template_index,
      base::Vector<const DirectHandle<Object>> args);

  template <typename... Args>
  DirectHandle<JSObject> ShadowRealmNewTypeErrorCopy(
      DirectHandle<Object> original, MessageTemplate template_index,
      Args... args)
    requires(
        std::conjunction_v<std::is_convertible<Args, DirectHandle<Object>>...>)
  {
    return ShadowRealmNewTypeErrorCopy(
        original, template_index,
        base::VectorOf<DirectHandle<Object>>({args...}));
  }

#define DECLARE_ERROR(NAME)                                                  \
  Handle<JSObject> New##NAME(MessageTemplate template_index,                 \
                             base::Vector<const DirectHandle<Object>> args); \
                                                                             \
  template <typename... Args>                                                \
    requires(std::is_convertible_v<Args, DirectHandle<Object>> && ...)       \
  Handle<JSObject> New##NAME(MessageTemplate template_index, Args... args) { \
    return New##NAME(template_index,                                         \
                     base::VectorOf<DirectHandle<Object>>({args...}));       \
  }
  DECLARE_ERROR(Error)
  DECLARE_ERROR(EvalError)
  DECLARE_ERROR(RangeError)
  DECLARE_ERROR(ReferenceError)
  DECLARE_ERROR(SuppressedError)
  DECLARE_ERROR(SyntaxError)
  DECLARE_ERROR(TypeError)
  DECLARE_ERROR(WasmCompileError)
  DECLARE_ERROR(WasmLinkError)
  DECLARE_ERROR(WasmSuspendError)
  DECLARE_ERROR(WasmRuntimeError)
  DECLARE_ERROR(WasmExceptionError)
#undef DECLARE_ERROR

  Handle<String> SizeToString(size_t value, bool check_cache = true);
  inline DirectHandle<String> Uint32ToString(uint32_t value,
                                             bool check_cache = true) {
    return SizeToString(value, check_cache);
  }

#define ROOT_ACCESSOR(Type, name, CamelName) inline Handle<Type> name();
  MUTABLE_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

  // Allocates a new SharedFunctionInfo object.
  Handle<SharedFunctionInfo> NewSharedFunctionInfoForApiFunction(
      MaybeDirectHandle<String> maybe_name,
      DirectHandle<FunctionTemplateInfo> function_template_info,
      FunctionKind kind);

  Handle<SharedFunctionInfo> NewSharedFunctionInfoForBuiltin(
      MaybeDirectHandle<String> name, Builtin builtin, int len,
      AdaptArguments adapt, FunctionKind kind = FunctionKind::kNormalFunction);

  DirectHandle<InterpreterData> NewInterpreterData(
      DirectHandle<BytecodeArray> bytecode_array, DirectHandle<Code> code);

  static bool IsFunctionModeWithPrototype(FunctionMode function_mode) {
    return (function_mode & kWithPrototypeBits) != 0;
  }

  static bool IsFunctionModeWithWritablePrototype(FunctionMode function_mode) {
    return (function_mode & kWithWritablePrototypeBit) != 0;
  }

  static bool IsFunctionModeWithName(FunctionMode function_mode) {
    return (function_mode & kWithNameBit) != 0;
  }

  DirectHandle<Map> CreateSloppyFunctionMap(
      FunctionMode function_mode,
      MaybeDirectHandle<JSFunction> maybe_empty_function);

  DirectHandle<Map> CreateStrictFunctionMap(
      FunctionMode function_mode, DirectHandle<JSFunction> empty_function);

  DirectHandle<Map> CreateClassFunctionMap(
      DirectHandle<JSFunction> empty_function);

  // Allocates a new JSMessageObject object.
  Handle<JSMessageObject> NewJSMessageObject(
      MessageTemplate message, DirectHandle<Object> argument,
      int start_position, int end_position,
      DirectHandle<SharedFunctionInfo> shared_info, int bytecode_offset,
      DirectHandle<Script> script,
      DirectHandle<StackTraceInfo> stack_trace =
          DirectHandle<StackTraceInfo>::null());

  Handle<DebugInfo> NewDebugInfo(DirectHandle<SharedFunctionInfo> shared);

  // Return a map for given number of properties using the map cache in the
  // native context.
  Handle<Map> ObjectLiteralMapFromCache(
      DirectHandle<NativeContext> native_context, int number_of_properties);

  Handle<LoadHandler> NewLoadHandler(
      int data_count, AllocationType allocation = AllocationType::kOld);
  Handle<StoreHandler> NewStoreHandler(int data_count);
  DirectHandle<MegaDomHandler> NewMegaDomHandler(
      MaybeObjectDirectHandle accessor, MaybeObjectDirectHandle context);

  // Creates a new FixedArray that holds the data associated with the
  // atom regexp and stores it in the regexp.
  void SetRegExpAtomData(DirectHandle<JSRegExp> regexp,
                         DirectHandle<String> source, JSRegExp::Flags flags,
                         DirectHandle<String> match_pattern);

  // Creates a new FixedArray that holds the data associated with the
  // irregexp regexp and stores it in the regexp.
  void SetRegExpIrregexpData(DirectHandle<JSRegExp> regexp,
                             DirectHandle<String> source, JSRegExp::Flags flags,
                             int capture_count, uint32_t backtrack_limit);

  // Creates a new FixedArray that holds the data associated with the
  // experimental regexp and stores it in the regexp.
  void SetRegExpExperimentalData(DirectHandle<JSRegExp> regexp,
                                 DirectHandle<String> source,
                                 JSRegExp::Flags flags, int capture_count);

  DirectHandle<RegExpData> NewAtomRegExpData(DirectHandle<String> source,
                                             JSRegExp::Flags flags,
                                             DirectHandle<String> pattern);
  DirectHandle<RegExpData> NewIrRegExpData(DirectHandle<String> source,
                                           JSRegExp::Flags flags,
                                           int capture_count,
                                           uint32_t backtrack_limit);
  DirectHandle<RegExpData> NewExperimentalRegExpData(
      DirectHandle<String> source, JSRegExp::Flags flags, int capture_count);

  // Returns the value for a known global constant (a property of the global
  // object which is neither configurable nor writable) like 'undefined'.
  // Returns a null handle when the given name is unknown.
  DirectHandle<Object> GlobalConstantFor(DirectHandle<Name> name);

  // Converts the given ToPrimitive hint to its string representation.
  DirectHandle<String> ToPrimitiveHintString(ToPrimitiveHint hint);

  Handle<JSPromise> NewJSPromiseWithoutHook();
  Handle<JSPromise> NewJSPromise();

  Tagged<HeapObject> NewForTest(DirectHandle<Map> map,
                                AllocationType allocation) {
    return New(map, allocation);
  }

  DirectHandle<JSSharedStruct> NewJSSharedStruct(
      DirectHandle<JSFunction> constructor,
      MaybeDirectHandle<NumberDictionary> maybe_elements_template);

  DirectHandle<JSSharedArray> NewJSSharedArray(
      DirectHandle<JSFunction> constructor, int length);

  Handle<JSAtomicsMutex> NewJSAtomicsMutex();

  Handle<JSAtomicsCondition> NewJSAtomicsCondition();

  DirectHandle<FunctionTemplateInfo> NewFunctionTemplateInfo(int length,
                                                             bool do_not_cache);

  DirectHandle<ObjectTemplateInfo> NewObjectTemplateInfo(
      DirectHandle<FunctionTemplateInfo> constructor, bool do_not_cache);

  DirectHandle<DictionaryTemplateInfo> NewDictionaryTemplateInfo(
      DirectHandle<FixedArray> property_names);

  // Helper class for creating JSFunction objects.
  class V8_EXPORT_PRIVATE JSFunctionBuilder final {
   public:
    JSFunctionBuilder(Isolate* isolate, DirectHandle<SharedFunctionInfo> sfi,
                      DirectHandle<Context> context);

    V8_WARN_UNUSED_RESULT Handle<JSFunction> Build();

    JSFunctionBuilder& set_map(DirectHandle<Map> v) {
      maybe_map_ = v;
      return *this;
    }
    JSFunctionBuilder& set_allocation_type(AllocationType v) {
      allocation_type_ = v;
      return *this;
    }
    JSFunctionBuilder& set_feedback_cell(DirectHandle<FeedbackCell> v) {
      maybe_feedback_cell_ = v;
      return *this;
    }

   private:
    V8_WARN_UNUSED_RESULT Handle<JSFunction> BuildRaw(DirectHandle<Code> code);

    Isolate* const isolate_;
    DirectHandle<SharedFunctionInfo> sfi_;
    DirectHandle<Context> context_;
    MaybeDirectHandle<Map> maybe_map_;
    MaybeDirectHandle<FeedbackCell> maybe_feedback_cell_;
    AllocationType allocation_type_ = AllocationType::kOld;

    friend class Factory;
  };

  // Allows creation of InstructionStream objects. It provides two build
  // methods, one of which tries to gracefully handle allocation failure.
  class V8_EXPORT_PRIVATE CodeBuilder final {
   public:
    CodeBuilder(Isolate* isolate, const CodeDesc& desc, CodeKind kind);

    // TODO(victorgomes): Remove Isolate dependency from CodeBuilder.
    CodeBuilder(LocalIsolate* local_isolate, const CodeDesc& desc,
                CodeKind kind);

    // Builds a new code object (fully initialized). All header fields of the
    // associated InstructionStream are immutable and the InstructionStream
    // object is write protected.
    V8_WARN_UNUSED_RESULT Handle<Code> Build();
    // Like Build, builds a new code object. May return an empty handle if the
    // allocation fails.
    V8_WARN_UNUSED_RESULT MaybeHandle<Code> TryBuild();

    // Sets the self-reference object in which a reference to the code object is
    // stored. This allows generated code to reference its own InstructionStream
    // object by using this handle.
    CodeBuilder& set_self_reference(Handle<Object> self_reference) {
      DCHECK(!self_reference.is_null());
      self_reference_ = self_reference;
      return *this;
    }

    CodeBuilder& set_builtin(Builtin builtin) {
      DCHECK_IMPLIES(builtin != Builtin::kNoBuiltinId,
                     !CodeKindIsJSFunction(kind_));
      builtin_ = builtin;
      return *this;
    }

    CodeBuilder& set_inlined_bytecode_size(uint32_t size) {
      DCHECK_IMPLIES(size != 0, CodeKindIsOptimizedJSFunction(kind_));
      inlined_bytecode_size_ = size;
      return *this;
    }

    CodeBuilder& set_osr_offset(BytecodeOffset offset) {
      DCHECK_IMPLIES(!offset.IsNone(), CodeKindCanOSR(kind_));
      osr_offset_ = offset;
      return *this;
    }

    CodeBuilder& set_source_position_table(Handle<TrustedByteArray> table) {
      DCHECK_NE(kind_, CodeKind::BASELINE);
      DCHECK(!table.is_null());
      source_position_table_ = table;
      return *this;
    }

    inline CodeBuilder& set_empty_source_position_table();

    CodeBuilder& set_bytecode_offset_table(Handle<TrustedByteArray> table) {
      DCHECK_EQ(kind_, CodeKind::BASELINE);
      DCHECK(!table.is_null());
      bytecode_offset_table_ = table;
      return *this;
    }

    CodeBuilder& set_deoptimization_data(
        Handle<DeoptimizationData> deopt_data) {
      DCHECK_NE(kind_, CodeKind::BASELINE);
      DCHECK(!deopt_data.is_null());
      deoptimization_data_ = deopt_data;
      return *this;
    }

    inline CodeBuilder& set_interpreter_data(
        Handle<TrustedObject> interpreter_data);

    CodeBuilder& set_is_context_specialized() {
      DCHECK(!CodeKindIsUnoptimizedJSFunction(kind_));
      is_context_specialized_ = true;
      return *this;
    }

    CodeBuilder& set_is_turbofanned() {
      DCHECK(!CodeKindIsUnoptimizedJSFunction(kind_));
      is_turbofanned_ = true;
      return *this;
    }

    CodeBuilder& set_stack_slots(int stack_slots) {
      stack_slots_ = stack_slots;
      return *this;
    }

    CodeBuilder& set_parameter_count(uint16_t parameter_count) {
      parameter_count_ = parameter_count;
      return *this;
    }

    CodeBuilder& set_profiler_data(BasicBlockProfilerData* profiler_data) {
      profiler_data_ = profiler_data;
      return *this;
    }

   private:
    MaybeHandle<Code> BuildInternal(bool retry_allocation_or_fail);

    DirectHandle<TrustedByteArray> NewTrustedByteArray(int length);
    // Return an allocation suitable for InstructionStreams but without writing
    // the map.
    Tagged<HeapObject> AllocateUninitializedInstructionStream(
        bool retry_allocation_or_fail);
    Handle<Code> NewCode(const NewCodeOptions& options);

    Isolate* const isolate_;
    LocalIsolate* local_isolate_;
    const CodeDesc& code_desc_;
    const CodeKind kind_;

    MaybeHandle<Object> self_reference_;
    Builtin builtin_ = Builtin::kNoBuiltinId;
    uint32_t inlined_bytecode_size_ = 0;
    BytecodeOffset osr_offset_ = BytecodeOffset::None();
    MaybeHandle<TrustedByteArray> bytecode_offset_table_;
    MaybeHandle<TrustedByteArray> source_position_table_;
    MaybeHandle<DeoptimizationData> deoptimization_data_;
    MaybeHandle<TrustedObject> interpreter_data_;
    BasicBlockProfilerData* profiler_data_ = nullptr;
    bool is_context_specialized_ = false;
    bool is_turbofanned_ = false;
    uint32_t stack_slots_ = 0;
    uint16_t parameter_count_ = 0;
  };

 private:
  friend class FactoryBase<Factory>;

  // ------
  // Customization points for FactoryBase
  Tagged<HeapObject> AllocateRaw(int size, AllocationType allocation,
                                 AllocationAlignment alignment = kTaggedAligned,
                                 AllocationHint hint = AllocationHint());

  Isolate* isolate() const {
    // Downcast to the privately inherited sub-class using c-style casts to
    // avoid undefined behavior (as static_cast cannot cast across private
    // bases).
    // NOLINTNEXTLINE (google-readability-casting)
    return (Isolate*)this;  // NOLINT(readability/casting)
  }

  V8_INLINE HeapAllocator* allocator() const;

  bool CanAllocateInReadOnlySpace();
  bool EmptyStringRootIsInitialized();
  AllocationType AllocationTypeForInPlaceInternalizableString();

  void ProcessNewScript(DirectHandle<Script> shared,
                        ScriptEventType script_event_type);
  // ------

  // MetaMapProviderFunc is supposed to be a function returning Tagged<Map>.
  // For example,  std::function<Tagged<Map>()>.
  template <typename MetaMapProviderFunc>
  V8_INLINE Handle<Map> NewMapImpl(
      MetaMapProviderFunc&& meta_map_provider, InstanceType type,
      int instance_size,
      ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND,
      int inobject_properties = 0,
      AllocationType allocation_type = AllocationType::kMap);

  Tagged<HeapObject> AllocateRawWithAllocationSite(
      DirectHandle<Map> map, AllocationType allocation,
      DirectHandle<AllocationSite> allocation_site);

  Handle<JSArrayBufferView> NewJSArrayBufferView(
      DirectHandle<Map> map, DirectHandle<FixedArrayBase> elements,
      DirectHandle<JSArrayBuffer> buffer, size_t byte_offset,
      size_t byte_length);

  Tagged<Symbol> NewSymbolInternal(
      AllocationType allocation = AllocationType::kOld);

  // Allocates new context with given map, sets length and initializes the
  // after-header part with uninitialized values and leaves the context header
  // uninitialized.
  Tagged<Context> NewContextInternal(DirectHandle<Map> map, int size,
                                     int variadic_part_length,
                                     AllocationType allocation);

  template <typename T>
  Handle<T> AllocateSmallOrderedHashTable(DirectHandle<Map> map, int capacity,
                                          AllocationType allocation);

  // Creates a heap object based on the map. The fields of the heap object are
  // not initialized, it's the responsibility of the caller to do that.
  Tagged<HeapObject> New(DirectHandle<Map> map, AllocationType allocation);

  template <typename T>
  Handle<T> CopyArrayWithMap(
      DirectHandle<T> src, DirectHandle<Map> map,
      AllocationType allocation = AllocationType::kYoung);
  template <typename T>
  Handle<T> CopyArrayAndGrow(DirectHandle<T> src, int grow_by,
                             AllocationType allocation);

  MaybeHandle<String> NewStringFromTwoByte(const base::uc16* string, int length,
                                           AllocationType allocation);

  // Creates a new JSArray with the given backing storage. Performs no
  // verification of the backing storage because it may not yet be filled.
  Handle<JSArray> NewJSArrayWithUnverifiedElements(
      DirectHandle<FixedArrayBase> elements, ElementsKind elements_kind,
      int length, AllocationType allocation = AllocationType::kYoung);
  Handle<JSArray> NewJSArrayWithUnverifiedElements(
      DirectHandle<Map> map, DirectHandle<FixedArrayBase> elements, int length,
      AllocationType allocation = AllocationType::kYoung);

  // Creates the backing storage for a JSArray. This handle must be discarded
  // before returning the JSArray reference to code outside Factory, which might
  // decide to left-trim the backing store. To avoid unnecessary HandleScopes,
  // this method requires capacity greater than zero.
  DirectHandle<FixedArrayBase> NewJSArrayStorage(
      ElementsKind elements_kind, int capacity,
      ArrayStorageAllocationMode mode =
          ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS);

  void InitializeAllocationMemento(Tagged<AllocationMemento> memento,
                                   Tagged<AllocationSite> allocation_site);

  // Initializes a JSObject based on its map.
  void InitializeJSObjectFromMap(
      Tagged<JSObject> obj, Tagged<Object> properties, Tagged<Map> map,
      NewJSObjectType = NewJSObjectType::kNoAPIWrapper);
  // Initializes JSObject body starting at given offset.
  void InitializeJSObjectBody(Tagged<JSObject> obj, Tagged<Map> map,
                              int start_offset);

  Handle<WeakArrayList> NewUninitializedWeakArrayList(
      int capacity, AllocationType allocation = AllocationType::kYoung);

#if V8_ENABLE_WEBASSEMBLY
  // The resulting array will be uninitialized, which means GC might fail for
  // reference arrays until initialization. Follow this up with a
  // {DisallowGarbageCollection} scope until initialization.
  Tagged<WasmArray> NewWasmArrayUninitialized(uint32_t length,
                                              DirectHandle<Map> map);

#if V8_ENABLE_DRUMBRAKE
  // WasmInterpreterRuntime needs to call NewWasmArrayUninitialized.
  friend class wasm::WasmInterpreterRuntime;
#endif  // V8_ENABLE_DRUMBRAKE
#endif  // V8_ENABLE_WEBASSEMBLY
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_FACTORY_H_
