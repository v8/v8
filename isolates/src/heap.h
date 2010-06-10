// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_HEAP_H_
#define V8_HEAP_H_

#include <math.h>

#include "splay-tree-inl.h"
#include "v8-counters.h"

namespace v8 {
namespace internal {

// TODO(isolates): remove HEAP here
#define HEAP (_inline_get_heap_())
class Heap;
inline Heap* _inline_get_heap_();

// Forward declarations.
class ZoneScopeInfo;

// Defines all the roots in Heap.
#define UNCONDITIONAL_STRONG_ROOT_LIST(V)                                      \
  /* Put the byte array map early.  We need it to be in place by the time   */ \
  /* the deserializer hits the next page, since it wants to put a byte      */ \
  /* array in the unused space at the end of the page.                      */ \
  V(Map, byte_array_map, ByteArrayMap)                                         \
  V(Map, one_pointer_filler_map, OnePointerFillerMap)                          \
  V(Map, two_pointer_filler_map, TwoPointerFillerMap)                          \
  /* Cluster the most popular ones in a few cache lines here at the top.    */ \
  V(Smi, stack_limit, StackLimit)                                              \
  V(Object, undefined_value, UndefinedValue)                                   \
  V(Object, the_hole_value, TheHoleValue)                                      \
  V(Object, null_value, NullValue)                                             \
  V(Object, true_value, TrueValue)                                             \
  V(Object, false_value, FalseValue)                                           \
  V(Map, heap_number_map, HeapNumberMap)                                       \
  V(Map, global_context_map, GlobalContextMap)                                 \
  V(Map, fixed_array_map, FixedArrayMap)                                       \
  V(Object, no_interceptor_result_sentinel, NoInterceptorResultSentinel)       \
  V(Map, meta_map, MetaMap)                                                    \
  V(Object, termination_exception, TerminationException)                       \
  V(Map, hash_table_map, HashTableMap)                                         \
  V(FixedArray, empty_fixed_array, EmptyFixedArray)                            \
  V(Map, string_map, StringMap)                                                \
  V(Map, ascii_string_map, AsciiStringMap)                                     \
  V(Map, symbol_map, SymbolMap)                                                \
  V(Map, ascii_symbol_map, AsciiSymbolMap)                                     \
  V(Map, cons_symbol_map, ConsSymbolMap)                                       \
  V(Map, cons_ascii_symbol_map, ConsAsciiSymbolMap)                            \
  V(Map, external_symbol_map, ExternalSymbolMap)                               \
  V(Map, external_ascii_symbol_map, ExternalAsciiSymbolMap)                    \
  V(Map, cons_string_map, ConsStringMap)                                       \
  V(Map, cons_ascii_string_map, ConsAsciiStringMap)                            \
  V(Map, external_string_map, ExternalStringMap)                               \
  V(Map, external_ascii_string_map, ExternalAsciiStringMap)                    \
  V(Map, undetectable_string_map, UndetectableStringMap)                       \
  V(Map, undetectable_ascii_string_map, UndetectableAsciiStringMap)            \
  V(Map, pixel_array_map, PixelArrayMap)                                       \
  V(Map, external_byte_array_map, ExternalByteArrayMap)                        \
  V(Map, external_unsigned_byte_array_map, ExternalUnsignedByteArrayMap)       \
  V(Map, external_short_array_map, ExternalShortArrayMap)                      \
  V(Map, external_unsigned_short_array_map, ExternalUnsignedShortArrayMap)     \
  V(Map, external_int_array_map, ExternalIntArrayMap)                          \
  V(Map, external_unsigned_int_array_map, ExternalUnsignedIntArrayMap)         \
  V(Map, external_float_array_map, ExternalFloatArrayMap)                      \
  V(Map, context_map, ContextMap)                                              \
  V(Map, catch_context_map, CatchContextMap)                                   \
  V(Map, code_map, CodeMap)                                                    \
  V(Map, oddball_map, OddballMap)                                              \
  V(Map, global_property_cell_map, GlobalPropertyCellMap)                      \
  V(Map, shared_function_info_map, SharedFunctionInfoMap)                      \
  V(Map, proxy_map, ProxyMap)                                                  \
  V(Object, nan_value, NanValue)                                               \
  V(Object, minus_zero_value, MinusZeroValue)                                  \
  V(Object, instanceof_cache_function, InstanceofCacheFunction)                \
  V(Object, instanceof_cache_map, InstanceofCacheMap)                          \
  V(Object, instanceof_cache_answer, InstanceofCacheAnswer)                    \
  V(String, empty_string, EmptyString)                                         \
  V(DescriptorArray, empty_descriptor_array, EmptyDescriptorArray)             \
  V(Map, neander_map, NeanderMap)                                              \
  V(JSObject, message_listeners, MessageListeners)                             \
  V(Proxy, prototype_accessors, PrototypeAccessors)                            \
  V(NumberDictionary, code_stubs, CodeStubs)                                   \
  V(NumberDictionary, non_monomorphic_cache, NonMonomorphicCache)              \
  V(Code, js_entry_code, JsEntryCode)                                          \
  V(Code, js_construct_entry_code, JsConstructEntryCode)                       \
  V(Code, c_entry_code, CEntryCode)                                            \
  V(FixedArray, number_string_cache, NumberStringCache)                        \
  V(FixedArray, single_character_string_cache, SingleCharacterStringCache)     \
  V(FixedArray, natives_source_cache, NativesSourceCache)                      \
  V(Object, last_script_id, LastScriptId)                                      \
  V(Script, empty_script, EmptyScript)                                         \
  V(Smi, real_stack_limit, RealStackLimit)                                     \

#if V8_TARGET_ARCH_ARM && !V8_INTERPRETED_REGEXP
#define STRONG_ROOT_LIST(V)                                                    \
  UNCONDITIONAL_STRONG_ROOT_LIST(V)                                            \
  V(Code, re_c_entry_code, RegExpCEntryCode)
#else
#define STRONG_ROOT_LIST(V) UNCONDITIONAL_STRONG_ROOT_LIST(V)
#endif

#define ROOT_LIST(V)                                  \
  STRONG_ROOT_LIST(V)                                 \
  V(SymbolTable, symbol_table, SymbolTable)

#define SYMBOL_LIST(V)                                                   \
  V(Array_symbol, "Array")                                               \
  V(Object_symbol, "Object")                                             \
  V(Proto_symbol, "__proto__")                                           \
  V(StringImpl_symbol, "StringImpl")                                     \
  V(arguments_symbol, "arguments")                                       \
  V(Arguments_symbol, "Arguments")                                       \
  V(arguments_shadow_symbol, ".arguments")                               \
  V(call_symbol, "call")                                                 \
  V(apply_symbol, "apply")                                               \
  V(caller_symbol, "caller")                                             \
  V(boolean_symbol, "boolean")                                           \
  V(Boolean_symbol, "Boolean")                                           \
  V(callee_symbol, "callee")                                             \
  V(constructor_symbol, "constructor")                                   \
  V(code_symbol, ".code")                                                \
  V(result_symbol, ".result")                                            \
  V(catch_var_symbol, ".catch-var")                                      \
  V(empty_symbol, "")                                                    \
  V(eval_symbol, "eval")                                                 \
  V(function_symbol, "function")                                         \
  V(length_symbol, "length")                                             \
  V(name_symbol, "name")                                                 \
  V(number_symbol, "number")                                             \
  V(Number_symbol, "Number")                                             \
  V(RegExp_symbol, "RegExp")                                             \
  V(source_symbol, "source")                                             \
  V(global_symbol, "global")                                             \
  V(ignore_case_symbol, "ignoreCase")                                    \
  V(multiline_symbol, "multiline")                                       \
  V(input_symbol, "input")                                               \
  V(index_symbol, "index")                                               \
  V(last_index_symbol, "lastIndex")                                      \
  V(object_symbol, "object")                                             \
  V(prototype_symbol, "prototype")                                       \
  V(string_symbol, "string")                                             \
  V(String_symbol, "String")                                             \
  V(Date_symbol, "Date")                                                 \
  V(this_symbol, "this")                                                 \
  V(to_string_symbol, "toString")                                        \
  V(char_at_symbol, "CharAt")                                            \
  V(undefined_symbol, "undefined")                                       \
  V(value_of_symbol, "valueOf")                                          \
  V(InitializeVarGlobal_symbol, "InitializeVarGlobal")                   \
  V(InitializeConstGlobal_symbol, "InitializeConstGlobal")               \
  V(stack_overflow_symbol, "kStackOverflowBoilerplate")                  \
  V(illegal_access_symbol, "illegal access")                             \
  V(out_of_memory_symbol, "out-of-memory")                               \
  V(illegal_execution_state_symbol, "illegal execution state")           \
  V(get_symbol, "get")                                                   \
  V(set_symbol, "set")                                                   \
  V(function_class_symbol, "Function")                                   \
  V(illegal_argument_symbol, "illegal argument")                         \
  V(MakeReferenceError_symbol, "MakeReferenceError")                     \
  V(MakeSyntaxError_symbol, "MakeSyntaxError")                           \
  V(MakeTypeError_symbol, "MakeTypeError")                               \
  V(invalid_lhs_in_assignment_symbol, "invalid_lhs_in_assignment")       \
  V(invalid_lhs_in_for_in_symbol, "invalid_lhs_in_for_in")               \
  V(invalid_lhs_in_postfix_op_symbol, "invalid_lhs_in_postfix_op")       \
  V(invalid_lhs_in_prefix_op_symbol, "invalid_lhs_in_prefix_op")         \
  V(illegal_return_symbol, "illegal_return")                             \
  V(illegal_break_symbol, "illegal_break")                               \
  V(illegal_continue_symbol, "illegal_continue")                         \
  V(unknown_label_symbol, "unknown_label")                               \
  V(redeclaration_symbol, "redeclaration")                               \
  V(failure_symbol, "<failure>")                                         \
  V(space_symbol, " ")                                                   \
  V(exec_symbol, "exec")                                                 \
  V(zero_symbol, "0")                                                    \
  V(global_eval_symbol, "GlobalEval")                                    \
  V(identity_hash_symbol, "v8::IdentityHash")                            \
  V(closure_symbol, "(closure)")


// Forward declaration of the GCTracer class.
class GCTracer;
class HeapStats;
class Isolate;


typedef String* (*ExternalStringTableUpdaterCallback)(Object** pointer);

typedef bool (*DirtyRegionCallback)(Address start,
                                    Address end,
                                    ObjectSlotCallback copy_object_func);


// The all static Heap captures the interface to the global object heap.
// All JavaScript contexts by this process share the same object heap.

class Heap {
 public:
  // Configure heap size before setup. Return false if the heap has been
  // setup already.
  bool ConfigureHeap(int max_semispace_size, int max_old_gen_size);
  bool ConfigureHeapDefault();

  // Initializes the global object heap. If create_heap_objects is true,
  // also creates the basic non-mutable objects.
  // Returns whether it succeeded.
  bool Setup(bool create_heap_objects);

  // Destroys all memory allocated by the heap.
  void TearDown();

  // Set the stack limit in the roots_ array.  Some architectures generate
  // code that looks here, because it is faster than loading from the static
  // jslimit_/real_jslimit_ variable in the StackGuard.
  void SetStackLimits();

  // Returns whether Setup has been called.
  bool HasBeenSetup();

  // Returns the maximum amount of memory reserved for the heap.  For
  // the young generation, we reserve 4 times the amount needed for a
  // semi space.  The young generation consists of two semi spaces and
  // we reserve twice the amount needed for those in order to ensure
  // that new space can be aligned to its size.
  int MaxReserved() {
    return 4 * reserved_semispace_size_ + max_old_generation_size_;
  }
  int MaxSemiSpaceSize() { return max_semispace_size_; }
  int ReservedSemiSpaceSize() { return reserved_semispace_size_; }
  int InitialSemiSpaceSize() { return initial_semispace_size_; }
  int MaxOldGenerationSize() { return max_old_generation_size_; }

  // Returns the capacity of the heap in bytes w/o growing. Heap grows when
  // more spaces are needed until it reaches the limit.
  int Capacity();

  // Returns the amount of memory currently committed for the heap.
  int CommittedMemory();

  // Returns the available bytes in space w/o growing.
  // Heap doesn't guarantee that it can allocate an object that requires
  // all available bytes. Check MaxHeapObjectSize() instead.
  int Available();

  // Returns the maximum object size in paged space.
  inline int MaxObjectSizeInPagedSpace();

  // Returns of size of all objects residing in the heap.
  int SizeOfObjects();

  // Return the starting address and a mask for the new space.  And-masking an
  // address with the mask will result in the start address of the new space
  // for all addresses in either semispace.
  Address NewSpaceStart() { return new_space_.start(); }
  uintptr_t NewSpaceMask() { return new_space_.mask(); }
  Address NewSpaceTop() { return new_space_.top(); }

  NewSpace* new_space() { return &new_space_; }
  OldSpace* old_pointer_space() { return old_pointer_space_; }
  OldSpace* old_data_space() { return old_data_space_; }
  OldSpace* code_space() { return code_space_; }
  MapSpace* map_space() { return map_space_; }
  CellSpace* cell_space() { return cell_space_; }
  LargeObjectSpace* lo_space() { return lo_space_; }

  bool always_allocate() { return always_allocate_scope_depth_ != 0; }
  Address always_allocate_scope_depth_address() {
    return reinterpret_cast<Address>(&always_allocate_scope_depth_);
  }
  bool linear_allocation() {
    return linear_allocation_scope_depth_ != 0;
  }

  Address* NewSpaceAllocationTopAddress() {
    return new_space_.allocation_top_address();
  }
  Address* NewSpaceAllocationLimitAddress() {
    return new_space_.allocation_limit_address();
  }

  // Uncommit unused semi space.
  bool UncommitFromSpace() { return new_space_.UncommitFromSpace(); }

#ifdef ENABLE_HEAP_PROTECTION
  // Protect/unprotect the heap by marking all spaces read-only/writable.
  void Protect();
  void Unprotect();
#endif

  // Allocates and initializes a new JavaScript object based on a
  // constructor.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateJSObject(JSFunction* constructor,
                           PretenureFlag pretenure = NOT_TENURED);

  // Allocates and initializes a new global object based on a constructor.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateGlobalObject(JSFunction* constructor);

  // Returns a deep copy of the JavaScript object.
  // Properties and elements are copied too.
  // Returns failure if allocation failed.
  Object* CopyJSObject(JSObject* source);

  // Allocates the function prototype.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateFunctionPrototype(JSFunction* function);

  // Reinitialize an JSGlobalProxy based on a constructor.  The object
  // must have the same size as objects allocated using the
  // constructor.  The object is reinitialized and behaves as an
  // object that has been freshly allocated using the constructor.
  Object* ReinitializeJSGlobalProxy(JSFunction* constructor,
                                    JSGlobalProxy* global);

  // Allocates and initializes a new JavaScript object based on a map.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateJSObjectFromMap(Map* map,
                                  PretenureFlag pretenure = NOT_TENURED);

  // Allocates a heap object based on the map.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  Object* Allocate(Map* map, AllocationSpace space);

  // Allocates a JS Map in the heap.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  Object* AllocateMap(InstanceType instance_type, int instance_size);

  // Allocates a partial map for bootstrapping.
  Object* AllocatePartialMap(InstanceType instance_type,
                             int instance_size);

  // Allocate a map for the specified function
  Object* AllocateInitialMap(JSFunction* fun);

  // Allocates an empty code cache.
  Object* AllocateCodeCache();

  // Clear the Instanceof cache (used when a prototype changes).
  inline void ClearInstanceofCache();

  // Allocates and fully initializes a String.  There are two String
  // encodings: ASCII and two byte. One should choose between the three string
  // allocation functions based on the encoding of the string buffer used to
  // initialized the string.
  //   - ...FromAscii initializes the string from a buffer that is ASCII
  //     encoded (it does not check that the buffer is ASCII encoded) and the
  //     result will be ASCII encoded.
  //   - ...FromUTF8 initializes the string from a buffer that is UTF-8
  //     encoded.  If the characters are all single-byte characters, the
  //     result will be ASCII encoded, otherwise it will converted to two
  //     byte.
  //   - ...FromTwoByte initializes the string from a buffer that is two-byte
  //     encoded.  If the characters are all single-byte characters, the
  //     result will be converted to ASCII, otherwise it will be left as
  //     two-byte.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateStringFromAscii(
      Vector<const char> str,
      PretenureFlag pretenure = NOT_TENURED);
  Object* AllocateStringFromUtf8(
      Vector<const char> str,
      PretenureFlag pretenure = NOT_TENURED);
  Object* AllocateStringFromTwoByte(
      Vector<const uc16> str,
      PretenureFlag pretenure = NOT_TENURED);

  // Allocates a symbol in old space based on the character stream.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  inline Object* AllocateSymbol(Vector<const char> str,
                                int chars,
                                uint32_t hash_field);

  Object* AllocateInternalSymbol(unibrow::CharacterStream* buffer,
                                 int chars,
                                 uint32_t hash_field);

  Object* AllocateExternalSymbol(Vector<const char> str,
                                 int chars);


  // Allocates and partially initializes a String.  There are two String
  // encodings: ASCII and two byte.  These functions allocate a string of the
  // given length and set its map and length fields.  The characters of the
  // string are uninitialized.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateRawAsciiString(
      int length,
      PretenureFlag pretenure = NOT_TENURED);
  Object* AllocateRawTwoByteString(
      int length,
      PretenureFlag pretenure = NOT_TENURED);

  // Computes a single character string where the character has code.
  // A cache is used for ascii codes.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed. Please note this does not perform a garbage collection.
  Object* LookupSingleCharacterStringFromCode(uint16_t code);

  // Allocate a byte array of the specified length
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateByteArray(int length, PretenureFlag pretenure);

  // Allocate a non-tenured byte array of the specified length
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateByteArray(int length);

  // Allocate a pixel array of the specified length
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocatePixelArray(int length,
                             uint8_t* external_pointer,
                             PretenureFlag pretenure);

  // Allocates an external array of the specified length and type.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateExternalArray(int length,
                                ExternalArrayType array_type,
                                void* external_pointer,
                                PretenureFlag pretenure);

  // Allocate a tenured JS global property cell.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateJSGlobalPropertyCell(Object* value);

  // Allocates a fixed array initialized with undefined values
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateFixedArray(int length, PretenureFlag pretenure);
  // Allocates a fixed array initialized with undefined values
  Object* AllocateFixedArray(int length);

  // Allocates an uninitialized fixed array. It must be filled by the caller.
  //
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateUninitializedFixedArray(int length);

  // Make a copy of src and return it. Returns
  // Failure::RetryAfterGC(requested_bytes, space) if the allocation failed.
  Object* CopyFixedArray(FixedArray* src);

  // Allocates a fixed array initialized with the hole values.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateFixedArrayWithHoles(
      int length,
      PretenureFlag pretenure = NOT_TENURED);

  // AllocateHashTable is identical to AllocateFixedArray except
  // that the resulting object has hash_table_map as map.
  Object* AllocateHashTable(int length,
                            PretenureFlag pretenure = NOT_TENURED);

  // Allocate a global (but otherwise uninitialized) context.
  Object* AllocateGlobalContext();

  // Allocate a function context.
  Object* AllocateFunctionContext(int length, JSFunction* closure);

  // Allocate a 'with' context.
  Object* AllocateWithContext(Context* previous,
                              JSObject* extension,
                              bool is_catch_context);

  // Allocates a new utility object in the old generation.
  Object* AllocateStruct(InstanceType type);

  // Allocates a function initialized with a shared part.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateFunction(Map* function_map,
                           SharedFunctionInfo* shared,
                           Object* prototype,
                           PretenureFlag pretenure = TENURED);

  // Indicies for direct access into argument objects.
  static const int kArgumentsObjectSize =
      JSObject::kHeaderSize + 2 * kPointerSize;
  static const int arguments_callee_index = 0;
  static const int arguments_length_index = 1;

  // Allocates an arguments object - optionally with an elements array.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateArgumentsObject(Object* callee, int length);

  // Same as NewNumberFromDouble, but may return a preallocated/immutable
  // number object (e.g., minus_zero_value_, nan_value_)
  Object* NumberFromDouble(double value,
                           PretenureFlag pretenure = NOT_TENURED);

  // Allocated a HeapNumber from value.
  Object* AllocateHeapNumber(double value, PretenureFlag pretenure);
  Object* AllocateHeapNumber(double value);  // pretenure = NOT_TENURED

  // Converts an int into either a Smi or a HeapNumber object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  inline Object* NumberFromInt32(int32_t value);

  // Converts an int into either a Smi or a HeapNumber object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  inline Object* NumberFromUint32(uint32_t value);

  // Allocates a new proxy object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateProxy(Address proxy,
                        PretenureFlag pretenure = NOT_TENURED);

  // Allocates a new SharedFunctionInfo object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateSharedFunctionInfo(Object* name);

  // Allocates a new cons string object.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateConsString(String* first, String* second);

  // Allocates a new sub string object which is a substring of an underlying
  // string buffer stretching from the index start (inclusive) to the index
  // end (exclusive).
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateSubString(String* buffer,
                            int start,
                            int end,
                            PretenureFlag pretenure = NOT_TENURED);

  // Allocate a new external string object, which is backed by a string
  // resource that resides outside the V8 heap.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this does not perform a garbage collection.
  Object* AllocateExternalStringFromAscii(
      ExternalAsciiString::Resource* resource);
  Object* AllocateExternalStringFromTwoByte(
      ExternalTwoByteString::Resource* resource);

  // Finalizes an external string by deleting the associated external
  // data and clearing the resource pointer.
  inline void FinalizeExternalString(String* string);

  // Allocates an uninitialized object.  The memory is non-executable if the
  // hardware and OS allow.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  inline Object* AllocateRaw(int size_in_bytes,
                             AllocationSpace space,
                             AllocationSpace retry_space);

  // Initialize a filler object to keep the ability to iterate over the heap
  // when shortening objects.
  void CreateFillerObjectAt(Address addr, int size);

  // Makes a new native code object
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed. On success, the pointer to the Code object is stored in the
  // self_reference. This allows generated code to reference its own Code
  // object by containing this pointer.
  // Please note this function does not perform a garbage collection.
  Object* CreateCode(const CodeDesc& desc,
                     ZoneScopeInfo* sinfo,
                     Code::Flags flags,
                     Handle<Object> self_reference);

  Object* CopyCode(Code* code);

  // Copy the code and scope info part of the code object, but insert
  // the provided data as the relocation information.
  Object* CopyCode(Code* code, Vector<byte> reloc_info);

  // Finds the symbol for string in the symbol table.
  // If not found, a new symbol is added to the table and returned.
  // Returns Failure::RetryAfterGC(requested_bytes, space) if allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  Object* LookupSymbol(Vector<const char> str);
  Object* LookupAsciiSymbol(const char* str) {
    return LookupSymbol(CStrVector(str));
  }
  Object* LookupSymbol(String* str);
  bool LookupSymbolIfExists(String* str, String** symbol);
  bool LookupTwoCharsSymbolIfExists(String* str, String** symbol);

  // Compute the matching symbol map for a string if possible.
  // NULL is returned if string is in new space or not flattened.
  Map* SymbolMapForString(String* str);

  // Tries to flatten a string before compare operation.
  //
  // Returns a failure in case it was decided that flattening was
  // necessary and failed.  Note, if flattening is not necessary the
  // string might stay non-flat even when not a failure is returned.
  //
  // Please note this function does not perform a garbage collection.
  inline Object* PrepareForCompare(String* str);

  // Converts the given boolean condition to JavaScript boolean value.
  inline Object* ToBoolean(bool condition);

  // Code that should be run before and after each GC.  Includes some
  // reporting/verification activities when compiled with DEBUG set.
  void GarbageCollectionPrologue();
  void GarbageCollectionEpilogue();

  // Performs garbage collection operation.
  // Returns whether required_space bytes are available after the collection.
  bool CollectGarbage(int required_space, AllocationSpace space);

  // Performs a full garbage collection. Force compaction if the
  // parameter is true.
  void CollectAllGarbage(bool force_compaction);

  // Notify the heap that a context has been disposed.
  int NotifyContextDisposed() { return ++contexts_disposed_; }

  // Utility to invoke the scavenger. This is needed in test code to
  // ensure correct callback for weak global handles.
  void PerformScavenge();

#ifdef DEBUG
  // Utility used with flag gc-greedy.
  bool GarbageCollectionGreedyCheck();
#endif

  void AddGCPrologueCallback(
      GCEpilogueCallback callback, GCType gc_type_filter);
  void RemoveGCPrologueCallback(GCEpilogueCallback callback);

  void AddGCEpilogueCallback(
      GCEpilogueCallback callback, GCType gc_type_filter);
  void RemoveGCEpilogueCallback(GCEpilogueCallback callback);

  void SetGlobalGCPrologueCallback(GCCallback callback) {
    ASSERT((callback == NULL) ^ (global_gc_prologue_callback_ == NULL));
    global_gc_prologue_callback_ = callback;
  }
  void SetGlobalGCEpilogueCallback(GCCallback callback) {
    ASSERT((callback == NULL) ^ (global_gc_epilogue_callback_ == NULL));
    global_gc_epilogue_callback_ = callback;
  }

  // Heap root getters.  We have versions with and without type::cast() here.
  // You can't use type::cast during GC because the assert fails.
#define ROOT_ACCESSOR(type, name, camel_name)                                  \
  type* name() {                                                               \
    return type::cast(roots_[k##camel_name##RootIndex]);                       \
  }                                                                            \
  type* raw_unchecked_##name() {                                               \
    return reinterpret_cast<type*>(roots_[k##camel_name##RootIndex]);          \
  }
  ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

// Utility type maps
#define STRUCT_MAP_ACCESSOR(NAME, Name, name)                                  \
    Map* name##_map() {                                                        \
      return Map::cast(roots_[k##Name##MapRootIndex]);                         \
    }
  STRUCT_LIST(STRUCT_MAP_ACCESSOR)
#undef STRUCT_MAP_ACCESSOR

#define SYMBOL_ACCESSOR(name, str) String* name() {                            \
    return String::cast(roots_[k##name##RootIndex]);                           \
  }
  SYMBOL_LIST(SYMBOL_ACCESSOR)
#undef SYMBOL_ACCESSOR

  // The hidden_symbol is special because it is the empty string, but does
  // not match the empty string.
  String* hidden_symbol() { return hidden_symbol_; }

  // Iterates over all roots in the heap.
  void IterateRoots(ObjectVisitor* v, VisitMode mode);
  // Iterates over all strong roots in the heap.
  void IterateStrongRoots(ObjectVisitor* v, VisitMode mode);
  // Iterates over all the other roots in the heap.
  void IterateWeakRoots(ObjectVisitor* v, VisitMode mode);

  enum ExpectedPageWatermarkState {
    WATERMARK_SHOULD_BE_VALID,
    WATERMARK_CAN_BE_INVALID
  };

  // For each dirty region on a page in use from an old space call
  // visit_dirty_region callback.
  // If either visit_dirty_region or callback can cause an allocation
  // in old space and changes in allocation watermark then
  // can_preallocate_during_iteration should be set to true.
  // All pages will be marked as having invalid watermark upon
  // iteration completion.
  void IterateDirtyRegions(
      PagedSpace* space,
      DirtyRegionCallback visit_dirty_region,
      ObjectSlotCallback callback,
      ExpectedPageWatermarkState expected_page_watermark_state);

  // Interpret marks as a bitvector of dirty marks for regions of size
  // Page::kRegionSize aligned by Page::kRegionAlignmentMask and covering
  // memory interval from start to top. For each dirty region call a
  // visit_dirty_region callback. Return updated bitvector of dirty marks.
  uint32_t IterateDirtyRegions(uint32_t marks,
                               Address start,
                               Address end,
                               DirtyRegionCallback visit_dirty_region,
                               ObjectSlotCallback callback);

  // Iterate pointers to new space found in memory interval from start to end.
  // Update dirty marks for page containing start address.
  void IterateAndMarkPointersToNewSpace(Address start,
                                        Address end,
                                        ObjectSlotCallback callback);

  // Iterate pointers to new space found in memory interval from start to end.
  // Return true if pointers to new space was found.
  static bool IteratePointersInDirtyRegion(Address start,
                                           Address end,
                                           ObjectSlotCallback callback);


  // Iterate pointers to new space found in memory interval from start to end.
  // This interval is considered to belong to the map space.
  // Return true if pointers to new space was found.
  static bool IteratePointersInDirtyMapsRegion(Address start,
                                               Address end,
                                               ObjectSlotCallback callback);


  // Returns whether the object resides in new space.
  inline bool InNewSpace(Object* object);
  inline bool InFromSpace(Object* object);
  inline bool InToSpace(Object* object);

  // Checks whether an address/object in the heap (including auxiliary
  // area and unused area).
  bool Contains(Address addr);
  bool Contains(HeapObject* value);

  // Checks whether an address/object in a space.
  // Currently used by tests, serialization and heap verification only.
  bool InSpace(Address addr, AllocationSpace space);
  bool InSpace(HeapObject* value, AllocationSpace space);

  // Finds out which space an object should get promoted to based on its type.
  inline OldSpace* TargetSpace(HeapObject* object);
  inline AllocationSpace TargetSpaceId(InstanceType type);

  // Sets the stub_cache_ (only used when expanding the dictionary).
  void public_set_code_stubs(NumberDictionary* value) {
    roots_[kCodeStubsRootIndex] = value;
  }

  // Sets the non_monomorphic_cache_ (only used when expanding the dictionary).
  void public_set_non_monomorphic_cache(NumberDictionary* value) {
    roots_[kNonMonomorphicCacheRootIndex] = value;
  }

  void public_set_empty_script(Script* script) {
    roots_[kEmptyScriptRootIndex] = script;
  }

  // Update the next script id.
  inline void SetLastScriptId(Object* last_script_id);

  // Generated code can embed this address to get access to the roots.
  Object** roots_address() { return roots_; }

#ifdef DEBUG
  void Print();
  void PrintHandles();

  // Verify the heap is in its normal state before or after a GC.
  void Verify();

  // Report heap statistics.
  void ReportHeapStatistics(const char* title);
  void ReportCodeStatistics(const char* title);

  // Fill in bogus values in from space
  void ZapFromSpace();
#endif

#if defined(ENABLE_LOGGING_AND_PROFILING)
  // Print short heap statistics.
  void PrintShortHeapStatistics();
#endif

  // Makes a new symbol object
  // Returns Failure::RetryAfterGC(requested_bytes, space) if the allocation
  // failed.
  // Please note this function does not perform a garbage collection.
  Object* CreateSymbol(const char* str, int length, int hash);
  Object* CreateSymbol(String* str);

  // Write barrier support for address[offset] = o.
  inline void RecordWrite(Address address, int offset);

  // Write barrier support for address[start : start + len[ = o.
  inline void RecordWrites(Address address, int start, int len);

  // Given an address occupied by a live code object, return that object.
  Object* FindCodeObject(Address a);

  // Invoke Shrink on shrinkable spaces.
  void Shrink();

  enum HeapState { NOT_IN_GC, SCAVENGE, MARK_COMPACT };
  inline HeapState gc_state() { return gc_state_; }

#ifdef DEBUG
  bool IsAllocationAllowed() { return allocation_allowed_; }
  inline bool allow_allocation(bool enable);

  bool disallow_allocation_failure() {
    return disallow_allocation_failure_;
  }

  void TracePathToObject(Object* target);
  void TracePathToGlobal();
#endif

  // Callback function passed to Heap::Iterate etc.  Copies an object if
  // necessary, the object might be promoted to an old space.  The caller must
  // ensure the precondition that the object is (a) a heap object and (b) in
  // the heap's from space.
  static void ScavengePointer(HeapObject** p);
  inline void ScavengeObject(HeapObject** p, HeapObject* object);

  // Commits from space if it is uncommitted.
  void EnsureFromSpaceIsCommitted();

  // Support for partial snapshots.  After calling this we can allocate a
  // certain number of bytes using only linear allocation (with a
  // LinearAllocationScope and an AlwaysAllocateScope) without using freelists
  // or causing a GC.  It returns true of space was reserved or false if a GC is
  // needed.  For paged spaces the space requested must include the space wasted
  // at the end of each page when allocating linearly.
  void ReserveSpace(
    int new_space_size,
    int pointer_space_size,
    int data_space_size,
    int code_space_size,
    int map_space_size,
    int cell_space_size,
    int large_object_size);

  //
  // Support for the API.
  //

  bool CreateApiObjects();

  // Attempt to find the number in a small cache.  If we finds it, return
  // the string representation of the number.  Otherwise return undefined.
  Object* GetNumberStringCache(Object* number);

  // Update the cache with a new number-string pair.
  void SetNumberStringCache(Object* number, String* str);

  // Adjusts the amount of registered external memory.
  // Returns the adjusted value.
  inline int AdjustAmountOfExternalAllocatedMemory(int change_in_bytes);

  // Allocate uninitialized fixed array.
  Object* AllocateRawFixedArray(int length);
  Object* AllocateRawFixedArray(int length,
                                PretenureFlag pretenure);

  // True if we have reached the allocation limit in the old generation that
  // should force the next GC (caused normally) to be a full one.
  bool OldGenerationPromotionLimitReached() {
    return (PromotedSpaceSize() + PromotedExternalMemorySize())
           > old_gen_promotion_limit_;
  }

  intptr_t OldGenerationSpaceAvailable() {
    return old_gen_allocation_limit_ -
           (PromotedSpaceSize() + PromotedExternalMemorySize());
  }

  // True if we have reached the allocation limit in the old generation that
  // should artificially cause a GC right now.
  bool OldGenerationAllocationLimitReached() {
    return OldGenerationSpaceAvailable() < 0;
  }

  // Can be called when the embedding application is idle.
  bool IdleNotification();

  // Declare all the root indices.
  enum RootListIndex {
#define ROOT_INDEX_DECLARATION(type, name, camel_name) k##camel_name##RootIndex,
    STRONG_ROOT_LIST(ROOT_INDEX_DECLARATION)
#undef ROOT_INDEX_DECLARATION

// Utility type maps
#define DECLARE_STRUCT_MAP(NAME, Name, name) k##Name##MapRootIndex,
  STRUCT_LIST(DECLARE_STRUCT_MAP)
#undef DECLARE_STRUCT_MAP

#define SYMBOL_INDEX_DECLARATION(name, str) k##name##RootIndex,
    SYMBOL_LIST(SYMBOL_INDEX_DECLARATION)
#undef SYMBOL_DECLARATION

    kSymbolTableRootIndex,
    kStrongRootListLength = kSymbolTableRootIndex,
    kRootListLength
  };

  Object* NumberToString(Object* number,
                         bool check_number_string_cache = true);

  Map* MapForExternalArrayType(ExternalArrayType array_type);
  RootListIndex RootIndexForExternalArrayType(
      ExternalArrayType array_type);

  void RecordStats(HeapStats* stats);

  // Copy block of memory from src to dst. Size of block should be aligned
  // by pointer size.
  static inline void CopyBlock(Address dst, Address src, int byte_size);

  inline void CopyBlockToOldSpaceAndUpdateRegionMarks(Address dst,
                                                      Address src,
                                                      int byte_size);

  // Optimized version of memmove for blocks with pointer size aligned sizes and
  // pointer size aligned addresses.
  static inline void MoveBlock(Address dst, Address src, int byte_size);

  inline void MoveBlockToOldSpaceAndUpdateRegionMarks(Address dst,
                                                      Address src,
                                                      int byte_size);

  // Check new space expansion criteria and expand semispaces if it was hit.
  void CheckNewSpaceExpansionCriteria();

  inline void IncrementYoungSurvivorsCounter(int survived) {
    survived_since_last_expansion_ += survived;
  }

  void UpdateNewSpaceReferencesInExternalStringTable(
      ExternalStringTableUpdaterCallback updater_func);

  // Helper function that governs the promotion policy from new space to
  // old.  If the object's old address lies below the new space's age
  // mark or if we've already filled the bottom 1/16th of the to space,
  // we try to promote this object.
  inline bool ShouldBePromoted(Address old_address, int object_size);

  int MaxObjectSizeInNewSpace() { return kMaxObjectSizeInNewSpace; }

  void ClearJSFunctionResultCaches();

  GCTracer* tracer() { return tracer_; }

 private:
  Heap();

  // TODO(isolates): Technically this can be calculated directly from
  //                 any pointer to Heap.
  Isolate* isolate_;

  static int reserved_semispace_size_;
  static int max_semispace_size_;
  static int initial_semispace_size_;
  static int max_old_generation_size_;
  static size_t code_range_size_;

  // For keeping track of how much data has survived
  // scavenge since last new space expansion.
  static int survived_since_last_expansion_;

  static int always_allocate_scope_depth_;
  static int linear_allocation_scope_depth_;

  // For keeping track of context disposals.
  static int contexts_disposed_;

#if defined(V8_TARGET_ARCH_X64)
  static const int kMaxObjectSizeInNewSpace = 512*KB;
#else
  static const int kMaxObjectSizeInNewSpace = 256*KB;
#endif

  static NewSpace new_space_;
  static OldSpace* old_pointer_space_;
  static OldSpace* old_data_space_;
  static OldSpace* code_space_;
  static MapSpace* map_space_;
  static CellSpace* cell_space_;
  static LargeObjectSpace* lo_space_;
  static HeapState gc_state_;

  // Returns the size of object residing in non new spaces.
  int PromotedSpaceSize();

  // Returns the amount of external memory registered since last global gc.
  int PromotedExternalMemorySize();

  static int mc_count_;  // how many mark-compact collections happened
  static int ms_count_;  // how many mark-sweep collections happened
  static int gc_count_;  // how many gc happened

  // Total length of the strings we failed to flatten since the last GC.
  static int unflattened_strings_length_;

#define ROOT_ACCESSOR(type, name, camel_name)                                  \
  static inline void set_##name(type* value) {                                 \
    roots_[k##camel_name##RootIndex] = value;                                  \
  }
  ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

#ifdef DEBUG
  static bool allocation_allowed_;

  // If the --gc-interval flag is set to a positive value, this
  // variable holds the value indicating the number of allocations
  // remain until the next failure and garbage collection.
  static int allocation_timeout_;

  // Do we expect to be able to handle allocation failure at this
  // time?
  static bool disallow_allocation_failure_;
#endif  // DEBUG

  // Limit that triggers a global GC on the next (normally caused) GC.  This
  // is checked when we have already decided to do a GC to help determine
  // which collector to invoke.
  static int old_gen_promotion_limit_;

  // Limit that triggers a global GC as soon as is reasonable.  This is
  // checked before expanding a paged space in the old generation and on
  // every allocation in large object space.
  static int old_gen_allocation_limit_;

  // Limit on the amount of externally allocated memory allowed
  // between global GCs. If reached a global GC is forced.
  static int external_allocation_limit_;

  // The amount of external memory registered through the API kept alive
  // by global handles
  static int amount_of_external_allocated_memory_;

  // Caches the amount of external memory registered at the last global gc.
  static int amount_of_external_allocated_memory_at_last_global_gc_;

  // Indicates that an allocation has failed in the old generation since the
  // last GC.
  static int old_gen_exhausted_;

  static Object* roots_[kRootListLength];

  struct StringTypeTable {
    InstanceType type;
    int size;
    RootListIndex index;
  };

  struct ConstantSymbolTable {
    const char* contents;
    RootListIndex index;
  };

  struct StructTable {
    InstanceType type;
    int size;
    RootListIndex index;
  };

  static const StringTypeTable string_type_table[];
  static const ConstantSymbolTable constant_symbol_table[];
  static const StructTable struct_table[];

  // The special hidden symbol which is an empty string, but does not match
  // any string when looked up in properties.
  static String* hidden_symbol_;

  // GC callback function, called before and after mark-compact GC.
  // Allocations in the callback function are disallowed.
  struct GCPrologueCallbackPair {
    GCPrologueCallbackPair(GCPrologueCallback callback, GCType gc_type)
        : callback(callback), gc_type(gc_type) {
    }
    bool operator==(const GCPrologueCallbackPair& pair) const {
      return pair.callback == callback;
    }
    GCPrologueCallback callback;
    GCType gc_type;
  };
  static List<GCPrologueCallbackPair> gc_prologue_callbacks_;

  struct GCEpilogueCallbackPair {
    GCEpilogueCallbackPair(GCEpilogueCallback callback, GCType gc_type)
        : callback(callback), gc_type(gc_type) {
    }
    bool operator==(const GCEpilogueCallbackPair& pair) const {
      return pair.callback == callback;
    }
    GCEpilogueCallback callback;
    GCType gc_type;
  };
  static List<GCEpilogueCallbackPair> gc_epilogue_callbacks_;

  static GCCallback global_gc_prologue_callback_;
  static GCCallback global_gc_epilogue_callback_;

  // Checks whether a global GC is necessary
  GarbageCollector SelectGarbageCollector(AllocationSpace space);

  // Performs garbage collection
  void PerformGarbageCollection(AllocationSpace space,
                                GarbageCollector collector,
                                GCTracer* tracer);

  // Allocate an uninitialized object in map space.  The behavior is identical
  // to Heap::AllocateRaw(size_in_bytes, MAP_SPACE), except that (a) it doesn't
  // have to test the allocation space argument and (b) can reduce code size
  // (since both AllocateRaw and AllocateRawMap are inlined).
  inline Object* AllocateRawMap();

  // Allocate an uninitialized object in the global property cell space.
  inline Object* AllocateRawCell();

  // Initializes a JSObject based on its map.
  void InitializeJSObjectFromMap(JSObject* obj,
                                 FixedArray* properties,
                                 Map* map);

  bool CreateInitialMaps();
  bool CreateInitialObjects();

  // These four Create*EntryStub functions are here because of a gcc-4.4 bug
  // that assigns wrong vtable entries.
  void CreateCEntryStub();
  void CreateJSEntryStub();
  void CreateJSConstructEntryStub();
  void CreateRegExpCEntryStub();

  void CreateFixedStubs();

  Object* CreateOddball(const char* to_string, Object* to_number);

  // Allocate empty fixed array.
  Object* AllocateEmptyFixedArray();

  // Performs a minor collection in new generation.
  void Scavenge();

  static String* UpdateNewSpaceReferenceInExternalStringTableEntry(
      Object** pointer);

  Address DoScavenge(ObjectVisitor* scavenge_visitor, Address new_space_front);

  // Performs a major collection in the whole heap.
  void MarkCompact(GCTracer* tracer);

  // Code to be run before and after mark-compact.
  void MarkCompactPrologue(bool is_compacting);
  void MarkCompactEpilogue(bool is_compacting);

  // Completely clear the Instanceof cache (to stop it keeping objects alive
  // around a GC).
  inline void CompletelyClearInstanceofCache();

  // Helper function used by CopyObject to copy a source object to an
  // allocated target object and update the forwarding pointer in the source
  // object.  Returns the target object.
  inline HeapObject* MigrateObject(HeapObject* source,
                                   HeapObject* target,
                                   int size);

#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  // Record the copy of an object in the NewSpace's statistics.
  void RecordCopiedObject(HeapObject* obj);

  // Record statistics before and after garbage collection.
  void ReportStatisticsBeforeGC();
  void ReportStatisticsAfterGC();
#endif

  // Slow part of scavenge object.
  void ScavengeObjectSlow(HeapObject** p, HeapObject* object);

  // Initializes a function with a shared part and prototype.
  // Returns the function.
  // Note: this code was factored out of AllocateFunction such that
  // other parts of the VM could use it. Specifically, a function that creates
  // instances of type JS_FUNCTION_TYPE benefit from the use of this function.
  // Please note this does not perform a garbage collection.
  inline Object* InitializeFunction(JSFunction* function,
                                    SharedFunctionInfo* shared,
                                    Object* prototype);

  static GCTracer* tracer_;


  // Initializes the number to string cache based on the max semispace size.
  Object* InitializeNumberStringCache();
  // Flush the number to string cache.
  void FlushNumberStringCache();

  // Flush code from functions we do not expect to use again. The code will
  // be replaced with a lazy compilable version.
  void FlushCode();

  static const int kInitialSymbolTableSize = 2048;
  static const int kInitialEvalCacheSize = 64;

  friend class Factory;
  friend class DisallowAllocationFailure;
  friend class AlwaysAllocateScope;
  friend class LinearAllocationScope;
  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(Heap);
};


class HeapStats {
 public:
  int* start_marker;
  int* new_space_size;
  int* new_space_capacity;
  int* old_pointer_space_size;
  int* old_pointer_space_capacity;
  int* old_data_space_size;
  int* old_data_space_capacity;
  int* code_space_size;
  int* code_space_capacity;
  int* map_space_size;
  int* map_space_capacity;
  int* cell_space_size;
  int* cell_space_capacity;
  int* lo_space_size;
  int* global_handle_count;
  int* weak_global_handle_count;
  int* pending_global_handle_count;
  int* near_death_global_handle_count;
  int* destroyed_global_handle_count;
  int* end_marker;
};


class AlwaysAllocateScope {
 public:
  AlwaysAllocateScope() {
    // We shouldn't hit any nested scopes, because that requires
    // non-handle code to call handle code. The code still works but
    // performance will degrade, so we want to catch this situation
    // in debug mode.
    ASSERT(HEAP->always_allocate_scope_depth_ == 0);
    HEAP->always_allocate_scope_depth_++;
  }

  ~AlwaysAllocateScope() {
    HEAP->always_allocate_scope_depth_--;
    ASSERT(HEAP->always_allocate_scope_depth_ == 0);
  }
};


class LinearAllocationScope {
 public:
  LinearAllocationScope() {
    HEAP->linear_allocation_scope_depth_++;
  }

  ~LinearAllocationScope() {
    HEAP->linear_allocation_scope_depth_--;
    ASSERT(HEAP->linear_allocation_scope_depth_ >= 0);
  }
};


#ifdef DEBUG
// Visitor class to verify interior pointers in spaces that do not contain
// or care about intergenerational references. All heap object pointers have to
// point into the heap to a location that has a map pointer at its first word.
// Caveat: Heap::Contains is an approximation because it can return true for
// objects in a heap space but above the allocation pointer.
class VerifyPointersVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        ASSERT(HEAP->Contains(object));
        ASSERT(object->map()->IsMap());
      }
    }
  }
};


// Visitor class to verify interior pointers in spaces that use region marks
// to keep track of intergenerational references.
// As VerifyPointersVisitor but also checks that dirty marks are set
// for regions covering intergenerational references.
class VerifyPointersAndDirtyRegionsVisitor: public ObjectVisitor {
 public:
  void VisitPointers(Object** start, Object** end) {
    for (Object** current = start; current < end; current++) {
      if ((*current)->IsHeapObject()) {
        HeapObject* object = HeapObject::cast(*current);
        ASSERT(HEAP->Contains(object));
        ASSERT(object->map()->IsMap());
        if (HEAP->InNewSpace(object)) {
          ASSERT(HEAP->InToSpace(object));
          Address addr = reinterpret_cast<Address>(current);
          ASSERT(Page::FromAddress(addr)->IsRegionDirty(addr));
        }
      }
    }
  }
};
#endif


// Space iterator for iterating over all spaces of the heap.
// Returns each space in turn, and null when it is done.
class AllSpaces BASE_EMBEDDED {
 public:
  Space* next();
  AllSpaces() { counter_ = FIRST_SPACE; }
 private:
  int counter_;
};


// Space iterator for iterating over all old spaces of the heap: Old pointer
// space, old data space and code space.
// Returns each space in turn, and null when it is done.
class OldSpaces BASE_EMBEDDED {
 public:
  OldSpace* next();
  OldSpaces() { counter_ = OLD_POINTER_SPACE; }
 private:
  int counter_;
};


// Space iterator for iterating over all the paged spaces of the heap:
// Map space, old pointer space, old data space, code space and cell space.
// Returns each space in turn, and null when it is done.
class PagedSpaces BASE_EMBEDDED {
 public:
  PagedSpace* next();
  PagedSpaces() { counter_ = OLD_POINTER_SPACE; }
 private:
  int counter_;
};


// Space iterator for iterating over all spaces of the heap.
// For each space an object iterator is provided. The deallocation of the
// returned object iterators is handled by the space iterator.
class SpaceIterator : public Malloced {
 public:
  SpaceIterator();
  virtual ~SpaceIterator();

  bool has_next();
  ObjectIterator* next();

 private:
  ObjectIterator* CreateIterator();

  int current_space_;  // from enum AllocationSpace.
  ObjectIterator* iterator_;  // object iterator for the current space.
};


// A HeapIterator provides iteration over the whole heap It aggregates a the
// specific iterators for the different spaces as these can only iterate over
// one space only.

class HeapIterator BASE_EMBEDDED {
 public:
  explicit HeapIterator();
  virtual ~HeapIterator();

  HeapObject* next();
  void reset();

 private:
  // Perform the initialization.
  void Init();

  // Perform all necessary shutdown (destruction) work.
  void Shutdown();

  // Space iterator for iterating all the spaces.
  SpaceIterator* space_iterator_;
  // Object iterator for the space currently being iterated.
  ObjectIterator* object_iterator_;
};


// Cache for mapping (map, property name) into field offset.
// Cleared at startup and prior to mark sweep collection.
class KeyedLookupCache {
 public:
  // Lookup field offset for (map, name). If absent, -1 is returned.
  static int Lookup(Map* map, String* name);

  // Update an element in the cache.
  static void Update(Map* map, String* name, int field_offset);

  // Clear the cache.
  static void Clear();

  static const int kLength = 64;
  static const int kCapacityMask = kLength - 1;
  static const int kMapHashShift = 2;

 private:
  static inline int Hash(Map* map, String* name);

  // Get the address of the keys and field_offsets arrays.  Used in
  // generated code to perform cache lookups.
  static Address keys_address() {
    return reinterpret_cast<Address>(&keys_);
  }

  static Address field_offsets_address() {
    return reinterpret_cast<Address>(&field_offsets_);
  }

  struct Key {
    Map* map;
    String* name;
  };
  static Key keys_[kLength];
  static int field_offsets_[kLength];

  friend class ExternalReference;
};


// Cache for mapping (array, property name) into descriptor index.
// The cache contains both positive and negative results.
// Descriptor index equals kNotFound means the property is absent.
// Cleared at startup and prior to any gc.
class DescriptorLookupCache {
 public:
  // Lookup descriptor index for (map, name).
  // If absent, kAbsent is returned.
  static int Lookup(DescriptorArray* array, String* name) {
    if (!StringShape(name).IsSymbol()) return kAbsent;
    int index = Hash(array, name);
    Key& key = keys_[index];
    if ((key.array == array) && (key.name == name)) return results_[index];
    return kAbsent;
  }

  // Update an element in the cache.
  static void Update(DescriptorArray* array, String* name, int result) {
    ASSERT(result != kAbsent);
    if (StringShape(name).IsSymbol()) {
      int index = Hash(array, name);
      Key& key = keys_[index];
      key.array = array;
      key.name = name;
      results_[index] = result;
    }
  }

  // Clear the cache.
  static void Clear();

  static const int kAbsent = -2;
 private:
  static int Hash(DescriptorArray* array, String* name) {
    // Uses only lower 32 bits if pointers are larger.
    uint32_t array_hash =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(array)) >> 2;
    uint32_t name_hash =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)) >> 2;
    return (array_hash ^ name_hash) % kLength;
  }

  static const int kLength = 64;
  struct Key {
    DescriptorArray* array;
    String* name;
  };

  static Key keys_[kLength];
  static int results_[kLength];
};


// ----------------------------------------------------------------------------
// Marking stack for tracing live objects.

class MarkingStack {
 public:
  void Initialize(Address low, Address high) {
    top_ = low_ = reinterpret_cast<HeapObject**>(low);
    high_ = reinterpret_cast<HeapObject**>(high);
    overflowed_ = false;
  }

  bool is_full() { return top_ >= high_; }

  bool is_empty() { return top_ <= low_; }

  bool overflowed() { return overflowed_; }

  void clear_overflowed() { overflowed_ = false; }

  // Push the (marked) object on the marking stack if there is room,
  // otherwise mark the object as overflowed and wait for a rescan of the
  // heap.
  void Push(HeapObject* object) {
    CHECK(object->IsHeapObject());
    if (is_full()) {
      object->SetOverflow();
      overflowed_ = true;
    } else {
      *(top_++) = object;
    }
  }

  HeapObject* Pop() {
    ASSERT(!is_empty());
    HeapObject* object = *(--top_);
    CHECK(object->IsHeapObject());
    return object;
  }

 private:
  HeapObject** low_;
  HeapObject** top_;
  HeapObject** high_;
  bool overflowed_;
};


// A helper class to document/test C++ scopes where we do not
// expect a GC. Usage:
//
// /* Allocation not allowed: we cannot handle a GC in this scope. */
// { AssertNoAllocation nogc;
//   ...
// }

#ifdef DEBUG

class DisallowAllocationFailure {
 public:
  DisallowAllocationFailure() {
    old_state_ = HEAP->disallow_allocation_failure_;
    HEAP->disallow_allocation_failure_ = true;
  }
  ~DisallowAllocationFailure() {
    HEAP->disallow_allocation_failure_ = old_state_;
  }
 private:
  bool old_state_;
};

class AssertNoAllocation {
 public:
  AssertNoAllocation() {
    old_state_ = HEAP->allow_allocation(false);
  }

  ~AssertNoAllocation() {
    HEAP->allow_allocation(old_state_);
  }

 private:
  bool old_state_;
};

class DisableAssertNoAllocation {
 public:
  DisableAssertNoAllocation() {
    old_state_ = HEAP->allow_allocation(true);
  }

  ~DisableAssertNoAllocation() {
    HEAP->allow_allocation(old_state_);
  }

 private:
  bool old_state_;
};

#else  // ndef DEBUG

class AssertNoAllocation {
 public:
  AssertNoAllocation() { }
  ~AssertNoAllocation() { }
};

class DisableAssertNoAllocation {
 public:
  DisableAssertNoAllocation() { }
  ~DisableAssertNoAllocation() { }
};

#endif

// GCTracer collects and prints ONE line after each garbage collector
// invocation IFF --trace_gc is used.

class GCTracer BASE_EMBEDDED {
 public:
  class Scope BASE_EMBEDDED {
   public:
    enum ScopeId {
      EXTERNAL,
      MC_MARK,
      MC_SWEEP,
      MC_COMPACT,
      kNumberOfScopes
    };

    Scope(GCTracer* tracer, ScopeId scope)
        : tracer_(tracer),
        scope_(scope) {
      start_time_ = OS::TimeCurrentMillis();
    }

    ~Scope() {
      ASSERT((0 <= scope_) && (scope_ < kNumberOfScopes));
      tracer_->scopes_[scope_] += OS::TimeCurrentMillis() - start_time_;
    }

   private:
    GCTracer* tracer_;
    ScopeId scope_;
    double start_time_;
  };

  GCTracer();
  ~GCTracer();

  // Sets the collector.
  void set_collector(GarbageCollector collector) { collector_ = collector; }

  // Sets the GC count.
  void set_gc_count(int count) { gc_count_ = count; }

  // Sets the full GC count.
  void set_full_gc_count(int count) { full_gc_count_ = count; }

  // Sets the flag that this is a compacting full GC.
  void set_is_compacting() { is_compacting_ = true; }
  bool is_compacting() const { return is_compacting_; }

  // Increment and decrement the count of marked objects.
  void increment_marked_count() { ++marked_count_; }
  void decrement_marked_count() { --marked_count_; }

  int marked_count() { return marked_count_; }

  void increment_promoted_objects_size(int object_size) {
    promoted_objects_size_ += object_size;
  }

  // Returns maximum GC pause.
  static int get_max_gc_pause() { return max_gc_pause_; }

  // Returns maximum size of objects alive after GC.
  static int get_max_alive_after_gc() { return max_alive_after_gc_; }

  // Returns minimal interval between two subsequent collections.
  static int get_min_in_mutator() { return min_in_mutator_; }

 private:
  // Returns a string matching the collector.
  const char* CollectorString();

  // Returns size of object in heap (in MB).
  double SizeOfHeapObjects() {
    return (static_cast<double>(HEAP->SizeOfObjects())) / MB;
  }

  double start_time_;  // Timestamp set in the constructor.
  int start_size_;  // Size of objects in heap set in constructor.
  GarbageCollector collector_;  // Type of collector.

  // A count (including this one, eg, the first collection is 1) of the
  // number of garbage collections.
  int gc_count_;

  // A count (including this one) of the number of full garbage collections.
  int full_gc_count_;

  // True if the current GC is a compacting full collection, false
  // otherwise.
  bool is_compacting_;

  // True if the *previous* full GC cwas a compacting collection (will be
  // false if there has not been a previous full GC).
  bool previous_has_compacted_;

  // On a full GC, a count of the number of marked objects.  Incremented
  // when an object is marked and decremented when an object's mark bit is
  // cleared.  Will be zero on a scavenge collection.
  int marked_count_;

  // The count from the end of the previous full GC.  Will be zero if there
  // was no previous full GC.
  int previous_marked_count_;

  // Amounts of time spent in different scopes during GC.
  double scopes_[Scope::kNumberOfScopes];

  // Total amount of space either wasted or contained in one of free lists
  // before the current GC.
  int in_free_list_or_wasted_before_gc_;

  // Difference between space used in the heap at the beginning of the current
  // collection and the end of the previous collection.
  int allocated_since_last_gc_;

  // Amount of time spent in mutator that is time elapsed between end of the
  // previous collection and the beginning of the current one.
  double spent_in_mutator_;

  // Size of objects promoted during the current collection.
  int promoted_objects_size_;

  // Maximum GC pause.
  static int max_gc_pause_;

  // Maximum size of objects alive after GC.
  static int max_alive_after_gc_;

  // Minimal interval between two subsequent collections.
  static int min_in_mutator_;

  // Size of objects alive after last GC.
  static int alive_after_last_gc_;

  static double last_gc_end_timestamp_;
};


class TranscendentalCache {
 public:
  enum Type {ACOS, ASIN, ATAN, COS, EXP, LOG, SIN, TAN, kNumberOfCaches};

  // Returns a heap number with f(input), where f is a math function specified
  // by the 'type' argument.
  inline Object* Get(Type type, double input);

  // The cache contains raw Object pointers.  This method disposes of
  // them before a garbage collection.
  void Clear();

 private:
  class SubCache {
    static const int kCacheSize = 512;

    explicit SubCache(Type t);

    inline Object* Get(double input) {
      Converter c;
      c.dbl = input;
      int hash = Hash(c);
      Element e = elements_[hash];
      if (e.in[0] == c.integers[0] &&
          e.in[1] == c.integers[1]) {
        ASSERT(e.output != NULL);
        Counters::transcendental_cache_hit.Increment();
        return e.output;
      }
      double answer = Calculate(input);
      Object* heap_number = heap_->AllocateHeapNumber(answer);
      if (!heap_number->IsFailure()) {
        elements_[hash].in[0] = c.integers[0];
        elements_[hash].in[1] = c.integers[1];
        elements_[hash].output = heap_number;
      }
      Counters::transcendental_cache_miss.Increment();
      return heap_number;
    }

    inline double Calculate(double input) {
      switch (type_) {
        case ACOS:
          return acos(input);
        case ASIN:
          return asin(input);
        case ATAN:
          return atan(input);
        case COS:
          return cos(input);
        case EXP:
          return exp(input);
        case LOG:
          return log(input);
        case SIN:
          return sin(input);
        case TAN:
          return tan(input);
        default:
          return 0.0;  // Never happens.
      }
    }
    struct Element {
      uint32_t in[2];
      Object* output;
    };
    union Converter {
      double dbl;
      uint32_t integers[2];
    };
    inline static int Hash(const Converter& c) {
      uint32_t hash = (c.integers[0] ^ c.integers[1]);
      hash ^= hash >> 16;
      hash ^= hash >> 8;
      return (hash & (kCacheSize - 1));
    }

    // Inline implementation of the caching.
    friend class TranscendentalCacheStub;

    // For evaluating value.
    friend class TranscendentalCache;

    Element elements_[kCacheSize];
    Type type_;
    Heap* heap_;

    DISALLOW_COPY_AND_ASSIGN(SubCache);
  };

  TranscendentalCache() {
    for (int i = 0; i < kNumberOfCaches; ++i) caches_[i] = NULL;
  }

  // Used to create an external reference.
  inline Address cache_array_address();

  // Instantiation
  friend class Isolate;
  // Inline implementation of the caching.
  friend class TranscendentalCacheStub;
  // Allow access to the caches_ array as an ExternalReference.
  friend class ExternalReference;

  SubCache* caches_[kNumberOfCaches];
  DISALLOW_COPY_AND_ASSIGN(TranscendentalCache);
};


// External strings table is a place where all external strings are
// registered.  We need to keep track of such strings to properly
// finalize them.
class ExternalStringTable : public AllStatic {
 public:
  // Registers an external string.
  inline static void AddString(String* string);

  inline static void Iterate(ObjectVisitor* v);

  // Restores internal invariant and gets rid of collected strings.
  // Must be called after each Iterate() that modified the strings.
  static void CleanUp();

  // Destroys all allocated memory.
  static void TearDown();

 private:
  friend class Heap;

  inline static void Verify();

  inline static void AddOldString(String* string);

  // Notifies the table that only a prefix of the new list is valid.
  inline static void ShrinkNewStrings(int position);

  // To speed up scavenge collections new space string are kept
  // separate from old space strings.
  static List<Object*> new_space_strings_;
  static List<Object*> old_space_strings_;
};

} }  // namespace v8::internal

#undef HEAP

#endif  // V8_HEAP_H_
