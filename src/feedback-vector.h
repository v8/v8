// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FEEDBACK_VECTOR_H_
#define V8_FEEDBACK_VECTOR_H_

#include <vector>

#include "src/base/logging.h"
#include "src/elements-kind.h"
#include "src/objects.h"
#include "src/type-hints.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

enum class FeedbackVectorSlotKind {
  // This kind means that the slot points to the middle of other slot
  // which occupies more than one feedback vector element.
  // There must be no such slots in the system.
  INVALID,

  CALL_IC,
  LOAD_IC,
  LOAD_GLOBAL_NOT_INSIDE_TYPEOF_IC,
  LOAD_GLOBAL_INSIDE_TYPEOF_IC,
  KEYED_LOAD_IC,
  STORE_SLOPPY_IC,
  STORE_STRICT_IC,
  KEYED_STORE_SLOPPY_IC,
  KEYED_STORE_STRICT_IC,
  INTERPRETER_BINARYOP_IC,
  INTERPRETER_COMPARE_IC,
  STORE_DATA_PROPERTY_IN_LITERAL_IC,
  CREATE_CLOSURE,
  LITERAL,
  // This is a general purpose slot that occupies one feedback vector element.
  GENERAL,

  KINDS_NUMBER  // Last value indicating number of kinds.
};

inline bool IsCallICKind(FeedbackVectorSlotKind kind) {
  return kind == FeedbackVectorSlotKind::CALL_IC;
}

inline bool IsLoadICKind(FeedbackVectorSlotKind kind) {
  return kind == FeedbackVectorSlotKind::LOAD_IC;
}

inline bool IsLoadGlobalICKind(FeedbackVectorSlotKind kind) {
  return kind == FeedbackVectorSlotKind::LOAD_GLOBAL_NOT_INSIDE_TYPEOF_IC ||
         kind == FeedbackVectorSlotKind::LOAD_GLOBAL_INSIDE_TYPEOF_IC;
}

inline bool IsKeyedLoadICKind(FeedbackVectorSlotKind kind) {
  return kind == FeedbackVectorSlotKind::KEYED_LOAD_IC;
}

inline bool IsStoreICKind(FeedbackVectorSlotKind kind) {
  return kind == FeedbackVectorSlotKind::STORE_SLOPPY_IC ||
         kind == FeedbackVectorSlotKind::STORE_STRICT_IC;
}

inline bool IsKeyedStoreICKind(FeedbackVectorSlotKind kind) {
  return kind == FeedbackVectorSlotKind::KEYED_STORE_SLOPPY_IC ||
         kind == FeedbackVectorSlotKind::KEYED_STORE_STRICT_IC;
}

inline TypeofMode GetTypeofModeFromICKind(FeedbackVectorSlotKind kind) {
  DCHECK(IsLoadGlobalICKind(kind));
  return (kind == FeedbackVectorSlotKind::LOAD_GLOBAL_INSIDE_TYPEOF_IC)
             ? INSIDE_TYPEOF
             : NOT_INSIDE_TYPEOF;
}

inline LanguageMode GetLanguageModeFromICKind(FeedbackVectorSlotKind kind) {
  DCHECK(IsStoreICKind(kind) || IsKeyedStoreICKind(kind));
  return (kind == FeedbackVectorSlotKind::STORE_SLOPPY_IC ||
          kind == FeedbackVectorSlotKind::KEYED_STORE_SLOPPY_IC)
             ? SLOPPY
             : STRICT;
}

std::ostream& operator<<(std::ostream& os, FeedbackVectorSlotKind kind);

template <typename Derived>
class FeedbackVectorSpecBase {
 public:
  FeedbackVectorSlot AddCallICSlot() {
    return AddSlot(FeedbackVectorSlotKind::CALL_IC);
  }

  FeedbackVectorSlot AddLoadICSlot() {
    return AddSlot(FeedbackVectorSlotKind::LOAD_IC);
  }

  FeedbackVectorSlot AddLoadGlobalICSlot(TypeofMode typeof_mode) {
    return AddSlot(
        typeof_mode == INSIDE_TYPEOF
            ? FeedbackVectorSlotKind::LOAD_GLOBAL_INSIDE_TYPEOF_IC
            : FeedbackVectorSlotKind::LOAD_GLOBAL_NOT_INSIDE_TYPEOF_IC);
  }

  FeedbackVectorSlot AddCreateClosureSlot() {
    return AddSlot(FeedbackVectorSlotKind::CREATE_CLOSURE);
  }

  FeedbackVectorSlot AddKeyedLoadICSlot() {
    return AddSlot(FeedbackVectorSlotKind::KEYED_LOAD_IC);
  }

  FeedbackVectorSlot AddStoreICSlot(LanguageMode language_mode) {
    STATIC_ASSERT(LANGUAGE_END == 2);
    return AddSlot(is_strict(language_mode)
                       ? FeedbackVectorSlotKind::STORE_STRICT_IC
                       : FeedbackVectorSlotKind::STORE_SLOPPY_IC);
  }

  FeedbackVectorSlot AddKeyedStoreICSlot(LanguageMode language_mode) {
    STATIC_ASSERT(LANGUAGE_END == 2);
    return AddSlot(is_strict(language_mode)
                       ? FeedbackVectorSlotKind::KEYED_STORE_STRICT_IC
                       : FeedbackVectorSlotKind::KEYED_STORE_SLOPPY_IC);
  }

  FeedbackVectorSlot AddInterpreterBinaryOpICSlot() {
    return AddSlot(FeedbackVectorSlotKind::INTERPRETER_BINARYOP_IC);
  }

  FeedbackVectorSlot AddInterpreterCompareICSlot() {
    return AddSlot(FeedbackVectorSlotKind::INTERPRETER_COMPARE_IC);
  }

  FeedbackVectorSlot AddGeneralSlot() {
    return AddSlot(FeedbackVectorSlotKind::GENERAL);
  }

  FeedbackVectorSlot AddLiteralSlot() {
    return AddSlot(FeedbackVectorSlotKind::LITERAL);
  }

  FeedbackVectorSlot AddStoreDataPropertyInLiteralICSlot() {
    return AddSlot(FeedbackVectorSlotKind::STORE_DATA_PROPERTY_IN_LITERAL_IC);
  }

#ifdef OBJECT_PRINT
  // For gdb debugging.
  void Print();
#endif  // OBJECT_PRINT

  DECLARE_PRINTER(FeedbackVectorSpec)

 private:
  inline FeedbackVectorSlot AddSlot(FeedbackVectorSlotKind kind);

  Derived* This() { return static_cast<Derived*>(this); }
};

class StaticFeedbackVectorSpec
    : public FeedbackVectorSpecBase<StaticFeedbackVectorSpec> {
 public:
  StaticFeedbackVectorSpec() : slot_count_(0) {}

  int slots() const { return slot_count_; }

  FeedbackVectorSlotKind GetKind(FeedbackVectorSlot slot) const {
    DCHECK(slot.ToInt() >= 0 && slot.ToInt() < slot_count_);
    return kinds_[slot.ToInt()];
  }

 private:
  friend class FeedbackVectorSpecBase<StaticFeedbackVectorSpec>;

  void append(FeedbackVectorSlotKind kind) {
    DCHECK(slot_count_ < kMaxLength);
    kinds_[slot_count_++] = kind;
  }

  static const int kMaxLength = 12;

  int slot_count_;
  FeedbackVectorSlotKind kinds_[kMaxLength];
};

class FeedbackVectorSpec : public FeedbackVectorSpecBase<FeedbackVectorSpec> {
 public:
  explicit FeedbackVectorSpec(Zone* zone) : slot_kinds_(zone) {
    slot_kinds_.reserve(16);
  }

  int slots() const { return static_cast<int>(slot_kinds_.size()); }

  FeedbackVectorSlotKind GetKind(FeedbackVectorSlot slot) const {
    return static_cast<FeedbackVectorSlotKind>(slot_kinds_.at(slot.ToInt()));
  }

 private:
  friend class FeedbackVectorSpecBase<FeedbackVectorSpec>;

  void append(FeedbackVectorSlotKind kind) {
    slot_kinds_.push_back(static_cast<unsigned char>(kind));
  }

  ZoneVector<unsigned char> slot_kinds_;
};

// The shape of the FeedbackMetadata is an array with:
// 0: slot_count
// 1: names table
// 2: parameters table
// 3..N: slot kinds packed into a bit vector
//
class FeedbackMetadata : public FixedArray {
 public:
  // Casting.
  static inline FeedbackMetadata* cast(Object* obj);

  static const int kSlotsCountIndex = 0;
  static const int kReservedIndexCount = 1;

  // Returns number of feedback vector elements used by given slot kind.
  static inline int GetSlotSize(FeedbackVectorSlotKind kind);

  bool SpecDiffersFrom(const FeedbackVectorSpec* other_spec) const;

  inline bool is_empty() const;

  // Returns number of slots in the vector.
  inline int slot_count() const;

  // Returns slot kind for given slot.
  FeedbackVectorSlotKind GetKind(FeedbackVectorSlot slot) const;

  template <typename Spec>
  static Handle<FeedbackMetadata> New(Isolate* isolate, const Spec* spec);

#ifdef OBJECT_PRINT
  // For gdb debugging.
  void Print();
#endif  // OBJECT_PRINT

  DECLARE_PRINTER(FeedbackMetadata)

  static const char* Kind2String(FeedbackVectorSlotKind kind);

 private:
  static const int kFeedbackVectorSlotKindBits = 5;
  STATIC_ASSERT(static_cast<int>(FeedbackVectorSlotKind::KINDS_NUMBER) <
                (1 << kFeedbackVectorSlotKindBits));

  void SetKind(FeedbackVectorSlot slot, FeedbackVectorSlotKind kind);

  typedef BitSetComputer<FeedbackVectorSlotKind, kFeedbackVectorSlotKindBits,
                         kSmiValueSize, uint32_t>
      VectorICComputer;

  DISALLOW_IMPLICIT_CONSTRUCTORS(FeedbackMetadata);
};

// The shape of the FeedbackVector is an array with:
// 0: feedback metadata
// 1: invocation count
// 2: feedback slot #0
// ...
// 2 + slot_count - 1: feedback slot #(slot_count-1)
//
class FeedbackVector : public FixedArray {
 public:
  // Casting.
  static inline FeedbackVector* cast(Object* obj);

  static const int kMetadataIndex = 0;
  static const int kInvocationCountIndex = 1;
  static const int kReservedIndexCount = 2;

  inline void ComputeCounts(int* with_type_info, int* generic,
                            int* vector_ic_count, bool code_is_interpreted);

  inline bool is_empty() const;

  // Returns number of slots in the vector.
  inline int slot_count() const;

  inline FeedbackMetadata* metadata() const;
  inline int invocation_count() const;

  // Conversion from a slot to an integer index to the underlying array.
  static int GetIndex(FeedbackVectorSlot slot) {
    return kReservedIndexCount + slot.ToInt();
  }

  // Conversion from an integer index to the underlying array to a slot.
  static inline FeedbackVectorSlot ToSlot(int index);
  inline Object* Get(FeedbackVectorSlot slot) const;
  inline void Set(FeedbackVectorSlot slot, Object* value,
                  WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Returns slot kind for given slot.
  FeedbackVectorSlotKind GetKind(FeedbackVectorSlot slot) const;

  static Handle<FeedbackVector> New(Isolate* isolate,
                                    Handle<FeedbackMetadata> metadata);

  static Handle<FeedbackVector> Copy(Isolate* isolate,
                                     Handle<FeedbackVector> vector);

#define DEFINE_SLOT_KIND_PREDICATE(Name) \
  bool Name(FeedbackVectorSlot slot) const { return Name##Kind(GetKind(slot)); }

  DEFINE_SLOT_KIND_PREDICATE(IsCallIC)
  DEFINE_SLOT_KIND_PREDICATE(IsLoadIC)
  DEFINE_SLOT_KIND_PREDICATE(IsLoadGlobalIC)
  DEFINE_SLOT_KIND_PREDICATE(IsKeyedLoadIC)
  DEFINE_SLOT_KIND_PREDICATE(IsStoreIC)
  DEFINE_SLOT_KIND_PREDICATE(IsKeyedStoreIC)
#undef DEFINE_SLOT_KIND_PREDICATE

  // Returns typeof mode encoded into kind of given slot.
  inline TypeofMode GetTypeofMode(FeedbackVectorSlot slot) const {
    return GetTypeofModeFromICKind(GetKind(slot));
  }

  // Returns language mode encoded into kind of given slot.
  inline LanguageMode GetLanguageMode(FeedbackVectorSlot slot) const {
    return GetLanguageModeFromICKind(GetKind(slot));
  }

#ifdef OBJECT_PRINT
  // For gdb debugging.
  void Print();
#endif  // OBJECT_PRINT

  DECLARE_PRINTER(FeedbackVector)

  // Clears the vector slots.
  void ClearSlots(SharedFunctionInfo* shared) { ClearSlotsImpl(shared, true); }

  void ClearSlotsAtGCTime(SharedFunctionInfo* shared) {
    ClearSlotsImpl(shared, false);
  }

  // The object that indicates an uninitialized cache.
  static inline Handle<Symbol> UninitializedSentinel(Isolate* isolate);

  // The object that indicates a megamorphic state.
  static inline Handle<Symbol> MegamorphicSentinel(Isolate* isolate);

  // The object that indicates a premonomorphic state.
  static inline Handle<Symbol> PremonomorphicSentinel(Isolate* isolate);

  // A raw version of the uninitialized sentinel that's safe to read during
  // garbage collection (e.g., for patching the cache).
  static inline Symbol* RawUninitializedSentinel(Isolate* isolate);

 private:
  void ClearSlotsImpl(SharedFunctionInfo* shared, bool force_clear);

  DISALLOW_IMPLICIT_CONSTRUCTORS(FeedbackVector);
};

// The following asserts protect an optimization in type feedback vector
// code that looks into the contents of a slot assuming to find a String,
// a Symbol, an AllocationSite, a WeakCell, or a FixedArray.
STATIC_ASSERT(WeakCell::kSize >= 2 * kPointerSize);
STATIC_ASSERT(WeakCell::kValueOffset == AllocationSite::kTransitionInfoOffset);
STATIC_ASSERT(WeakCell::kValueOffset == FixedArray::kLengthOffset);
STATIC_ASSERT(WeakCell::kValueOffset == Name::kHashFieldSlot);
// Verify that an empty hash field looks like a tagged object, but can't
// possibly be confused with a pointer.
STATIC_ASSERT((Name::kEmptyHashField & kHeapObjectTag) == kHeapObjectTag);
STATIC_ASSERT(Name::kEmptyHashField == 0x3);
// Verify that a set hash field will not look like a tagged object.
STATIC_ASSERT(Name::kHashNotComputedMask == kHeapObjectTag);

class FeedbackMetadataIterator {
 public:
  explicit FeedbackMetadataIterator(Handle<FeedbackMetadata> metadata)
      : metadata_handle_(metadata),
        next_slot_(FeedbackVectorSlot(0)),
        slot_kind_(FeedbackVectorSlotKind::INVALID) {}

  explicit FeedbackMetadataIterator(FeedbackMetadata* metadata)
      : metadata_(metadata),
        next_slot_(FeedbackVectorSlot(0)),
        slot_kind_(FeedbackVectorSlotKind::INVALID) {}

  inline bool HasNext() const;

  inline FeedbackVectorSlot Next();

  // Returns slot kind of the last slot returned by Next().
  FeedbackVectorSlotKind kind() const {
    DCHECK_NE(FeedbackVectorSlotKind::INVALID, slot_kind_);
    DCHECK_NE(FeedbackVectorSlotKind::KINDS_NUMBER, slot_kind_);
    return slot_kind_;
  }

  // Returns entry size of the last slot returned by Next().
  inline int entry_size() const;

 private:
  FeedbackMetadata* metadata() const {
    return !metadata_handle_.is_null() ? *metadata_handle_ : metadata_;
  }

  // The reason for having a handle and a raw pointer to the meta data is
  // to have a single iterator implementation for both "handlified" and raw
  // pointer use cases.
  Handle<FeedbackMetadata> metadata_handle_;
  FeedbackMetadata* metadata_;
  FeedbackVectorSlot cur_slot_;
  FeedbackVectorSlot next_slot_;
  FeedbackVectorSlotKind slot_kind_;
};

// A FeedbackNexus is the combination of a FeedbackVector and a slot.
// Derived classes customize the update and retrieval of feedback.
class FeedbackNexus {
 public:
  FeedbackNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : vector_handle_(vector), vector_(NULL), slot_(slot) {}
  FeedbackNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : vector_(vector), slot_(slot) {}
  virtual ~FeedbackNexus() {}

  Handle<FeedbackVector> vector_handle() const {
    DCHECK(vector_ == NULL);
    return vector_handle_;
  }
  FeedbackVector* vector() const {
    return vector_handle_.is_null() ? vector_ : *vector_handle_;
  }
  FeedbackVectorSlot slot() const { return slot_; }
  FeedbackVectorSlotKind kind() const { return vector()->GetKind(slot()); }

  InlineCacheState ic_state() const { return StateFromFeedback(); }
  bool IsUninitialized() const { return StateFromFeedback() == UNINITIALIZED; }
  Map* FindFirstMap() const {
    MapHandleList maps;
    ExtractMaps(&maps);
    if (maps.length() > 0) return *maps.at(0);
    return NULL;
  }

  // TODO(mvstanton): remove FindAllMaps, it didn't survive a code review.
  void FindAllMaps(MapHandleList* maps) const { ExtractMaps(maps); }

  virtual InlineCacheState StateFromFeedback() const = 0;
  virtual int ExtractMaps(MapHandleList* maps) const;
  virtual MaybeHandle<Object> FindHandlerForMap(Handle<Map> map) const;
  virtual bool FindHandlers(List<Handle<Object>>* code_list,
                            int length = -1) const;
  virtual Name* FindFirstName() const { return NULL; }

  virtual void ConfigureUninitialized();
  virtual void ConfigurePremonomorphic();
  virtual void ConfigureMegamorphic();

  inline Object* GetFeedback() const;
  inline Object* GetFeedbackExtra() const;

  inline Isolate* GetIsolate() const;

 protected:
  inline void SetFeedback(Object* feedback,
                          WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  inline void SetFeedbackExtra(Object* feedback_extra,
                               WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  Handle<FixedArray> EnsureArrayOfSize(int length);
  Handle<FixedArray> EnsureExtraArrayOfSize(int length);
  void InstallHandlers(Handle<FixedArray> array, MapHandleList* maps,
                       List<Handle<Object>>* handlers);

 private:
  // The reason for having a vector handle and a raw pointer is that we can and
  // should use handles during IC miss, but not during GC when we clear ICs. If
  // you have a handle to the vector that is better because more operations can
  // be done, like allocation.
  Handle<FeedbackVector> vector_handle_;
  FeedbackVector* vector_;
  FeedbackVectorSlot slot_;
};

class CallICNexus final : public FeedbackNexus {
 public:
  CallICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsCallIC(slot));
  }
  CallICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsCallIC(slot));
  }

  void Clear(Code* host);

  void ConfigureUninitialized() override;
  void ConfigureMonomorphicArray();
  void ConfigureMonomorphic(Handle<JSFunction> function);
  void ConfigureMegamorphic() final;
  void ConfigureMegamorphic(int call_count);

  InlineCacheState StateFromFeedback() const final;

  int ExtractMaps(MapHandleList* maps) const final {
    // CallICs don't record map feedback.
    return 0;
  }
  MaybeHandle<Object> FindHandlerForMap(Handle<Map> map) const final {
    return MaybeHandle<Code>();
  }
  bool FindHandlers(List<Handle<Object>>* code_list,
                    int length = -1) const final {
    return length == 0;
  }

  int ExtractCallCount();

  // Compute the call frequency based on the call count and the invocation
  // count (taken from the type feedback vector).
  float ComputeCallFrequency();
};

class LoadICNexus : public FeedbackNexus {
 public:
  LoadICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsLoadIC(slot));
  }
  LoadICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsLoadIC(slot));
  }

  void Clear(Code* host);

  void ConfigureMonomorphic(Handle<Map> receiver_map, Handle<Object> handler);

  void ConfigurePolymorphic(MapHandleList* maps,
                            List<Handle<Object>>* handlers);

  InlineCacheState StateFromFeedback() const override;
};

class LoadGlobalICNexus : public FeedbackNexus {
 public:
  LoadGlobalICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsLoadGlobalIC(slot));
  }
  LoadGlobalICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsLoadGlobalIC(slot));
  }

  int ExtractMaps(MapHandleList* maps) const final {
    // LoadGlobalICs don't record map feedback.
    return 0;
  }
  MaybeHandle<Object> FindHandlerForMap(Handle<Map> map) const final {
    return MaybeHandle<Code>();
  }
  bool FindHandlers(List<Handle<Object>>* code_list,
                    int length = -1) const final {
    return length == 0;
  }

  void ConfigureMegamorphic() override { UNREACHABLE(); }
  void Clear(Code* host);

  void ConfigureUninitialized() override;
  void ConfigurePropertyCellMode(Handle<PropertyCell> cell);
  void ConfigureHandlerMode(Handle<Object> handler);

  InlineCacheState StateFromFeedback() const override;
};

class KeyedLoadICNexus : public FeedbackNexus {
 public:
  KeyedLoadICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsKeyedLoadIC(slot));
  }
  KeyedLoadICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsKeyedLoadIC(slot));
  }

  void Clear(Code* host);

  // name can be a null handle for element loads.
  void ConfigureMonomorphic(Handle<Name> name, Handle<Map> receiver_map,
                            Handle<Object> handler);
  // name can be null.
  void ConfigurePolymorphic(Handle<Name> name, MapHandleList* maps,
                            List<Handle<Object>>* handlers);

  void ConfigureMegamorphicKeyed(IcCheckType property_type);

  IcCheckType GetKeyType() const;
  InlineCacheState StateFromFeedback() const override;
  Name* FindFirstName() const override;
};

class StoreICNexus : public FeedbackNexus {
 public:
  StoreICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsStoreIC(slot));
  }
  StoreICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsStoreIC(slot));
  }

  void Clear(Code* host);

  void ConfigureMonomorphic(Handle<Map> receiver_map, Handle<Object> handler);

  void ConfigurePolymorphic(MapHandleList* maps,
                            List<Handle<Object>>* handlers);

  InlineCacheState StateFromFeedback() const override;
};

class KeyedStoreICNexus : public FeedbackNexus {
 public:
  KeyedStoreICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsKeyedStoreIC(slot));
  }
  KeyedStoreICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK(vector->IsKeyedStoreIC(slot));
  }

  void Clear(Code* host);

  // name can be a null handle for element loads.
  void ConfigureMonomorphic(Handle<Name> name, Handle<Map> receiver_map,
                            Handle<Object> handler);
  // name can be null.
  void ConfigurePolymorphic(Handle<Name> name, MapHandleList* maps,
                            List<Handle<Object>>* handlers);
  void ConfigurePolymorphic(MapHandleList* maps,
                            MapHandleList* transitioned_maps,
                            List<Handle<Object>>* handlers);
  void ConfigureMegamorphicKeyed(IcCheckType property_type);

  KeyedAccessStoreMode GetKeyedAccessStoreMode() const;
  IcCheckType GetKeyType() const;

  InlineCacheState StateFromFeedback() const override;
  Name* FindFirstName() const override;
};

class BinaryOpICNexus final : public FeedbackNexus {
 public:
  BinaryOpICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK_EQ(FeedbackVectorSlotKind::INTERPRETER_BINARYOP_IC,
              vector->GetKind(slot));
  }
  BinaryOpICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK_EQ(FeedbackVectorSlotKind::INTERPRETER_BINARYOP_IC,
              vector->GetKind(slot));
  }

  void Clear(Code* host);

  InlineCacheState StateFromFeedback() const final;
  BinaryOperationHint GetBinaryOperationFeedback() const;

  int ExtractMaps(MapHandleList* maps) const final {
    // BinaryOpICs don't record map feedback.
    return 0;
  }
  MaybeHandle<Object> FindHandlerForMap(Handle<Map> map) const final {
    return MaybeHandle<Code>();
  }
  bool FindHandlers(List<Handle<Object>>* code_list,
                    int length = -1) const final {
    return length == 0;
  }
};

class CompareICNexus final : public FeedbackNexus {
 public:
  CompareICNexus(Handle<FeedbackVector> vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK_EQ(FeedbackVectorSlotKind::INTERPRETER_COMPARE_IC,
              vector->GetKind(slot));
  }
  CompareICNexus(FeedbackVector* vector, FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK_EQ(FeedbackVectorSlotKind::INTERPRETER_COMPARE_IC,
              vector->GetKind(slot));
  }

  void Clear(Code* host);

  InlineCacheState StateFromFeedback() const final;
  CompareOperationHint GetCompareOperationFeedback() const;

  int ExtractMaps(MapHandleList* maps) const final {
    // BinaryOpICs don't record map feedback.
    return 0;
  }
  MaybeHandle<Object> FindHandlerForMap(Handle<Map> map) const final {
    return MaybeHandle<Code>();
  }
  bool FindHandlers(List<Handle<Object>>* code_list,
                    int length = -1) const final {
    return length == 0;
  }
};

class StoreDataPropertyInLiteralICNexus : public FeedbackNexus {
 public:
  StoreDataPropertyInLiteralICNexus(Handle<FeedbackVector> vector,
                                    FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK_EQ(FeedbackVectorSlotKind::STORE_DATA_PROPERTY_IN_LITERAL_IC,
              vector->GetKind(slot));
  }
  StoreDataPropertyInLiteralICNexus(FeedbackVector* vector,
                                    FeedbackVectorSlot slot)
      : FeedbackNexus(vector, slot) {
    DCHECK_EQ(FeedbackVectorSlotKind::STORE_DATA_PROPERTY_IN_LITERAL_IC,
              vector->GetKind(slot));
  }

  void Clear(Code* host) { ConfigureUninitialized(); }

  void ConfigureMonomorphic(Handle<Name> name, Handle<Map> receiver_map);

  InlineCacheState StateFromFeedback() const override;
};

inline BinaryOperationHint BinaryOperationHintFromFeedback(int type_feedback);
inline CompareOperationHint CompareOperationHintFromFeedback(int type_feedback);

}  // namespace internal
}  // namespace v8

#endif  // V8_FEEDBACK_VECTOR_H_
