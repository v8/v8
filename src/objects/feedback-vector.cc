// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/feedback-vector.h"
#include "src/diagnostics/code-tracer.h"
#include "src/heap/off-thread-factory-inl.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/ic/ic-inl.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/feedback-vector-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/map-inl.h"
#include "src/objects/object-macros.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

FeedbackSlot FeedbackVectorSpec::AddSlot(FeedbackSlotKind kind) {
  int slot = slots();
  int entries_per_slot = FeedbackMetadata::GetSlotSize(kind);
  append(kind);
  for (int i = 1; i < entries_per_slot; i++) {
    append(FeedbackSlotKind::kInvalid);
  }
  return FeedbackSlot(slot);
}

FeedbackSlot FeedbackVectorSpec::AddTypeProfileSlot() {
  FeedbackSlot slot = AddSlot(FeedbackSlotKind::kTypeProfile);
  CHECK_EQ(FeedbackVectorSpec::kTypeProfileSlotIndex,
           FeedbackVector::GetIndex(slot));
  return slot;
}

bool FeedbackVectorSpec::HasTypeProfileSlot() const {
  FeedbackSlot slot =
      FeedbackVector::ToSlot(FeedbackVectorSpec::kTypeProfileSlotIndex);
  if (slots() <= slot.ToInt()) {
    return false;
  }
  return GetKind(slot) == FeedbackSlotKind::kTypeProfile;
}

static bool IsPropertyNameFeedback(MaybeObject feedback) {
  HeapObject heap_object;
  if (!feedback->GetHeapObjectIfStrong(&heap_object)) return false;
  if (heap_object.IsString()) {
    DCHECK(heap_object.IsInternalizedString());
    return true;
  }
  if (!heap_object.IsSymbol()) return false;
  Symbol symbol = Symbol::cast(heap_object);
  ReadOnlyRoots roots = symbol.GetReadOnlyRoots();
  return symbol != roots.uninitialized_symbol() &&
         symbol != roots.megamorphic_symbol();
}

std::ostream& operator<<(std::ostream& os, FeedbackSlotKind kind) {
  return os << FeedbackMetadata::Kind2String(kind);
}

FeedbackSlotKind FeedbackMetadata::GetKind(FeedbackSlot slot) const {
  int index = VectorICComputer::index(0, slot.ToInt());
  int data = get(index);
  return VectorICComputer::decode(data, slot.ToInt());
}

void FeedbackMetadata::SetKind(FeedbackSlot slot, FeedbackSlotKind kind) {
  int index = VectorICComputer::index(0, slot.ToInt());
  int data = get(index);
  int new_data = VectorICComputer::encode(data, slot.ToInt(), kind);
  set(index, new_data);
}

// static
template <typename LocalIsolate>
Handle<FeedbackMetadata> FeedbackMetadata::New(LocalIsolate* isolate,
                                               const FeedbackVectorSpec* spec) {
  auto* factory = isolate->factory();

  const int slot_count = spec == nullptr ? 0 : spec->slots();
  const int closure_feedback_cell_count =
      spec == nullptr ? 0 : spec->closure_feedback_cells();
  if (slot_count == 0 && closure_feedback_cell_count == 0) {
    return factory->empty_feedback_metadata();
  }
#ifdef DEBUG
  for (int i = 0; i < slot_count;) {
    DCHECK(spec);
    FeedbackSlotKind kind = spec->GetKind(FeedbackSlot(i));
    int entry_size = FeedbackMetadata::GetSlotSize(kind);
    for (int j = 1; j < entry_size; j++) {
      FeedbackSlotKind kind = spec->GetKind(FeedbackSlot(i + j));
      DCHECK_EQ(FeedbackSlotKind::kInvalid, kind);
    }
    i += entry_size;
  }
#endif

  Handle<FeedbackMetadata> metadata =
      factory->NewFeedbackMetadata(slot_count, closure_feedback_cell_count);

  // Initialize the slots. The raw data section has already been pre-zeroed in
  // NewFeedbackMetadata.
  for (int i = 0; i < slot_count; i++) {
    DCHECK(spec);
    FeedbackSlot slot(i);
    FeedbackSlotKind kind = spec->GetKind(slot);
    metadata->SetKind(slot, kind);
  }

  return metadata;
}

template Handle<FeedbackMetadata> FeedbackMetadata::New(
    Isolate* isolate, const FeedbackVectorSpec* spec);
template Handle<FeedbackMetadata> FeedbackMetadata::New(
    OffThreadIsolate* isolate, const FeedbackVectorSpec* spec);

bool FeedbackMetadata::SpecDiffersFrom(
    const FeedbackVectorSpec* other_spec) const {
  if (other_spec->slots() != slot_count()) {
    return true;
  }

  int slots = slot_count();
  for (int i = 0; i < slots;) {
    FeedbackSlot slot(i);
    FeedbackSlotKind kind = GetKind(slot);
    int entry_size = FeedbackMetadata::GetSlotSize(kind);

    if (kind != other_spec->GetKind(slot)) {
      return true;
    }
    i += entry_size;
  }
  return false;
}

const char* FeedbackMetadata::Kind2String(FeedbackSlotKind kind) {
  switch (kind) {
    case FeedbackSlotKind::kInvalid:
      return "Invalid";
    case FeedbackSlotKind::kCall:
      return "Call";
    case FeedbackSlotKind::kLoadProperty:
      return "LoadProperty";
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
      return "LoadGlobalInsideTypeof";
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
      return "LoadGlobalNotInsideTypeof";
    case FeedbackSlotKind::kLoadKeyed:
      return "LoadKeyed";
    case FeedbackSlotKind::kHasKeyed:
      return "HasKeyed";
    case FeedbackSlotKind::kStoreNamedSloppy:
      return "StoreNamedSloppy";
    case FeedbackSlotKind::kStoreNamedStrict:
      return "StoreNamedStrict";
    case FeedbackSlotKind::kStoreOwnNamed:
      return "StoreOwnNamed";
    case FeedbackSlotKind::kStoreGlobalSloppy:
      return "StoreGlobalSloppy";
    case FeedbackSlotKind::kStoreGlobalStrict:
      return "StoreGlobalStrict";
    case FeedbackSlotKind::kStoreKeyedSloppy:
      return "StoreKeyedSloppy";
    case FeedbackSlotKind::kStoreKeyedStrict:
      return "StoreKeyedStrict";
    case FeedbackSlotKind::kStoreInArrayLiteral:
      return "StoreInArrayLiteral";
    case FeedbackSlotKind::kBinaryOp:
      return "BinaryOp";
    case FeedbackSlotKind::kCompareOp:
      return "CompareOp";
    case FeedbackSlotKind::kStoreDataPropertyInLiteral:
      return "StoreDataPropertyInLiteral";
    case FeedbackSlotKind::kLiteral:
      return "Literal";
    case FeedbackSlotKind::kTypeProfile:
      return "TypeProfile";
    case FeedbackSlotKind::kForIn:
      return "ForIn";
    case FeedbackSlotKind::kInstanceOf:
      return "InstanceOf";
    case FeedbackSlotKind::kCloneObject:
      return "CloneObject";
    case FeedbackSlotKind::kKindsNumber:
      break;
  }
  UNREACHABLE();
}

bool FeedbackMetadata::HasTypeProfileSlot() const {
  FeedbackSlot slot =
      FeedbackVector::ToSlot(FeedbackVectorSpec::kTypeProfileSlotIndex);
  return slot.ToInt() < slot_count() &&
         GetKind(slot) == FeedbackSlotKind::kTypeProfile;
}

FeedbackSlotKind FeedbackVector::GetKind(FeedbackSlot slot) const {
  DCHECK(!is_empty());
  return metadata().GetKind(slot);
}

FeedbackSlot FeedbackVector::GetTypeProfileSlot() const {
  DCHECK(metadata().HasTypeProfileSlot());
  FeedbackSlot slot =
      FeedbackVector::ToSlot(FeedbackVectorSpec::kTypeProfileSlotIndex);
  DCHECK_EQ(FeedbackSlotKind::kTypeProfile, GetKind(slot));
  return slot;
}

// static
Handle<ClosureFeedbackCellArray> ClosureFeedbackCellArray::New(
    Isolate* isolate, Handle<SharedFunctionInfo> shared) {
  Factory* factory = isolate->factory();

  int num_feedback_cells =
      shared->feedback_metadata().closure_feedback_cell_count();

  Handle<ClosureFeedbackCellArray> feedback_cell_array =
      factory->NewClosureFeedbackCellArray(num_feedback_cells);

  for (int i = 0; i < num_feedback_cells; i++) {
    Handle<FeedbackCell> cell =
        factory->NewNoClosuresCell(factory->undefined_value());
    feedback_cell_array->set(i, *cell);
  }
  return feedback_cell_array;
}

// static
Handle<FeedbackVector> FeedbackVector::New(
    Isolate* isolate, Handle<SharedFunctionInfo> shared,
    Handle<ClosureFeedbackCellArray> closure_feedback_cell_array,
    IsCompiledScope* is_compiled_scope) {
  DCHECK(is_compiled_scope->is_compiled());
  Factory* factory = isolate->factory();

  Handle<FeedbackMetadata> feedback_metadata(shared->feedback_metadata(),
                                             isolate);
  const int slot_count = feedback_metadata->slot_count();

  Handle<FeedbackVector> vector =
      factory->NewFeedbackVector(shared, closure_feedback_cell_array);

  DCHECK_EQ(vector->length(), slot_count);

  DCHECK_EQ(vector->shared_function_info(), *shared);
  DCHECK_EQ(
      vector->optimized_code_weak_or_smi(),
      MaybeObject::FromSmi(Smi::FromEnum(
          FLAG_log_function_events ? OptimizationMarker::kLogFirstExecution
                                   : OptimizationMarker::kNone)));
  DCHECK_EQ(vector->invocation_count(), 0);
  DCHECK_EQ(vector->profiler_ticks(), 0);

  // Ensure we can skip the write barrier
  Handle<Object> uninitialized_sentinel = UninitializedSentinel(isolate);
  DCHECK_EQ(ReadOnlyRoots(isolate).uninitialized_symbol(),
            *uninitialized_sentinel);
  for (int i = 0; i < slot_count;) {
    FeedbackSlot slot(i);
    FeedbackSlotKind kind = feedback_metadata->GetKind(slot);
    int index = FeedbackVector::GetIndex(slot);
    int entry_size = FeedbackMetadata::GetSlotSize(kind);

    Object extra_value = *uninitialized_sentinel;
    switch (kind) {
      case FeedbackSlotKind::kLoadGlobalInsideTypeof:
      case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
      case FeedbackSlotKind::kStoreGlobalSloppy:
      case FeedbackSlotKind::kStoreGlobalStrict:
        vector->set(index, HeapObjectReference::ClearedValue(isolate),
                    SKIP_WRITE_BARRIER);
        break;
      case FeedbackSlotKind::kForIn:
      case FeedbackSlotKind::kCompareOp:
      case FeedbackSlotKind::kBinaryOp:
        vector->set(index, Smi::zero(), SKIP_WRITE_BARRIER);
        break;
      case FeedbackSlotKind::kLiteral:
        vector->set(index, Smi::zero(), SKIP_WRITE_BARRIER);
        break;
      case FeedbackSlotKind::kCall:
        vector->set(index, *uninitialized_sentinel, SKIP_WRITE_BARRIER);
        extra_value = Smi::zero();
        break;
      case FeedbackSlotKind::kCloneObject:
      case FeedbackSlotKind::kLoadProperty:
      case FeedbackSlotKind::kLoadKeyed:
      case FeedbackSlotKind::kHasKeyed:
      case FeedbackSlotKind::kStoreNamedSloppy:
      case FeedbackSlotKind::kStoreNamedStrict:
      case FeedbackSlotKind::kStoreOwnNamed:
      case FeedbackSlotKind::kStoreKeyedSloppy:
      case FeedbackSlotKind::kStoreKeyedStrict:
      case FeedbackSlotKind::kStoreInArrayLiteral:
      case FeedbackSlotKind::kStoreDataPropertyInLiteral:
      case FeedbackSlotKind::kTypeProfile:
      case FeedbackSlotKind::kInstanceOf:
        vector->set(index, *uninitialized_sentinel, SKIP_WRITE_BARRIER);
        break;

      case FeedbackSlotKind::kInvalid:
      case FeedbackSlotKind::kKindsNumber:
        UNREACHABLE();
        break;
    }
    for (int j = 1; j < entry_size; j++) {
      vector->set(index + j, extra_value, SKIP_WRITE_BARRIER);
    }
    i += entry_size;
  }

  Handle<FeedbackVector> result = Handle<FeedbackVector>::cast(vector);
  if (!isolate->is_best_effort_code_coverage() ||
      isolate->is_collecting_type_profile()) {
    AddToVectorsForProfilingTools(isolate, result);
  }
  return result;
}

namespace {

Handle<FeedbackVector> NewFeedbackVectorForTesting(
    Isolate* isolate, const FeedbackVectorSpec* spec) {
  Handle<FeedbackMetadata> metadata = FeedbackMetadata::New(isolate, spec);
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfoForBuiltin(
          isolate->factory()->empty_string(), Builtins::kIllegal);
  // Set the raw feedback metadata to circumvent checks that we are not
  // overwriting existing metadata.
  shared->set_raw_outer_scope_info_or_feedback_metadata(*metadata);
  Handle<ClosureFeedbackCellArray> closure_feedback_cell_array =
      ClosureFeedbackCellArray::New(isolate, shared);

  IsCompiledScope is_compiled_scope(shared->is_compiled_scope(isolate));
  return FeedbackVector::New(isolate, shared, closure_feedback_cell_array,
                             &is_compiled_scope);
}

}  // namespace

// static
Handle<FeedbackVector> FeedbackVector::NewWithOneBinarySlotForTesting(
    Zone* zone, Isolate* isolate) {
  FeedbackVectorSpec one_slot(zone);
  one_slot.AddBinaryOpICSlot();
  return NewFeedbackVectorForTesting(isolate, &one_slot);
}

// static
Handle<FeedbackVector> FeedbackVector::NewWithOneCompareSlotForTesting(
    Zone* zone, Isolate* isolate) {
  FeedbackVectorSpec one_slot(zone);
  one_slot.AddCompareICSlot();
  return NewFeedbackVectorForTesting(isolate, &one_slot);
}

// static
void FeedbackVector::AddToVectorsForProfilingTools(
    Isolate* isolate, Handle<FeedbackVector> vector) {
  DCHECK(!isolate->is_best_effort_code_coverage() ||
         isolate->is_collecting_type_profile());
  if (!vector->shared_function_info().IsSubjectToDebugging()) return;
  Handle<ArrayList> list = Handle<ArrayList>::cast(
      isolate->factory()->feedback_vectors_for_profiling_tools());
  list = ArrayList::Add(isolate, list, vector);
  isolate->SetFeedbackVectorsForProfilingTools(*list);
}

// static
void FeedbackVector::SetOptimizedCode(Handle<FeedbackVector> vector,
                                      Handle<Code> code) {
  DCHECK_EQ(code->kind(), Code::OPTIMIZED_FUNCTION);
  vector->set_optimized_code_weak_or_smi(HeapObjectReference::Weak(*code));
}

void FeedbackVector::ClearOptimizedCode() {
  DCHECK(has_optimized_code());
  SetOptimizationMarker(OptimizationMarker::kNone);
}

void FeedbackVector::ClearOptimizationMarker() {
  DCHECK(!has_optimized_code());
  SetOptimizationMarker(OptimizationMarker::kNone);
}

void FeedbackVector::SetOptimizationMarker(OptimizationMarker marker) {
  set_optimized_code_weak_or_smi(MaybeObject::FromSmi(Smi::FromEnum(marker)));
}

void FeedbackVector::EvictOptimizedCodeMarkedForDeoptimization(
    SharedFunctionInfo shared, const char* reason) {
  MaybeObject slot = optimized_code_weak_or_smi();
  if (slot->IsSmi()) {
    return;
  }

  if (slot->IsCleared()) {
    ClearOptimizationMarker();
    return;
  }

  Code code = Code::cast(slot->GetHeapObject());
  if (code.marked_for_deoptimization()) {
    if (FLAG_trace_deopt) {
      CodeTracer::Scope scope(GetIsolate()->GetCodeTracer());
      PrintF(scope.file(),
             "[evicting optimized code marked for deoptimization (%s) for ",
             reason);
      shared.ShortPrint(scope.file());
      PrintF(scope.file(), "]\n");
    }
    if (!code.deopt_already_counted()) {
      code.set_deopt_already_counted(true);
    }
    ClearOptimizedCode();
  }
}

bool FeedbackVector::ClearSlots(Isolate* isolate) {
  if (!shared_function_info().HasFeedbackMetadata()) return false;
  MaybeObject uninitialized_sentinel = MaybeObject::FromObject(
      FeedbackVector::RawUninitializedSentinel(isolate));

  bool feedback_updated = false;
  FeedbackMetadataIterator iter(metadata());
  while (iter.HasNext()) {
    FeedbackSlot slot = iter.Next();

    MaybeObject obj = Get(slot);
    if (obj != uninitialized_sentinel) {
      FeedbackNexusNoHandle nexus(MainThreadNoHandleConfig(*this, slot));
      feedback_updated |= nexus.Clear();
    }
  }
  return feedback_updated;
}

void FeedbackVector::AssertNoLegacyTypes(MaybeObject object) {
#ifdef DEBUG
  HeapObject heap_object;
  if (object->GetHeapObject(&heap_object)) {
    // Instead of FixedArray, the Feedback and the Extra should contain
    // WeakFixedArrays. The only allowed FixedArray subtype is HashTable.
    DCHECK_IMPLIES(heap_object.IsFixedArray(), heap_object.IsHashTable());
  }
#endif
}

MaybeObject MainThreadConfig::GetFeedback() const {
  return vector().Get(slot());
}

Handle<WeakFixedArray> MainThreadConfig::NewArray(int size) const {
  DCHECK(can_allocate());
  Handle<WeakFixedArray> array = isolate()->factory()->NewWeakFixedArray(size);
  return array;
}

MaybeObjectHandle MainThreadConfig::NewHandle(MaybeObject object) const {
  return handle(object, isolate());
}

template <typename J>
Handle<J> MainThreadConfig::NewHandle(J object) const {
  return handle(object, isolate());
}

void MainThreadConfig::SetFeedback(MaybeObject feedback,
                                   WriteBarrierMode mode) {
  vector().Set(slot(), feedback, mode);
}

std::pair<MaybeObject, MaybeObject> MainThreadConfig::GetFeedbackPair() const {
  MaybeObject feedback = vector().Get(slot());
  int extra_index = vector().GetIndex(slot()) + 1;
  MaybeObject feedback_extra = vector().get(extra_index);
  return std::pair<MaybeObject, MaybeObject>(feedback, feedback_extra);
}

void MainThreadConfig::SetFeedbackPair(MaybeObject feedback,
                                       WriteBarrierMode mode,
                                       MaybeObject feedback_extra,
                                       WriteBarrierMode mode_extra) {
  const int index = vector().GetIndex(slot());
  vector().set(index, feedback, mode);
  const int extra_index = index + 1;
  vector().set(extra_index, feedback_extra, mode_extra);
}

MaybeObjectHandle MainThreadNoHandleConfig::NewHandle(
    MaybeObject object) const {
  UNREACHABLE();
  return MaybeObjectHandle();
}

template <typename J>
Handle<J> MainThreadNoHandleConfig::NewHandle(J object) const {
  UNREACHABLE();
  return Handle<J>();
}

MaybeObject MainThreadNoHandleConfig::GetFeedback() const {
  return vector().Get(slot());
}

void MainThreadNoHandleConfig::SetFeedback(MaybeObject feedback,
                                           WriteBarrierMode mode) {
  vector().Set(slot(), feedback, mode);
}

std::pair<MaybeObject, MaybeObject> MainThreadNoHandleConfig::GetFeedbackPair()
    const {
  const int index = vector().GetIndex(slot());
  MaybeObject feedback = vector().get(index);
  const int extra_index = index + 1;
  MaybeObject feedback_extra = vector().get(extra_index);
  return std::pair<MaybeObject, MaybeObject>(feedback, feedback_extra);
}

void MainThreadNoHandleConfig::SetFeedbackPair(MaybeObject feedback,
                                               WriteBarrierMode mode,
                                               MaybeObject feedback_extra,
                                               WriteBarrierMode mode_extra) {
  const int index = vector().GetIndex(slot());
  vector().set(index, feedback, mode);
  const int extra_index = index + 1;
  vector().set(extra_index, feedback_extra, mode_extra);
}

MaybeObjectHandle BackgroundThreadConfig::NewHandle(MaybeObject object) const {
  return handle(object, local_heap());
}

template <typename J>
Handle<J> BackgroundThreadConfig::NewHandle(J object) const {
  return handle(object, local_heap());
}

MaybeObject BackgroundThreadConfig::GetFeedback() const {
  return vector().Get(slot());
}

void BackgroundThreadConfig::SetFeedback(MaybeObject feedback,
                                         WriteBarrierMode mode) {
  UNREACHABLE();
}

std::pair<MaybeObject, MaybeObject> BackgroundThreadConfig::GetFeedbackPair()
    const {
  // TODO(mvstanton): locking
  MaybeObject feedback = vector().Get(slot());
  int extra_index = vector().GetIndex(slot()) + 1;
  MaybeObject feedback_extra = vector().get(extra_index);
  return std::pair<MaybeObject, MaybeObject>(feedback, feedback_extra);
}

void BackgroundThreadConfig::SetFeedbackPair(MaybeObject feedback,
                                             WriteBarrierMode mode,
                                             MaybeObject feedback_extra,
                                             WriteBarrierMode mode_extra) {
  UNREACHABLE();
}

template <class T>
FeedbackNexusImpl<T>::FeedbackNexusImpl(T configuration) : g_(configuration) {
  kind_ = g_.vector().GetKind(g_.slot());
}

template <class T>
FeedbackNexusImpl<T>::FeedbackNexusImpl(Handle<FeedbackVector> vector,
                                        FeedbackSlot slot, Isolate* isolate)
    : g_(vector, slot, isolate) {
  if (!vector.is_null()) {
    kind_ = g_.vector().GetKind(g_.slot());
  } else {
    kind_ = FeedbackSlotKind::kInvalid;
  }
}

template <class T>
Handle<WeakFixedArray> FeedbackNexusImpl<T>::CreateArrayOfSize(int length) {
  DCHECK(g_.can_allocate());
  Handle<WeakFixedArray> array = g_.NewArray(length);
  return array;
}

template <class T>
void FeedbackNexusImpl<T>::ConfigureUninitialized() {
  Isolate* isolate = GetIsolate();
  switch (kind()) {
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof: {
      SetFeedback(
          HeapObjectReference::ClearedValue(isolate), SKIP_WRITE_BARRIER,
          *FeedbackVector::UninitializedSentinel(isolate), SKIP_WRITE_BARRIER);
      break;
    }
    case FeedbackSlotKind::kCloneObject:
    case FeedbackSlotKind::kCall: {
      SetFeedback(*FeedbackVector::UninitializedSentinel(isolate),
                  SKIP_WRITE_BARRIER, Smi::zero(), SKIP_WRITE_BARRIER);
      break;
    }
    case FeedbackSlotKind::kInstanceOf: {
      SetFeedback(*FeedbackVector::UninitializedSentinel(isolate),
                  SKIP_WRITE_BARRIER);
      break;
    }
    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kStoreDataPropertyInLiteral: {
      SetFeedback(
          *FeedbackVector::UninitializedSentinel(isolate), SKIP_WRITE_BARRIER,
          *FeedbackVector::UninitializedSentinel(isolate), SKIP_WRITE_BARRIER);
      break;
    }
    default:
      UNREACHABLE();
  }
}

template <class T>
bool FeedbackNexusImpl<T>::Clear() {
  bool feedback_updated = false;

  switch (kind()) {
    case FeedbackSlotKind::kTypeProfile:
      // We don't clear these kinds ever.
      break;

    case FeedbackSlotKind::kCompareOp:
    case FeedbackSlotKind::kForIn:
    case FeedbackSlotKind::kBinaryOp:
      // We don't clear these, either.
      break;

    case FeedbackSlotKind::kLiteral:
      SetFeedback(Smi::zero(), SKIP_WRITE_BARRIER);
      feedback_updated = true;
      break;

    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
    case FeedbackSlotKind::kCall:
    case FeedbackSlotKind::kInstanceOf:
    case FeedbackSlotKind::kStoreDataPropertyInLiteral:
    case FeedbackSlotKind::kCloneObject:
      if (!IsCleared()) {
        ConfigureUninitialized();
        feedback_updated = true;
      }
      break;

    case FeedbackSlotKind::kInvalid:
    case FeedbackSlotKind::kKindsNumber:
      UNREACHABLE();
  }
  return feedback_updated;
}

template <class T>
bool FeedbackNexusImpl<T>::ConfigureMegamorphic() {
  DisallowHeapAllocation no_gc;
  Isolate* isolate = GetIsolate();
  MaybeObject sentinel =
      MaybeObject::FromObject(*FeedbackVector::MegamorphicSentinel(isolate));
  if (GetFeedback() != sentinel) {
    SetFeedback(sentinel, SKIP_WRITE_BARRIER,
                HeapObjectReference::ClearedValue(isolate));
    return true;
  }

  return false;
}

template <class T>
bool FeedbackNexusImpl<T>::ConfigureMegamorphic(IcCheckType property_type) {
  DisallowHeapAllocation no_gc;
  Isolate* isolate = GetIsolate();
  bool changed = false;
  MaybeObject sentinel =
      MaybeObject::FromObject(*FeedbackVector::MegamorphicSentinel(isolate));
  if (GetFeedback() != sentinel) {
    SetFeedback(sentinel, SKIP_WRITE_BARRIER);
    changed = true;
  }

  Smi extra = Smi::FromInt(static_cast<int>(property_type));
  auto feedback = g_.GetFeedbackPair();
  changed = changed || feedback.second != MaybeObject::FromSmi(extra);
  if (changed) {
    SetFeedback(sentinel, SKIP_WRITE_BARRIER, extra, SKIP_WRITE_BARRIER);
  }
  return changed;
}

template <class T>
Map FeedbackNexusImpl<T>::GetFirstMap() const {
  MapHandles maps;
  ExtractMaps(&maps);
  if (!maps.empty()) return *maps.at(0);
  return Map();
}

template <class T>
InlineCacheState FeedbackNexusImpl<T>::ic_state() const {
  Isolate* isolate = GetIsolate();
  MaybeObject feedback, extra;
  if (FeedbackMetadata::GetSlotSize(kind()) == 2) {
    auto pair = g_.GetFeedbackPair();
    feedback = pair.first;
    extra = pair.second;
  } else {
    DCHECK_EQ(FeedbackMetadata::GetSlotSize(kind()), 1);
    feedback = g_.GetFeedback();
  }

  switch (kind()) {
    case FeedbackSlotKind::kLiteral:
      if (feedback->IsSmi()) return UNINITIALIZED;
      return MONOMORPHIC;

    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof: {
      if (feedback->IsSmi()) return MONOMORPHIC;

      DCHECK(feedback->IsWeakOrCleared());
      if (!feedback->IsCleared() ||
          extra != MaybeObject::FromObject(
                       *FeedbackVector::UninitializedSentinel(isolate))) {
        return MONOMORPHIC;
      }
      return UNINITIALIZED;
    }

    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      }
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::MegamorphicSentinel(isolate))) {
        return MEGAMORPHIC;
      }
      if (feedback->IsWeakOrCleared()) {
        // Don't check if the map is cleared.
        return MONOMORPHIC;
      }
      HeapObject heap_object;
      if (feedback->GetHeapObjectIfStrong(&heap_object)) {
        if (heap_object.IsWeakFixedArray()) {
          // Determine state purely by our structure, don't check if the maps
          // are cleared.
          return POLYMORPHIC;
        }
        if (heap_object.IsName()) {
          DCHECK(IsKeyedLoadICKind(kind()) || IsKeyedStoreICKind(kind()) ||
                 IsKeyedHasICKind(kind()));
          Object extra_object = extra->GetHeapObjectAssumeStrong();
          WeakFixedArray extra_array = WeakFixedArray::cast(extra_object);
          return extra_array.length() > 2 ? POLYMORPHIC : MONOMORPHIC;
        }
      }
      UNREACHABLE();
    }
    case FeedbackSlotKind::kCall: {
      HeapObject heap_object;
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::MegamorphicSentinel(isolate))) {
        return GENERIC;
      } else if (feedback->IsWeakOrCleared()) {
        if (feedback->GetHeapObjectIfWeak(&heap_object)) {
          if (heap_object.IsFeedbackCell()) {
            return POLYMORPHIC;
          }
          CHECK(heap_object.IsJSFunction() || heap_object.IsJSBoundFunction());
        }
        return MONOMORPHIC;
      } else if (feedback->GetHeapObjectIfStrong(&heap_object) &&
                 heap_object.IsAllocationSite()) {
        return MONOMORPHIC;
      }

      CHECK_EQ(feedback, MaybeObject::FromObject(
                             *FeedbackVector::UninitializedSentinel(isolate)));
      return UNINITIALIZED;
    }
    case FeedbackSlotKind::kBinaryOp: {
      BinaryOperationHint hint = GetBinaryOperationFeedback();
      if (hint == BinaryOperationHint::kNone) {
        return UNINITIALIZED;
      } else if (hint == BinaryOperationHint::kAny) {
        return GENERIC;
      }

      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kCompareOp: {
      CompareOperationHint hint = GetCompareOperationFeedback();
      if (hint == CompareOperationHint::kNone) {
        return UNINITIALIZED;
      } else if (hint == CompareOperationHint::kAny) {
        return GENERIC;
      }

      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kForIn: {
      ForInHint hint = GetForInFeedback();
      if (hint == ForInHint::kNone) {
        return UNINITIALIZED;
      } else if (hint == ForInHint::kAny) {
        return GENERIC;
      }
      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kInstanceOf: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      } else if (feedback ==
                 MaybeObject::FromObject(
                     *FeedbackVector::MegamorphicSentinel(isolate))) {
        return MEGAMORPHIC;
      }
      return MONOMORPHIC;
    }
    case FeedbackSlotKind::kStoreDataPropertyInLiteral: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      } else if (feedback->IsWeakOrCleared()) {
        // Don't check if the map is cleared.
        return MONOMORPHIC;
      }

      return MEGAMORPHIC;
    }
    case FeedbackSlotKind::kTypeProfile: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      }
      return MONOMORPHIC;
    }

    case FeedbackSlotKind::kCloneObject: {
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::UninitializedSentinel(isolate))) {
        return UNINITIALIZED;
      }
      if (feedback == MaybeObject::FromObject(
                          *FeedbackVector::MegamorphicSentinel(isolate))) {
        return MEGAMORPHIC;
      }
      if (feedback->IsWeakOrCleared()) {
        return MONOMORPHIC;
      }

      DCHECK(feedback->GetHeapObjectAssumeStrong().IsWeakFixedArray());
      return POLYMORPHIC;
    }

    case FeedbackSlotKind::kInvalid:
    case FeedbackSlotKind::kKindsNumber:
      UNREACHABLE();
  }
  return UNINITIALIZED;
}

template <class T>
void FeedbackNexusImpl<T>::ConfigurePropertyCellMode(
    Handle<PropertyCell> cell) {
  DCHECK(IsGlobalICKind(kind()));
  Isolate* isolate = GetIsolate();
  SetFeedback(HeapObjectReference::Weak(*cell), UPDATE_WRITE_BARRIER,
              *FeedbackVector::UninitializedSentinel(isolate),
              SKIP_WRITE_BARRIER);
}

template <class T>
bool FeedbackNexusImpl<T>::ConfigureLexicalVarMode(int script_context_index,
                                                   int context_slot_index,
                                                   bool immutable) {
  DCHECK(IsGlobalICKind(kind()));
  DCHECK_LE(0, script_context_index);
  DCHECK_LE(0, context_slot_index);
  if (!ContextIndexBits::is_valid(script_context_index) ||
      !SlotIndexBits::is_valid(context_slot_index) ||
      !ImmutabilityBit::is_valid(immutable)) {
    return false;
  }
  int config = ContextIndexBits::encode(script_context_index) |
               SlotIndexBits::encode(context_slot_index) |
               ImmutabilityBit::encode(immutable);

  Isolate* isolate = GetIsolate();
  SetFeedback(Smi::From31BitPattern(config), SKIP_WRITE_BARRIER,
              *FeedbackVector::UninitializedSentinel(isolate),
              SKIP_WRITE_BARRIER);
  return true;
}

template <class T>
void FeedbackNexusImpl<T>::ConfigureHandlerMode(
    const MaybeObjectHandle& handler) {
  DCHECK(IsGlobalICKind(kind()));
  DCHECK(IC::IsHandler(*handler));
  SetFeedback(HeapObjectReference::ClearedValue(GetIsolate()),
              UPDATE_WRITE_BARRIER, *handler, UPDATE_WRITE_BARRIER);
}

template <class T>
void FeedbackNexusImpl<T>::ConfigureCloneObject(Handle<Map> source_map,
                                                Handle<Map> result_map) {
  Isolate* isolate = GetIsolate();
  Handle<HeapObject> feedback;
  {
    MaybeObject maybe_feedback = GetFeedback();
    if (maybe_feedback->IsStrongOrWeak()) {
      feedback = handle(maybe_feedback->GetHeapObject(), isolate);
    } else {
      DCHECK(maybe_feedback->IsCleared());
    }
  }
  switch (ic_state()) {
    case UNINITIALIZED:
      // Cache the first map seen which meets the fast case requirements.
      SetFeedback(HeapObjectReference::Weak(*source_map), UPDATE_WRITE_BARRIER,
                  *result_map);
      break;
    case MONOMORPHIC:
      if (feedback.is_null() || feedback.is_identical_to(source_map) ||
          Map::cast(*feedback).is_deprecated()) {
        SetFeedback(HeapObjectReference::Weak(*source_map),
                    UPDATE_WRITE_BARRIER, *result_map);
      } else {
        // Transition to POLYMORPHIC.
        Handle<WeakFixedArray> array =
            CreateArrayOfSize(2 * kCloneObjectPolymorphicEntrySize);
        array->Set(0, HeapObjectReference::Weak(*feedback));
        array->Set(1, g_.GetFeedbackPair().second);
        array->Set(2, HeapObjectReference::Weak(*source_map));
        array->Set(3, MaybeObject::FromObject(*result_map));
        SetFeedback(*array, UPDATE_WRITE_BARRIER,
                    HeapObjectReference::ClearedValue(isolate));
      }
      break;
    case POLYMORPHIC: {
      const int kMaxElements =
          FLAG_max_polymorphic_map_count * kCloneObjectPolymorphicEntrySize;
      Handle<WeakFixedArray> array = Handle<WeakFixedArray>::cast(feedback);
      int i = 0;
      for (; i < array->length(); i += kCloneObjectPolymorphicEntrySize) {
        MaybeObject feedback_map = array->Get(i);
        if (feedback_map->IsCleared()) break;
        Handle<Map> cached_map(Map::cast(feedback_map->GetHeapObject()),
                               isolate);
        if (cached_map.is_identical_to(source_map) ||
            cached_map->is_deprecated())
          break;
      }

      if (i >= array->length()) {
        if (i == kMaxElements) {
          // Transition to MEGAMORPHIC.
          MaybeObject sentinel = MaybeObject::FromObject(
              *FeedbackVector::MegamorphicSentinel(isolate));
          SetFeedback(sentinel, SKIP_WRITE_BARRIER,
                      HeapObjectReference::ClearedValue(isolate));
          break;
        }

        // Grow polymorphic feedback array.
        Handle<WeakFixedArray> new_array = CreateArrayOfSize(
            array->length() + kCloneObjectPolymorphicEntrySize);
        for (int j = 0; j < array->length(); ++j) {
          new_array->Set(j, array->Get(j));
        }
        SetFeedback(*new_array);
        array = new_array;
      }

      array->Set(i, HeapObjectReference::Weak(*source_map));
      array->Set(i + 1, MaybeObject::FromObject(*result_map));
      break;
    }

    default:
      UNREACHABLE();
  }
}

template <class T>
int FeedbackNexusImpl<T>::GetCallCount() {
  DCHECK(IsCallICKind(kind()));

  Object call_count = g_.GetFeedbackPair().second->template cast<Object>();
  CHECK(call_count.IsSmi());
  uint32_t value = static_cast<uint32_t>(Smi::ToInt(call_count));
  return CallCountField::decode(value);
}

template <class T>
void FeedbackNexusImpl<T>::SetSpeculationMode(SpeculationMode mode) {
  DCHECK(IsCallICKind(kind()));

  Object call_count = g_.GetFeedbackPair().second->template cast<Object>();
  CHECK(call_count.IsSmi());
  uint32_t count = static_cast<uint32_t>(Smi::ToInt(call_count));
  uint32_t value = CallCountField::encode(CallCountField::decode(count));
  int result = static_cast<int>(value | SpeculationModeField::encode(mode));
  MaybeObject feedback = GetFeedback();
  // We can skip the write barrier for {feedback} because it's not changing.
  SetFeedback(feedback, SKIP_WRITE_BARRIER, Smi::FromInt(result),
              SKIP_WRITE_BARRIER);
}

template <class T>
SpeculationMode FeedbackNexusImpl<T>::GetSpeculationMode() {
  DCHECK(IsCallICKind(kind()));

  Object call_count = g_.GetFeedbackPair().second->template cast<Object>();
  CHECK(call_count.IsSmi());
  uint32_t value = static_cast<uint32_t>(Smi::ToInt(call_count));
  return SpeculationModeField::decode(value);
}

template <class T>
float FeedbackNexusImpl<T>::ComputeCallFrequency() {
  DCHECK(IsCallICKind(kind()));

  double const invocation_count = vector().invocation_count();
  double const call_count = GetCallCount();
  if (invocation_count == 0.0) {  // Prevent division by 0.
    return 0.0f;
  }
  return static_cast<float>(call_count / invocation_count);
}

template <class T>
void FeedbackNexusImpl<T>::ConfigureMonomorphic(
    Handle<Name> name, Handle<Map> receiver_map,
    const MaybeObjectHandle& handler) {
  DCHECK(handler.is_null() || IC::IsHandler(*handler));
  if (kind() == FeedbackSlotKind::kStoreDataPropertyInLiteral) {
    SetFeedback(HeapObjectReference::Weak(*receiver_map), UPDATE_WRITE_BARRIER,
                *name);
  } else {
    if (name.is_null()) {
      SetFeedback(HeapObjectReference::Weak(*receiver_map),
                  UPDATE_WRITE_BARRIER, *handler);
    } else {
      Handle<WeakFixedArray> array = CreateArrayOfSize(2);
      array->Set(0, HeapObjectReference::Weak(*receiver_map));
      array->Set(1, *handler);
      SetFeedback(*name, UPDATE_WRITE_BARRIER, *array);
    }
  }
}

template <class T>
void FeedbackNexusImpl<T>::ConfigurePolymorphic(
    Handle<Name> name, std::vector<MapAndHandler> const& maps_and_handlers) {
  int receiver_count = static_cast<int>(maps_and_handlers.size());
  DCHECK_GT(receiver_count, 1);
  Handle<WeakFixedArray> array = CreateArrayOfSize(receiver_count * 2);

  for (int current = 0; current < receiver_count; ++current) {
    Handle<Map> map = maps_and_handlers[current].first;
    array->Set(current * 2, HeapObjectReference::Weak(*map));
    MaybeObjectHandle handler = maps_and_handlers[current].second;
    DCHECK(IC::IsHandler(*handler));
    array->Set(current * 2 + 1, *handler);
  }

  // Use a release store to flush all writes to the array. It will be
  // examined on the background compilation thread.
  array->synchronized_set_length(array->length());

  if (name.is_null()) {
    SetFeedback(*array, UPDATE_WRITE_BARRIER,
                *FeedbackVector::UninitializedSentinel(GetIsolate()),
                SKIP_WRITE_BARRIER);
  } else {
    SetFeedback(*name, UPDATE_WRITE_BARRIER, *array);
  }
}

template <class T>
int FeedbackNexusImpl<T>::ExtractMaps(MapHandles* maps) const {
  DCHECK(IsLoadICKind(kind()) || IsStoreICKind(kind()) ||
         IsKeyedLoadICKind(kind()) || IsKeyedStoreICKind(kind()) ||
         IsStoreOwnICKind(kind()) || IsStoreDataPropertyInLiteralKind(kind()) ||
         IsStoreInArrayLiteralICKind(kind()) || IsKeyedHasICKind(kind()));

  DisallowHeapAllocation no_gc;
  MaybeObject feedback = GetFeedback();
  bool is_named_feedback = IsPropertyNameFeedback(feedback);
  HeapObject heap_object;
  if ((feedback->GetHeapObjectIfStrong(&heap_object) &&
       heap_object.IsWeakFixedArray()) ||
      is_named_feedback) {
    int found = 0;
    WeakFixedArray array;
    if (is_named_feedback) {
      array = WeakFixedArray::cast(
          g_.GetFeedbackPair().second->GetHeapObjectAssumeStrong());
    } else {
      array = WeakFixedArray::cast(heap_object);
    }
    const int increment = 2;
    HeapObject heap_object;
    for (int i = 0; i < array.length(); i += increment) {
      DCHECK(array.Get(i)->IsWeakOrCleared());
      if (array.Get(i)->GetHeapObjectIfWeak(&heap_object)) {
        Map map = Map::cast(heap_object);
        maps->push_back(g_.NewHandle(map));
        found++;
      }
    }
    return found;
  } else if (feedback->GetHeapObjectIfWeak(&heap_object)) {
    Map map = Map::cast(heap_object);
    maps->push_back(g_.NewHandle(map));
    return 1;
  }

  return 0;
}

template <class T>
int FeedbackNexusImpl<T>::ExtractMapsAndHandlers(
    std::vector<std::pair<Handle<Map>, MaybeObjectHandle>>* maps_and_handlers)
    const {
  return ExtractMapsAndHandlers(maps_and_handlers,
                                [](Handle<Map> map) { return map; });
}

template <class T>
int FeedbackNexusImpl<T>::ExtractMapsAndHandlers(
    std::vector<std::pair<Handle<Map>, MaybeObjectHandle>>* maps_and_handlers,
    const TryUpdateHandler& map_handler) const {
  DCHECK(IsLoadICKind(kind()) ||
         IsStoreICKind(kind()) | IsKeyedLoadICKind(kind()) ||
         IsKeyedStoreICKind(kind()) || IsStoreOwnICKind(kind()) ||
         IsStoreDataPropertyInLiteralKind(kind()) ||
         IsStoreInArrayLiteralICKind(kind()) || IsKeyedHasICKind(kind()));

  DisallowHeapAllocation no_gc;
  auto pair = g_.GetFeedbackPair();
  MaybeObject feedback = pair.first;
  bool is_named_feedback = IsPropertyNameFeedback(feedback);
  HeapObject heap_object;
  if ((feedback->GetHeapObjectIfStrong(&heap_object) &&
       heap_object.IsWeakFixedArray()) ||
      is_named_feedback) {
    int found = 0;
    WeakFixedArray array;
    if (is_named_feedback) {
      array = WeakFixedArray::cast(pair.second->GetHeapObjectAssumeStrong());
    } else {
      array = WeakFixedArray::cast(heap_object);
    }
    const int increment = 2;
    HeapObject heap_object;
    maps_and_handlers->reserve(array.length() / increment);
    for (int i = 0; i < array.length(); i += increment) {
      DCHECK(array.Get(i)->IsWeakOrCleared());
      if (array.Get(i)->GetHeapObjectIfWeak(&heap_object)) {
        MaybeObject handler = array.Get(i + 1);
        if (!handler->IsCleared()) {
          DCHECK(IC::IsHandler(handler));
          Handle<Map> map(g_.NewHandle(Map::cast(heap_object)));
          if (!map_handler(map).ToHandle(&map)) {
            continue;
          }
          maps_and_handlers->push_back(
              MapAndHandler(map, g_.NewHandle(handler)));
          found++;
        }
      }
    }
    return found;
  } else if (feedback->GetHeapObjectIfWeak(&heap_object)) {
    MaybeObject handler = pair.second;
    if (!handler->IsCleared()) {
      DCHECK(IC::IsHandler(handler));
      Handle<Map> map = g_.NewHandle(Map::cast(heap_object));
      if (!map_handler(map).ToHandle(&map)) {
        return 0;
      }
      maps_and_handlers->push_back(MapAndHandler(map, g_.NewHandle(handler)));
      return 1;
    }
  }

  return 0;
}

template <class T>
MaybeObjectHandle FeedbackNexusImpl<T>::FindHandlerForMap(
    Handle<Map> map) const {
  DCHECK(IsLoadICKind(kind()) || IsStoreICKind(kind()) ||
         IsKeyedLoadICKind(kind()) || IsKeyedStoreICKind(kind()) ||
         IsStoreOwnICKind(kind()) || IsStoreDataPropertyInLiteralKind(kind()) ||
         IsKeyedHasICKind(kind()));

  auto pair = g_.GetFeedbackPair();
  MaybeObject feedback = pair.first;
  bool is_named_feedback = IsPropertyNameFeedback(feedback);
  HeapObject heap_object;
  if ((feedback->GetHeapObjectIfStrong(&heap_object) &&
       heap_object.IsWeakFixedArray()) ||
      is_named_feedback) {
    WeakFixedArray array;
    if (is_named_feedback) {
      array = WeakFixedArray::cast(pair.second->GetHeapObjectAssumeStrong());
    } else {
      array = WeakFixedArray::cast(heap_object);
    }
    const int increment = 2;
    HeapObject heap_object;
    for (int i = 0; i < array.length(); i += increment) {
      DCHECK(array.Get(i)->IsWeakOrCleared());
      if (array.Get(i)->GetHeapObjectIfWeak(&heap_object)) {
        Map array_map = Map::cast(heap_object);
        if (array_map == *map && !array.Get(i + increment - 1)->IsCleared()) {
          MaybeObject handler = array.Get(i + increment - 1);
          DCHECK(IC::IsHandler(handler));
          return g_.NewHandle(handler);
        }
      }
    }
  } else if (feedback->GetHeapObjectIfWeak(&heap_object)) {
    Map cell_map = Map::cast(heap_object);
    if (cell_map == *map && !pair.second->IsCleared()) {
      MaybeObject handler = pair.second;
      DCHECK(IC::IsHandler(handler));
      return g_.NewHandle(handler);
    }
  }

  return MaybeObjectHandle();
}

template <class T>
Name FeedbackNexusImpl<T>::GetName() const {
  if (IsKeyedStoreICKind(kind()) || IsKeyedLoadICKind(kind()) ||
      IsKeyedHasICKind(kind())) {
    MaybeObject feedback = GetFeedback();
    if (IsPropertyNameFeedback(feedback)) {
      return Name::cast(feedback->GetHeapObjectAssumeStrong());
    }
  }
  if (IsStoreDataPropertyInLiteralKind(kind())) {
    MaybeObject extra = g_.GetFeedbackPair().second;
    if (IsPropertyNameFeedback(extra)) {
      return Name::cast(extra->GetHeapObjectAssumeStrong());
    }
  }
  return Name();
}

template <class T>
KeyedAccessLoadMode FeedbackNexusImpl<T>::GetKeyedAccessLoadMode() const {
  DCHECK(IsKeyedLoadICKind(kind()) || IsKeyedHasICKind(kind()));

  if (GetKeyType() == PROPERTY) return STANDARD_LOAD;

  std::vector<MapAndHandler> maps_and_handlers;
  ExtractMapsAndHandlers(&maps_and_handlers);
  for (MapAndHandler map_and_handler : maps_and_handlers) {
    KeyedAccessLoadMode mode =
        LoadHandler::GetKeyedAccessLoadMode(*map_and_handler.second);
    if (mode != STANDARD_LOAD) return mode;
  }

  return STANDARD_LOAD;
}

namespace {

bool BuiltinHasKeyedAccessStoreMode(int builtin_index) {
  DCHECK(Builtins::IsBuiltinId(builtin_index));
  switch (builtin_index) {
    case Builtins::kKeyedStoreIC_SloppyArguments_Standard:
    case Builtins::kKeyedStoreIC_SloppyArguments_GrowNoTransitionHandleCOW:
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionIgnoreOOB:
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_Standard:
    case Builtins::kStoreFastElementIC_GrowNoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_NoTransitionIgnoreOOB:
    case Builtins::kStoreFastElementIC_NoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_Standard:
    case Builtins::kElementsTransitionAndStore_GrowNoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_NoTransitionIgnoreOOB:
    case Builtins::kElementsTransitionAndStore_NoTransitionHandleCOW:
      return true;
    default:
      return false;
  }
  UNREACHABLE();
}

KeyedAccessStoreMode KeyedAccessStoreModeForBuiltin(int builtin_index) {
  DCHECK(BuiltinHasKeyedAccessStoreMode(builtin_index));
  switch (builtin_index) {
    case Builtins::kKeyedStoreIC_SloppyArguments_Standard:
    case Builtins::kStoreFastElementIC_Standard:
    case Builtins::kElementsTransitionAndStore_Standard:
      return STANDARD_STORE;
    case Builtins::kKeyedStoreIC_SloppyArguments_GrowNoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_GrowNoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_GrowNoTransitionHandleCOW:
      return STORE_AND_GROW_HANDLE_COW;
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionIgnoreOOB:
    case Builtins::kStoreFastElementIC_NoTransitionIgnoreOOB:
    case Builtins::kElementsTransitionAndStore_NoTransitionIgnoreOOB:
      return STORE_IGNORE_OUT_OF_BOUNDS;
    case Builtins::kKeyedStoreIC_SloppyArguments_NoTransitionHandleCOW:
    case Builtins::kStoreFastElementIC_NoTransitionHandleCOW:
    case Builtins::kElementsTransitionAndStore_NoTransitionHandleCOW:
      return STORE_HANDLE_COW;
    default:
      UNREACHABLE();
  }
}

}  // namespace

template <class T>
KeyedAccessStoreMode FeedbackNexusImpl<T>::GetKeyedAccessStoreMode() const {
  DCHECK(IsKeyedStoreICKind(kind()) || IsStoreInArrayLiteralICKind(kind()) ||
         IsStoreDataPropertyInLiteralKind(kind()));
  KeyedAccessStoreMode mode = STANDARD_STORE;

  if (GetKeyType() == PROPERTY) return mode;

  std::vector<MapAndHandler> maps_and_handlers;
  ExtractMapsAndHandlers(&maps_and_handlers);
  for (const MapAndHandler& map_and_handler : maps_and_handlers) {
    const MaybeObjectHandle maybe_code_handler = map_and_handler.second;
    // The first handler that isn't the slow handler will have the bits we need.
    Handle<Code> handler;
    if (maybe_code_handler.object()->IsStoreHandler()) {
      Handle<StoreHandler> data_handler =
          Handle<StoreHandler>::cast(maybe_code_handler.object());

      if ((data_handler->smi_handler()).IsSmi()) {
        // Decode the KeyedAccessStoreMode information from the Handler.
        mode = StoreHandler::GetKeyedAccessStoreMode(
            MaybeObject::FromObject(data_handler->smi_handler()));
        if (mode != STANDARD_STORE) return mode;
        continue;
      } else {
        handler = handle(Code::cast(data_handler->smi_handler()),
                         vector().GetIsolate());
      }

    } else if (maybe_code_handler.object()->IsSmi()) {
      // Skip for Proxy Handlers.
      if (*(maybe_code_handler.object()) ==
          *StoreHandler::StoreProxy(GetIsolate()))
        continue;
      // Decode the KeyedAccessStoreMode information from the Handler.
      mode = StoreHandler::GetKeyedAccessStoreMode(*maybe_code_handler);
      if (mode != STANDARD_STORE) return mode;
      continue;
    } else {
      // Element store without prototype chain check.
      handler = Handle<Code>::cast(maybe_code_handler.object());
    }

    if (handler->is_builtin()) {
      const int builtin_index = handler->builtin_index();
      if (!BuiltinHasKeyedAccessStoreMode(builtin_index)) continue;

      mode = KeyedAccessStoreModeForBuiltin(builtin_index);
      break;
    }
  }

  return mode;
}

template <class T>
IcCheckType FeedbackNexusImpl<T>::GetKeyType() const {
  DCHECK(IsKeyedStoreICKind(kind()) || IsKeyedLoadICKind(kind()) ||
         IsStoreInArrayLiteralICKind(kind()) || IsKeyedHasICKind(kind()) ||
         IsStoreDataPropertyInLiteralKind(kind()));
  auto pair = g_.GetFeedbackPair();
  MaybeObject feedback = pair.first;
  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::MegamorphicSentinel(GetIsolate()))) {
    return static_cast<IcCheckType>(
        Smi::ToInt(pair.second->template cast<Object>()));
  }
  MaybeObject maybe_name =
      IsStoreDataPropertyInLiteralKind(kind()) ? pair.second : feedback;
  return IsPropertyNameFeedback(maybe_name) ? PROPERTY : ELEMENT;
}

template <class T>
BinaryOperationHint FeedbackNexusImpl<T>::GetBinaryOperationFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kBinaryOp);
  int feedback = GetFeedback().ToSmi().value();
  return BinaryOperationHintFromFeedback(feedback);
}

template <class T>
CompareOperationHint FeedbackNexusImpl<T>::GetCompareOperationFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kCompareOp);
  int feedback = GetFeedback().ToSmi().value();
  return CompareOperationHintFromFeedback(feedback);
}

template <class T>
ForInHint FeedbackNexusImpl<T>::GetForInFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kForIn);
  int feedback = GetFeedback().ToSmi().value();
  return ForInHintFromFeedback(feedback);
}

template <class T>
MaybeHandle<JSObject> FeedbackNexusImpl<T>::GetConstructorFeedback() const {
  DCHECK_EQ(kind(), FeedbackSlotKind::kInstanceOf);
  MaybeObject feedback = GetFeedback();
  HeapObject heap_object;
  if (feedback->GetHeapObjectIfWeak(&heap_object)) {
    return g_.NewHandle(JSObject::cast(heap_object));
  }
  return MaybeHandle<JSObject>();
}

namespace {

bool InList(Handle<ArrayList> types, Handle<String> type) {
  for (int i = 0; i < types->Length(); i++) {
    Object obj = types->Get(i);
    if (String::cast(obj).Equals(*type)) {
      return true;
    }
  }
  return false;
}
}  // anonymous namespace

template <class T>
void FeedbackNexusImpl<T>::Collect(Handle<String> type, int position) {
  DCHECK(IsTypeProfileKind(kind()));
  DCHECK_GE(position, 0);
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();

  // Map source position to collection of types
  Handle<SimpleNumberDictionary> types;

  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    types = SimpleNumberDictionary::New(isolate, 1);
  } else {
    types = handle(
        SimpleNumberDictionary::cast(feedback->GetHeapObjectAssumeStrong()),
        isolate);
  }

  Handle<ArrayList> position_specific_types;

  InternalIndex entry = types->FindEntry(isolate, position);
  if (entry.is_not_found()) {
    position_specific_types = ArrayList::New(isolate, 1);
    types = SimpleNumberDictionary::Set(
        isolate, types, position,
        ArrayList::Add(isolate, position_specific_types, type));
  } else {
    DCHECK(types->ValueAt(entry).IsArrayList());
    position_specific_types =
        handle(ArrayList::cast(types->ValueAt(entry)), isolate);
    if (!InList(position_specific_types, type)) {  // Add type
      types = SimpleNumberDictionary::Set(
          isolate, types, position,
          ArrayList::Add(isolate, position_specific_types, type));
    }
  }
  SetFeedback(*types);
}

template <class T>
std::vector<int> FeedbackNexusImpl<T>::GetSourcePositions() const {
  DCHECK(IsTypeProfileKind(kind()));
  std::vector<int> source_positions;
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();

  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    return source_positions;
  }

  Handle<SimpleNumberDictionary> types(
      SimpleNumberDictionary::cast(feedback->GetHeapObjectAssumeStrong()),
      isolate);

  for (int index = SimpleNumberDictionary::kElementsStartIndex;
       index < types->length(); index += SimpleNumberDictionary::kEntrySize) {
    int key_index = index + SimpleNumberDictionary::kEntryKeyIndex;
    Object key = types->get(key_index);
    if (key.IsSmi()) {
      int position = Smi::cast(key).value();
      source_positions.push_back(position);
    }
  }
  return source_positions;
}

template <class T>
std::vector<Handle<String>> FeedbackNexusImpl<T>::GetTypesForSourcePositions(
    uint32_t position) const {
  DCHECK(IsTypeProfileKind(kind()));
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();
  std::vector<Handle<String>> types_for_position;
  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    return types_for_position;
  }

  Handle<SimpleNumberDictionary> types(
      SimpleNumberDictionary::cast(feedback->GetHeapObjectAssumeStrong()),
      isolate);

  InternalIndex entry = types->FindEntry(isolate, position);
  if (entry.is_not_found()) return types_for_position;

  DCHECK(types->ValueAt(entry).IsArrayList());
  Handle<ArrayList> position_specific_types =
      Handle<ArrayList>(ArrayList::cast(types->ValueAt(entry)), isolate);
  for (int i = 0; i < position_specific_types->Length(); i++) {
    Object t = position_specific_types->Get(i);
    types_for_position.push_back(Handle<String>(String::cast(t), isolate));
  }

  return types_for_position;
}

namespace {

Handle<JSObject> ConvertToJSObject(Isolate* isolate,
                                   Handle<SimpleNumberDictionary> feedback) {
  Handle<JSObject> type_profile =
      isolate->factory()->NewJSObject(isolate->object_function());

  for (int index = SimpleNumberDictionary::kElementsStartIndex;
       index < feedback->length();
       index += SimpleNumberDictionary::kEntrySize) {
    int key_index = index + SimpleNumberDictionary::kEntryKeyIndex;
    Object key = feedback->get(key_index);
    if (key.IsSmi()) {
      int value_index = index + SimpleNumberDictionary::kEntryValueIndex;

      Handle<ArrayList> position_specific_types(
          ArrayList::cast(feedback->get(value_index)), isolate);

      int position = Smi::ToInt(key);
      JSObject::AddDataElement(
          type_profile, position,
          isolate->factory()->NewJSArrayWithElements(
              ArrayList::Elements(isolate, position_specific_types)),
          PropertyAttributes::NONE);
    }
  }
  return type_profile;
}
}  // namespace

template <class T>
JSObject FeedbackNexusImpl<T>::GetTypeProfile() const {
  DCHECK(IsTypeProfileKind(kind()));
  Isolate* isolate = GetIsolate();

  MaybeObject const feedback = GetFeedback();

  if (feedback == MaybeObject::FromObject(
                      *FeedbackVector::UninitializedSentinel(isolate))) {
    return *isolate->factory()->NewJSObject(isolate->object_function());
  }

  return *ConvertToJSObject(isolate,
                            handle(SimpleNumberDictionary::cast(
                                       feedback->GetHeapObjectAssumeStrong()),
                                   isolate));
}

template <class T>
void FeedbackNexusImpl<T>::ResetTypeProfile() {
  DCHECK(IsTypeProfileKind(kind()));
  SetFeedback(*FeedbackVector::UninitializedSentinel(GetIsolate()));
}

template class FeedbackNexusImpl<MainThreadConfig>;
template class FeedbackNexusImpl<MainThreadNoHandleConfig>;
template class FeedbackNexusImpl<BackgroundThreadConfig>;
}  // namespace internal
}  // namespace v8
