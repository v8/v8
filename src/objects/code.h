// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CODE_H_
#define V8_OBJECTS_CODE_H_

#include "src/codegen/handler-table.h"
#include "src/codegen/maglev-safepoint-table.h"
#include "src/objects/code-kind.h"
#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class BytecodeArray;
class CodeDesc;
class ObjectIterator;
class SafepointScope;

enum class Builtin;

// Code is a container for data fields related to its associated
// {InstructionStream} object. Since {InstructionStream} objects reside on
// write-protected pages within the heap, its header fields need to be
// immutable.  Every InstructionStream object has an associated Code object,
// but not every Code object has an InstructionStream (e.g. for builtins).
//
// Embedded builtins consist of on-heap Code objects, with an out-of-line body
// section. Accessors (e.g. InstructionStart), redirect to the off-heap area.
// Metadata table offsets remain relative to MetadataStart(), i.e. they point
// into the off-heap metadata section. The off-heap layout is described in
// detail in the EmbeddedData class, but at a high level one can assume a
// dedicated, out-of-line, instruction and metadata section for each embedded
// builtin:
//
//  +--------------------------+  <-- InstructionStart()
//  |   off-heap instructions  |
//  |           ...            |
//  +--------------------------+  <-- InstructionEnd()
//
//  +--------------------------+  <-- MetadataStart() (MS)
//  |    off-heap metadata     |
//  |           ...            |  <-- MS + handler_table_offset()
//  |                          |  <-- MS + constant_pool_offset()
//  |                          |  <-- MS + code_comments_offset()
//  |                          |  <-- MS + unwinding_info_offset()
//  +--------------------------+  <-- MetadataEnd()
//
class Code : public HeapObject {
 public:
  // When V8_EXTERNAL_CODE_SPACE is enabled, InstructionStream objects are
  // allocated in a separate pointer compression cage instead of the cage where
  // all the other objects are allocated.
  inline PtrComprCageBase code_cage_base() const;
  inline PtrComprCageBase code_cage_base(Isolate* isolate) const;

  // Back-reference to the InstructionStream object.
  //
  // Note the cage-less accessor versions may not be called if the current Code
  // object is InReadOnlySpace. That may only be the case for Code objects
  // representing builtins, or in other words, Code objects for which
  // has_instruction_stream() is never true.
  DECL_GETTER(instruction_stream, InstructionStream)
  DECL_RELAXED_GETTER(instruction_stream, InstructionStream)
  DECL_ACCESSORS(raw_instruction_stream, Object)
  DECL_RELAXED_GETTER(raw_instruction_stream, Object)

  // Whether this Code object has an associated InstructionStream (embedded
  // builtins don't).
  //
  // Note there's a short amount of time during CodeBuilder::BuildInternal in
  // which the Code object has been allocated and initialized, but the
  // InstructionStream doesn't exist yet - in this situation,
  // has_instruction_stream is `false` but will change to `true` once
  // InstructionStream has also been initialized.
  // Unfortunately, it's not easily possible to avoid this. The
  // InstructionStream can't be allocated first, since relocation requires
  // access to Code::relocation_info.
  inline bool has_instruction_stream() const;
  inline bool has_instruction_stream(RelaxedLoadTag) const;

  // The start of the associated instruction stream. Points either into an
  // on-heap InstructionStream object, or to the beginning of an embedded
  // builtin.
  DECL_GETTER(instruction_start, Address)
  DECL_PRIMITIVE_ACCESSORS(instruction_size, int)
  inline Address instruction_end() const;

  inline void SetInstructionStreamAndInstructionStart(
      Isolate* isolate_for_sandbox, InstructionStream code,
      WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline void SetInstructionStartForOffHeapBuiltin(Isolate* isolate_for_sandbox,
                                                   Address entry);
  inline void SetInstructionStartForSerialization(Isolate* isolate,
                                                  Address entry);
  inline void UpdateInstructionStart(Isolate* isolate_for_sandbox,
                                     InstructionStream istream);

  DECL_RELAXED_UINT16_ACCESSORS(kind_specific_flags)

  inline void initialize_flags(CodeKind kind, Builtin builtin_id,
                               bool is_turbofanned, int stack_slots);

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic.
  inline void clear_padding();

  // Flushes the instruction cache for the executable instructions of this code
  // object. Make sure to call this while the code is still writable.
  void FlushICache() const;

  DECL_PRIMITIVE_ACCESSORS(can_have_weak_objects, bool)
  DECL_PRIMITIVE_ACCESSORS(marked_for_deoptimization, bool)

  // [is_promise_rejection]: For kind BUILTIN tells whether the
  // exception thrown by the code will lead to promise rejection or
  // uncaught if both this and is_exception_caught is set.
  // Use GetBuiltinCatchPrediction to access this.
  DECL_PRIMITIVE_ACCESSORS(is_promise_rejection, bool)

  inline HandlerTable::CatchPrediction GetBuiltinCatchPrediction() const;

  DECL_PRIMITIVE_ACCESSORS(metadata_size, int)
  // [handler_table_offset]: The offset where the exception handler table
  // starts.
  DECL_PRIMITIVE_ACCESSORS(handler_table_offset, int)
  // [unwinding_info_offset]: Offset of the unwinding info section.
  DECL_PRIMITIVE_ACCESSORS(unwinding_info_offset, int32_t)
  // [deoptimization_data]: Array containing data for deopt for non-baseline
  // code.
  DECL_ACCESSORS(deoptimization_data, FixedArray)
  // [bytecode_or_interpreter_data]: BytecodeArray or InterpreterData for
  // baseline code.
  DECL_ACCESSORS(bytecode_or_interpreter_data, HeapObject)
  // [source_position_table]: ByteArray for the source positions table for
  // non-baseline code.
  DECL_ACCESSORS(source_position_table, ByteArray)
  // [bytecode_offset_table]: ByteArray for the bytecode offset for baseline
  // code.
  DECL_ACCESSORS(bytecode_offset_table, ByteArray)
  // [relocation_info]: InstructionStream relocation information
  DECL_ACCESSORS(relocation_info, ByteArray)
  DECL_PRIMITIVE_ACCESSORS(inlined_bytecode_size, unsigned)
  DECL_PRIMITIVE_ACCESSORS(osr_offset, BytecodeOffset)
  // [code_comments_offset]: Offset of the code comment section.
  DECL_PRIMITIVE_ACCESSORS(code_comments_offset, int)
  // [constant_pool offset]: Offset of the constant pool.
  DECL_PRIMITIVE_ACCESSORS(constant_pool_offset, int)

  // Unchecked accessors to be used during GC.
  inline ByteArray unchecked_relocation_info() const;
  inline FixedArray unchecked_deoptimization_data() const;

  inline CodeKind kind() const;
  inline Builtin builtin_id() const;
  inline bool is_builtin() const;

  inline bool is_optimized_code() const;
  inline bool is_wasm_code() const;

  inline bool is_interpreter_trampoline_builtin() const;
  inline bool is_baseline_trampoline_builtin() const;
  inline bool is_baseline_leave_frame_builtin() const;

  // Tells whether the code checks the tiering state in the function's feedback
  // vector.
  inline bool checks_tiering_state() const;

  // Tells whether the outgoing parameters of this code are tagged pointers.
  inline bool has_tagged_outgoing_params() const;

  // [is_maglevved]: Tells whether the code object was generated by the
  // Maglev optimizing compiler.
  inline bool is_maglevved() const;

  // [is_turbofanned]: Tells whether the code object was generated by the
  // TurboFan optimizing compiler.
  inline bool is_turbofanned() const;

  // [uses_safepoint_table]: Whether this InstructionStream object uses
  // safepoint tables (note the table may still be empty, see
  // has_safepoint_table).
  inline bool uses_safepoint_table() const;

  // [stack_slots]: If {uses_safepoint_table()}, the number of stack slots
  // reserved in the code prologue; otherwise 0.
  inline int stack_slots() const;

  inline ByteArray SourcePositionTable(Isolate* isolate,
                                       SharedFunctionInfo sfi) const;

  inline Address safepoint_table_address() const;
  inline int safepoint_table_size() const;
  inline bool has_safepoint_table() const;

  inline Address handler_table_address() const;
  inline int handler_table_size() const;
  inline bool has_handler_table() const;

  inline Address constant_pool() const;
  // An accessor to be used during GC if the instruction_stream moved and the
  // field was not updated yet.
  inline Address constant_pool(InstructionStream instruction_stream) const;
  inline int constant_pool_size() const;
  inline bool has_constant_pool() const;

  inline Address code_comments() const;
  inline int code_comments_size() const;
  inline bool has_code_comments() const;

  inline Address unwinding_info_start() const;
  inline Address unwinding_info_end() const;
  inline int unwinding_info_size() const;
  inline bool has_unwinding_info() const;

  inline byte* relocation_start() const;
  inline byte* relocation_end() const;
  inline int relocation_size() const;

  inline int safepoint_table_offset() const { return 0; }

  inline Address body_start() const;
  inline Address body_end() const;
  inline int body_size() const;

  inline Address metadata_start() const;
  inline Address metadata_end() const;

  // The size of the associated InstructionStream object, if it exists.
  inline int InstructionStreamObjectSize() const;

  // TODO(jgruber): This function tries to account for various parts of the
  // object graph, but is incomplete. Take it as a lower bound for the memory
  // associated with this Code object.
  inline int SizeIncludingMetadata() const;

  // The following functions include support for short builtin calls:
  //
  // When builtins un-embedding is enabled for the Isolate
  // (see Isolate::is_short_builtin_calls_enabled()) then both embedded and
  // un-embedded builtins might be exeuted and thus two kinds of |pc|s might
  // appear on the stack.
  // Unlike the paremeterless versions of the functions above the below variants
  // ensure that the instruction start correspond to the given |pc| value.
  // Thus for off-heap trampoline InstructionStream objects the result might be
  // the instruction start/end of the embedded code stream or of un-embedded
  // one. For normal InstructionStream objects these functions just return the
  // instruction_start/end() values.
  // TODO(11527): remove these versions once the full solution is ready.
  inline Address InstructionStart(Isolate* isolate, Address pc) const;
  inline Address InstructionEnd(Isolate* isolate, Address pc) const;
  inline bool contains(Isolate* isolate, Address pc) const;
  inline int GetOffsetFromInstructionStart(Isolate* isolate, Address pc) const;
  // Support for short builtin calls END.

  SafepointEntry GetSafepointEntry(Isolate* isolate, Address pc);
  MaglevSafepointEntry GetMaglevSafepointEntry(Isolate* isolate, Address pc);

  void SetMarkedForDeoptimization(Isolate* isolate, const char* reason);

  inline bool CanContainWeakObjects();
  inline bool IsWeakObject(HeapObject object);
  static inline bool IsWeakObjectInOptimizedCode(HeapObject object);
  static inline bool IsWeakObjectInDeoptimizationLiteralArray(Object object);

  // This function should be called only from GC.
  void ClearEmbeddedObjects(Heap* heap);

  // [embedded_objects_cleared]: If CodeKindIsOptimizedJSFunction(kind), tells
  // whether the embedded objects in the code marked for deoptimization were
  // cleared. Note that embedded_objects_cleared() implies
  // marked_for_deoptimization().
  inline bool embedded_objects_cleared() const;
  inline void set_embedded_objects_cleared(bool flag);

  // Migrate code from desc without flushing the instruction cache.
  void CopyFromNoFlush(ByteArray reloc_info, Heap* heap, const CodeDesc& desc);
  void RelocateFromDesc(ByteArray reloc_info, Heap* heap, const CodeDesc& desc);

  bool IsIsolateIndependent(Isolate* isolate);

  inline uintptr_t GetBaselineStartPCForBytecodeOffset(int bytecode_offset,
                                                       BytecodeArray bytecodes);

  inline uintptr_t GetBaselineEndPCForBytecodeOffset(int bytecode_offset,
                                                     BytecodeArray bytecodes);

  // Returns true if the function is inlined in the code.
  bool Inlines(SharedFunctionInfo sfi);

  // Returns the PC of the next bytecode in execution order.
  // If the bytecode at the given offset is JumpLoop, the PC of the jump target
  // is returned. Other jumps are not allowed.
  // For other bytecodes this is equivalent to
  // GetBaselineEndPCForBytecodeOffset.
  inline uintptr_t GetBaselinePCForNextExecutedBytecode(
      int bytecode_offset, BytecodeArray bytecodes);

  inline int GetBytecodeOffsetForBaselinePC(Address baseline_pc,
                                            BytecodeArray bytecodes);

  inline void IterateDeoptimizationLiterals(RootVisitor* v);

  static inline Code FromTargetAddress(Address address);

#ifdef ENABLE_DISASSEMBLER
  V8_EXPORT_PRIVATE void Disassemble(const char* name, std::ostream& os,
                                     Isolate* isolate,
                                     Address current_pc = kNullAddress);
#endif  // ENABLE_DISASSEMBLER

  DECL_CAST(Code)
  DECL_PRINTER(Code)
  DECL_VERIFIER(Code)

// Layout description.
#define CODE_DATA_FIELDS(V)                                                   \
  /* Strong pointer fields. */                                                \
  V(kRelocationInfoOffset, kTaggedSize)                                       \
  V(kDeoptimizationDataOrInterpreterDataOffset, kTaggedSize)                  \
  V(kPositionTableOffset, kTaggedSize)                                        \
  V(kPointerFieldsStrongEndOffset, 0)                                         \
  /* Strong InstructionStream pointer fields. */                              \
  V(kInstructionStreamOffset, kTaggedSize)                                    \
  V(kCodePointerFieldsStrongEndOffset, 0)                                     \
  /* Raw data fields. */                                                      \
  /* Data or code not directly visited by GC directly starts here. */         \
  V(kDataStart, 0)                                                            \
  V(kInstructionStartOffset, kSystemPointerSize)                              \
  /* The serializer needs to copy bytes starting from here verbatim. */       \
  V(kFlagsOffset, kInt32Size)                                                 \
  V(kBuiltinIdOffset, kInt16Size)                                             \
  V(kKindSpecificFlagsOffset, kInt16Size)                                     \
  V(kInstructionSizeOffset, kIntSize)                                         \
  V(kMetadataSizeOffset, kIntSize)                                            \
  V(kInlinedBytecodeSizeOffset, kIntSize)                                     \
  V(kOsrOffsetOffset, kInt32Size)                                             \
  V(kHandlerTableOffsetOffset, kIntSize)                                      \
  V(kUnwindingInfoOffsetOffset, kInt32Size)                                   \
  V(kConstantPoolOffsetOffset, V8_EMBEDDED_CONSTANT_POOL_BOOL ? kIntSize : 0) \
  V(kCodeCommentsOffsetOffset, kIntSize)                                      \
  V(kUnalignedSize, OBJECT_POINTER_PADDING(kUnalignedSize))                   \
  /* Total size. */                                                           \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, CODE_DATA_FIELDS)
#undef CODE_DATA_FIELDS

#ifdef V8_EXTERNAL_CODE_SPACE
  template <typename T>
  using ExternalCodeField =
      TaggedField<T, kInstructionStreamOffset, ExternalCodeCompressionScheme>;
#else
  template <typename T>
  using ExternalCodeField = TaggedField<T, kInstructionStreamOffset>;
#endif  // V8_EXTERNAL_CODE_SPACE

  class BodyDescriptor;

  // Flags layout.
#define FLAGS_BIT_FIELDS(V, _)      \
  V(KindField, CodeKind, 4, _)      \
  V(IsTurbofannedField, bool, 1, _) \
  V(StackSlotsField, int, 24, _)
  DEFINE_BIT_FIELDS(FLAGS_BIT_FIELDS)
#undef FLAGS_BIT_FIELDS
  // TODO(v8:13784): merge this with KindSpecificFlags by dropping the
  // IsPromiseRejection field or taking one bit from the StackSlots field.
  // The other 3 bits are still free.
  static_assert(FLAGS_BIT_FIELDS_Ranges::kBitsCount == 29);
  static_assert(FLAGS_BIT_FIELDS_Ranges::kBitsCount <=
                FIELD_SIZE(kFlagsOffset) * kBitsPerByte);
  static_assert(kCodeKindCount <= KindField::kNumValues);

  // KindSpecificFlags layout.
#define KIND_SPECIFIC_FLAGS_BIT_FIELDS(V, _)  \
  V(MarkedForDeoptimizationField, bool, 1, _) \
  V(EmbeddedObjectsClearedField, bool, 1, _)  \
  V(CanHaveWeakObjectsField, bool, 1, _)      \
  V(IsPromiseRejectionField, bool, 1, _)
  DEFINE_BIT_FIELDS(KIND_SPECIFIC_FLAGS_BIT_FIELDS)
#undef KIND_SPECIFIC_FLAGS_BIT_FIELDS
  // The other 12 bits are still free.
  static_assert(KIND_SPECIFIC_FLAGS_BIT_FIELDS_Ranges::kBitsCount == 4);
  static_assert(KIND_SPECIFIC_FLAGS_BIT_FIELDS_Ranges::kBitsCount <=
                FIELD_SIZE(Code::kKindSpecificFlagsOffset) * kBitsPerByte);

  // The {marked_for_deoptimization} field is accessed from generated code.
  static const int kMarkedForDeoptimizationBit =
      MarkedForDeoptimizationField::kShift;

  class OptimizedCodeIterator;

  // Reserve one argument count value as the "don't adapt arguments" sentinel.
  static const int kArgumentsBits = 16;
  static const int kMaxArguments = (1 << kArgumentsBits) - 2;

 private:
  inline void init_instruction_start(Isolate* isolate, Address initial_value);
  inline void set_instruction_start(Isolate* isolate, Address value);

  DECL_RELAXED_UINT16_ACCESSORS(flags)

  enum BytecodeToPCPosition {
    kPcAtStartOfBytecode,
    // End of bytecode equals the start of the next bytecode.
    // We need it when we deoptimize to the next bytecode (lazy deopt or deopt
    // of non-topmost frame).
    kPcAtEndOfBytecode
  };
  inline uintptr_t GetBaselinePCForBytecodeOffset(int bytecode_offset,
                                                  BytecodeToPCPosition position,
                                                  BytecodeArray bytecodes);

  template <typename IsolateT>
  friend class Deserializer;
  friend class ReadOnlyDeserializer;  // For init_instruction_start.
  friend Factory;
  friend FactoryBase<Factory>;
  friend FactoryBase<LocalFactory>;

  OBJECT_CONSTRUCTORS(Code, HeapObject);
};

// A Code object when used in situations where gc might be in progress. The
// underlying pointer is guaranteed to be a Code object.
//
// Semantics around Code and InstructionStream objects are quite delicate when
// GC is in progress and objects are currently being moved, because the
// tightly-coupled object pair {Code,InstructionStream} are conceptually
// treated as a single object in our codebase, and we frequently convert
// between the two. However, during GC, extra care must be taken when accessing
// the `Code::instruction_stream` and `InstructionStream::code` slots because
// they may contain forwarding pointers.
//
// This class a) clarifies at use sites that we're dealing with a Code object
// in a situation that requires special semantics, and b) safely implements
// related functions.
//
// Note that both the underlying Code object and the associated
// InstructionStream may be forwarding pointers, thus type checks and normal
// (checked) casts do not work on GcSafeCode.
class GcSafeCode : public HeapObject {
 public:
  DECL_CAST(GcSafeCode)

  // Use with care, this casts away knowledge that we're dealing with a
  // special-semantics object.
  inline Code UnsafeCastToCode() const;

  // Safe accessors (these just forward to Code methods).
  inline Address instruction_start() const;
  inline Address instruction_end() const;
  inline bool is_builtin() const;
  inline Builtin builtin_id() const;
  inline CodeKind kind() const;
  inline bool is_interpreter_trampoline_builtin() const;
  inline bool is_baseline_trampoline_builtin() const;
  inline bool is_baseline_leave_frame_builtin() const;
  inline bool has_instruction_stream() const;
  inline bool is_maglevved() const;
  inline bool is_turbofanned() const;
  inline bool has_tagged_outgoing_params() const;
  inline bool marked_for_deoptimization() const;
  inline Object raw_instruction_stream() const;
  inline Address constant_pool() const;
  inline Address constant_pool(InstructionStream istream) const;
  inline Address safepoint_table_address() const;
  inline int stack_slots() const;

  inline int GetOffsetFromInstructionStart(Isolate* isolate, Address pc) const;
  inline Address InstructionStart(Isolate* isolate, Address pc) const;
  inline Address InstructionEnd(Isolate* isolate, Address pc) const;
  inline bool CanDeoptAt(Isolate* isolate, Address pc) const;
  inline Object raw_instruction_stream(PtrComprCageBase code_cage_base) const;

 private:
  OBJECT_CONSTRUCTORS(GcSafeCode, HeapObject);
};

// InstructionStream contains the instruction stream for V8-generated code
// objects.
//
// When V8_EXTERNAL_CODE_SPACE is enabled, InstructionStream objects are
// allocated in a separate pointer compression cage instead of the cage where
// all the other objects are allocated.
class InstructionStream : public HeapObject {
 public:
  NEVER_READ_ONLY_SPACE

  // All InstructionStream objects have the following layout:
  //
  //  +--------------------------+
  //  |          header          |
  //  +--------------------------+  <-- body_start()
  //  |       instructions       |   == instruction_start()
  //  |           ...            |
  //  | padded to meta alignment |      see kMetadataAlignment
  //  +--------------------------+  <-- instruction_end()
  //  |         metadata         |   == metadata_start() (MS)
  //  |           ...            |
  //  |                          |  <-- MS + handler_table_offset()
  //  |                          |  <-- MS + constant_pool_offset()
  //  |                          |  <-- MS + code_comments_offset()
  //  |                          |  <-- MS + unwinding_info_offset()
  //  | padded to obj alignment  |
  //  +--------------------------+  <-- metadata_end() == body_end()
  //  | padded to kCodeAlignmentMinusCodeHeader
  //  +--------------------------+
  //
  // In other words, the variable-size 'body' consists of 'instructions' and
  // 'metadata'.

  // Constants for use in static asserts, stating whether the body is adjacent,
  // i.e. instructions and metadata areas are adjacent.
  static constexpr bool kOnHeapBodyIsContiguous = true;
  static constexpr bool kOffHeapBodyIsContiguous = false;
  static constexpr bool kBodyIsContiguous =
      kOnHeapBodyIsContiguous && kOffHeapBodyIsContiguous;

  inline Address instruction_start() const;

  // The metadata section is aligned to this value.
  static constexpr int kMetadataAlignment = kIntSize;

  // [code]: The associated Code object.
  DECL_RELEASE_ACQUIRE_ACCESSORS(code, Code)
  DECL_RELEASE_ACQUIRE_ACCESSORS(raw_code, HeapObject)
  inline Code unchecked_code(AcquireLoadTag tag) const;

  // When V8_EXTERNAL_CODE_SPACE is enabled, InstructionStream objects are
  // allocated in a separate pointer compression cage instead of the cage where
  // all the other objects are allocated. This field contains cage base value
  // which is used for decompressing the references to non-InstructionStream
  // objects (map, deoptimization_data, etc.).
  inline PtrComprCageBase main_cage_base() const;
  inline PtrComprCageBase main_cage_base(RelaxedLoadTag) const;
  inline void set_main_cage_base(Address cage_base, RelaxedStoreTag);

  // The size of the entire body section, containing instructions and inlined
  // metadata.
  DECL_PRIMITIVE_ACCESSORS(body_size, int)
  inline Address body_end() const;

  // The entire code object including its header is copied verbatim to the
  // snapshot so that it can be written in one, fast, memcpy during
  // deserialization. The deserializer will overwrite some pointers, rather
  // like a runtime linker, but the random allocation addresses used in the
  // mksnapshot process would still be present in the unlinked snapshot data,
  // which would make snapshot production non-reproducible. This method wipes
  // out the to-be-overwritten header data for reproducible snapshots.
  inline void WipeOutHeader();

  static inline InstructionStream FromTargetAddress(Address address);
  static inline InstructionStream FromEntryAddress(Address location_of_address);

  // Relocate the code by delta bytes. Called to signal that this code
  // object has been moved by delta bytes.
  void Relocate(intptr_t delta);

  inline void clear_padding();

  static constexpr int TrailingPaddingSizeFor(int body_size) {
    return RoundUp<kCodeAlignment>(kHeaderSize + body_size) - kHeaderSize -
           body_size;
  }
  static constexpr int SizeFor(int body_size) {
    return kHeaderSize + body_size + TrailingPaddingSizeFor(body_size);
  }
  inline int Size() const;

  DECL_CAST(InstructionStream)
  DECL_PRINTER(InstructionStream)
  DECL_VERIFIER(InstructionStream)

  // Layout description.
#define ISTREAM_FIELDS(V)                                             \
  V(kCodeOffset, kTaggedSize)                                         \
  /* Data or code not directly visited by GC directly starts here. */ \
  V(kDataStart, 0)                                                    \
  V(kMainCageBaseUpper32BitsOffset,                                   \
    V8_EXTERNAL_CODE_SPACE_BOOL ? kTaggedSize : 0)                    \
  V(kBodySizeOffset, kIntSize)                                        \
  V(kUnalignedSize, OBJECT_POINTER_PADDING(kUnalignedSize))           \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, ISTREAM_FIELDS)
#undef ISTREAM_FIELDS

  static_assert(kCodeAlignment > kHeaderSize);
  // We do two things to ensure kCodeAlignment of the entry address:
  // 1) Add kCodeAlignmentMinusCodeHeader padding once in the beginning of every
  //    MemoryChunk.
  // 2) Round up all IStream allocations to a multiple of kCodeAlignment, see
  //    TrailingPaddingSizeFor.
  // Together, the IStream object itself will always start at offset
  // kCodeAlignmentMinusCodeHeader, which aligns the entry to kCodeAlignment.
  static constexpr int kCodeAlignmentMinusCodeHeader =
      kCodeAlignment - kHeaderSize;

  class BodyDescriptor;

 private:
  OBJECT_CONSTRUCTORS(InstructionStream, HeapObject);
};

class Code::OptimizedCodeIterator {
 public:
  explicit OptimizedCodeIterator(Isolate* isolate);
  OptimizedCodeIterator(const OptimizedCodeIterator&) = delete;
  OptimizedCodeIterator& operator=(const OptimizedCodeIterator&) = delete;
  Code Next();

 private:
  Isolate* isolate_;
  std::unique_ptr<SafepointScope> safepoint_scope_;
  std::unique_ptr<ObjectIterator> object_iterator_;
  enum { kIteratingCodeSpace, kIteratingCodeLOSpace, kDone } state_;

  DISALLOW_GARBAGE_COLLECTION(no_gc)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_CODE_H_
