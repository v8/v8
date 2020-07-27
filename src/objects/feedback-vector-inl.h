// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FEEDBACK_VECTOR_INL_H_
#define V8_OBJECTS_FEEDBACK_VECTOR_INL_H_

#include "src/common/globals.h"
#include "src/heap/factory-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/code-inl.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/maybe-object-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

OBJECT_CONSTRUCTORS_IMPL(FeedbackVector, HeapObject)
OBJECT_CONSTRUCTORS_IMPL(FeedbackMetadata, HeapObject)
OBJECT_CONSTRUCTORS_IMPL(ClosureFeedbackCellArray, FixedArray)

NEVER_READ_ONLY_SPACE_IMPL(FeedbackVector)
NEVER_READ_ONLY_SPACE_IMPL(ClosureFeedbackCellArray)

CAST_ACCESSOR(FeedbackVector)
CAST_ACCESSOR(FeedbackMetadata)
CAST_ACCESSOR(ClosureFeedbackCellArray)

INT32_ACCESSORS(FeedbackMetadata, slot_count, kSlotCountOffset)

INT32_ACCESSORS(FeedbackMetadata, closure_feedback_cell_count,
                kFeedbackCellCountOffset)

int32_t FeedbackMetadata::synchronized_slot_count() const {
  return base::Acquire_Load(reinterpret_cast<const base::Atomic32*>(
      FIELD_ADDR(*this, kSlotCountOffset)));
}

int32_t FeedbackMetadata::get(int index) const {
  DCHECK(index >= 0 && index < length());
  int offset = kHeaderSize + index * kInt32Size;
  return ReadField<int32_t>(offset);
}

void FeedbackMetadata::set(int index, int32_t value) {
  DCHECK(index >= 0 && index < length());
  int offset = kHeaderSize + index * kInt32Size;
  WriteField<int32_t>(offset, value);
}

bool FeedbackMetadata::is_empty() const { return slot_count() == 0; }

int FeedbackMetadata::length() const {
  return FeedbackMetadata::length(slot_count());
}

int FeedbackMetadata::GetSlotSize(FeedbackSlotKind kind) {
  switch (kind) {
    case FeedbackSlotKind::kForIn:
    case FeedbackSlotKind::kInstanceOf:
    case FeedbackSlotKind::kCompareOp:
    case FeedbackSlotKind::kBinaryOp:
    case FeedbackSlotKind::kLiteral:
    case FeedbackSlotKind::kTypeProfile:
      return 1;

    case FeedbackSlotKind::kCall:
    case FeedbackSlotKind::kCloneObject:
    case FeedbackSlotKind::kLoadProperty:
    case FeedbackSlotKind::kLoadGlobalInsideTypeof:
    case FeedbackSlotKind::kLoadGlobalNotInsideTypeof:
    case FeedbackSlotKind::kLoadKeyed:
    case FeedbackSlotKind::kHasKeyed:
    case FeedbackSlotKind::kStoreNamedSloppy:
    case FeedbackSlotKind::kStoreNamedStrict:
    case FeedbackSlotKind::kStoreOwnNamed:
    case FeedbackSlotKind::kStoreGlobalSloppy:
    case FeedbackSlotKind::kStoreGlobalStrict:
    case FeedbackSlotKind::kStoreKeyedSloppy:
    case FeedbackSlotKind::kStoreKeyedStrict:
    case FeedbackSlotKind::kStoreInArrayLiteral:
    case FeedbackSlotKind::kStoreDataPropertyInLiteral:
      return 2;

    case FeedbackSlotKind::kInvalid:
    case FeedbackSlotKind::kKindsNumber:
      UNREACHABLE();
  }
  return 1;
}

Handle<FeedbackCell> ClosureFeedbackCellArray::GetFeedbackCell(int index) {
  return handle(FeedbackCell::cast(get(index)), GetIsolate());
}

ACCESSORS(FeedbackVector, shared_function_info, SharedFunctionInfo,
          kSharedFunctionInfoOffset)
WEAK_ACCESSORS(FeedbackVector, optimized_code_weak_or_smi,
               kOptimizedCodeWeakOrSmiOffset)
ACCESSORS(FeedbackVector, closure_feedback_cell_array, ClosureFeedbackCellArray,
          kClosureFeedbackCellArrayOffset)
INT32_ACCESSORS(FeedbackVector, length, kLengthOffset)
INT32_ACCESSORS(FeedbackVector, invocation_count, kInvocationCountOffset)
INT32_ACCESSORS(FeedbackVector, profiler_ticks, kProfilerTicksOffset)

void FeedbackVector::clear_padding() {
  if (FIELD_SIZE(kPaddingOffset) == 0) return;
  DCHECK_EQ(4, FIELD_SIZE(kPaddingOffset));
  memset(reinterpret_cast<void*>(address() + kPaddingOffset), 0,
         FIELD_SIZE(kPaddingOffset));
}

bool FeedbackVector::is_empty() const { return length() == 0; }

FeedbackMetadata FeedbackVector::metadata() const {
  return shared_function_info().feedback_metadata();
}

void FeedbackVector::clear_invocation_count() { set_invocation_count(0); }

Code FeedbackVector::optimized_code() const {
  MaybeObject slot = optimized_code_weak_or_smi();
  DCHECK(slot->IsSmi() || slot->IsWeakOrCleared());
  HeapObject heap_object;
  return slot->GetHeapObject(&heap_object) ? Code::cast(heap_object) : Code();
}

OptimizationMarker FeedbackVector::optimization_marker() const {
  MaybeObject slot = optimized_code_weak_or_smi();
  Smi value;
  if (!slot->ToSmi(&value)) return OptimizationMarker::kNone;
  return static_cast<OptimizationMarker>(value.value());
}

bool FeedbackVector::has_optimized_code() const {
  return !optimized_code().is_null();
}

bool FeedbackVector::has_optimization_marker() const {
  return optimization_marker() != OptimizationMarker::kLogFirstExecution &&
         optimization_marker() != OptimizationMarker::kNone;
}

// Conversion from an integer index to either a slot or an ic slot.
// static
FeedbackSlot FeedbackVector::ToSlot(intptr_t index) {
  DCHECK_LE(static_cast<uintptr_t>(index),
            static_cast<uintptr_t>(std::numeric_limits<int>::max()));
  return FeedbackSlot(static_cast<int>(index));
}

MaybeObject FeedbackVector::Get(FeedbackSlot slot) const {
  const Isolate* isolate = GetIsolateForPtrCompr(*this);
  return Get(isolate, slot);
}

MaybeObject FeedbackVector::Get(const Isolate* isolate,
                                FeedbackSlot slot) const {
  return get(isolate, GetIndex(slot));
}

MaybeObject FeedbackVector::get(int index) const {
  const Isolate* isolate = GetIsolateForPtrCompr(*this);
  return get(isolate, index);
}

MaybeObject FeedbackVector::get(const Isolate* isolate, int index) const {
  DCHECK_LT(static_cast<unsigned>(index), static_cast<unsigned>(length()));
  int offset = OffsetOfElementAt(index);
  return RELAXED_READ_WEAK_FIELD(*this, offset);
}

Handle<FeedbackCell> FeedbackVector::GetClosureFeedbackCell(int index) const {
  DCHECK_GE(index, 0);
  ClosureFeedbackCellArray cell_array =
      ClosureFeedbackCellArray::cast(closure_feedback_cell_array());
  return cell_array.GetFeedbackCell(index);
}

void FeedbackVector::Set(FeedbackSlot slot, MaybeObject value,
                         WriteBarrierMode mode) {
  set(GetIndex(slot), value, mode);
}

void FeedbackVector::set(int index, MaybeObject value, WriteBarrierMode mode) {
  DCHECK_GE(index, 0);
  DCHECK_LT(index, this->length());
  int offset = OffsetOfElementAt(index);
  RELAXED_WRITE_WEAK_FIELD(*this, offset, value);
  CONDITIONAL_WEAK_WRITE_BARRIER(*this, offset, value, mode);
}

void FeedbackVector::Set(FeedbackSlot slot, Object value,
                         WriteBarrierMode mode) {
  set(GetIndex(slot), MaybeObject::FromObject(value), mode);
}

void FeedbackVector::set(int index, Object value, WriteBarrierMode mode) {
  set(index, MaybeObject::FromObject(value), mode);
}

inline MaybeObjectSlot FeedbackVector::slots_start() {
  return RawMaybeWeakField(kFeedbackSlotsOffset);
}

// Helper function to transform the feedback to BinaryOperationHint.
BinaryOperationHint BinaryOperationHintFromFeedback(int type_feedback) {
  switch (type_feedback) {
    case BinaryOperationFeedback::kNone:
      return BinaryOperationHint::kNone;
    case BinaryOperationFeedback::kSignedSmall:
      return BinaryOperationHint::kSignedSmall;
    case BinaryOperationFeedback::kSignedSmallInputs:
      return BinaryOperationHint::kSignedSmallInputs;
    case BinaryOperationFeedback::kNumber:
      return BinaryOperationHint::kNumber;
    case BinaryOperationFeedback::kNumberOrOddball:
      return BinaryOperationHint::kNumberOrOddball;
    case BinaryOperationFeedback::kString:
      return BinaryOperationHint::kString;
    case BinaryOperationFeedback::kBigInt:
      return BinaryOperationHint::kBigInt;
    default:
      return BinaryOperationHint::kAny;
  }
  UNREACHABLE();
}

// Helper function to transform the feedback to CompareOperationHint.
template <CompareOperationFeedback::Type Feedback>
bool Is(int type_feedback) {
  return !(type_feedback & ~Feedback);
}

CompareOperationHint CompareOperationHintFromFeedback(int type_feedback) {
  if (Is<CompareOperationFeedback::kNone>(type_feedback)) {
    return CompareOperationHint::kNone;
  }

  if (Is<CompareOperationFeedback::kSignedSmall>(type_feedback)) {
    return CompareOperationHint::kSignedSmall;
  } else if (Is<CompareOperationFeedback::kNumber>(type_feedback)) {
    return CompareOperationHint::kNumber;
  } else if (Is<CompareOperationFeedback::kNumberOrBoolean>(type_feedback)) {
    return CompareOperationHint::kNumberOrBoolean;
  }

  if (Is<CompareOperationFeedback::kInternalizedString>(type_feedback)) {
    return CompareOperationHint::kInternalizedString;
  } else if (Is<CompareOperationFeedback::kString>(type_feedback)) {
    return CompareOperationHint::kString;
  }

  if (Is<CompareOperationFeedback::kReceiver>(type_feedback)) {
    return CompareOperationHint::kReceiver;
  } else if (Is<CompareOperationFeedback::kReceiverOrNullOrUndefined>(
                 type_feedback)) {
    return CompareOperationHint::kReceiverOrNullOrUndefined;
  }

  if (Is<CompareOperationFeedback::kBigInt>(type_feedback)) {
    return CompareOperationHint::kBigInt;
  }

  if (Is<CompareOperationFeedback::kSymbol>(type_feedback)) {
    return CompareOperationHint::kSymbol;
  }

  DCHECK(Is<CompareOperationFeedback::kAny>(type_feedback));
  return CompareOperationHint::kAny;
}

// Helper function to transform the feedback to ForInHint.
ForInHint ForInHintFromFeedback(int type_feedback) {
  switch (type_feedback) {
    case ForInFeedback::kNone:
      return ForInHint::kNone;
    case ForInFeedback::kEnumCacheKeys:
      return ForInHint::kEnumCacheKeys;
    case ForInFeedback::kEnumCacheKeysAndIndices:
      return ForInHint::kEnumCacheKeysAndIndices;
    default:
      return ForInHint::kAny;
  }
  UNREACHABLE();
}

Handle<Symbol> FeedbackVector::UninitializedSentinel(Isolate* isolate) {
  return isolate->factory()->uninitialized_symbol();
}

Handle<Symbol> FeedbackVector::GenericSentinel(Isolate* isolate) {
  return isolate->factory()->generic_symbol();
}

Handle<Symbol> FeedbackVector::MegamorphicSentinel(Isolate* isolate) {
  return isolate->factory()->megamorphic_symbol();
}

Symbol FeedbackVector::RawUninitializedSentinel(Isolate* isolate) {
  return ReadOnlyRoots(isolate).uninitialized_symbol();
}

bool FeedbackMetadataIterator::HasNext() const {
  return next_slot_.ToInt() < metadata().slot_count();
}

FeedbackSlot FeedbackMetadataIterator::Next() {
  DCHECK(HasNext());
  cur_slot_ = next_slot_;
  slot_kind_ = metadata().GetKind(cur_slot_);
  next_slot_ = FeedbackSlot(next_slot_.ToInt() + entry_size());
  return cur_slot_;
}

int FeedbackMetadataIterator::entry_size() const {
  return FeedbackMetadata::GetSlotSize(kind());
}

MaybeObject NexusConfig::GetFeedback(FeedbackVector vector, int index) const {
  return vector.get(index);
}

void NexusConfig::SetFeedback(FeedbackVector vector, int index,
                              MaybeObject feedback,
                              WriteBarrierMode mode) const {
  vector.set(index, feedback, mode);
}

MaybeObject FeedbackNexus::GetFeedback() const {
  if (g_->config() == NexusConfig::BackgroundThread &&
      !cache_slot1_.is_null()) {
    return *cache_slot1_;
  }
  const int index = vector().GetIndex(slot());
  MaybeObject feedback = g_->GetFeedback(vector(), index);
  FeedbackVector::AssertNoLegacyTypes(feedback);
  if (g_->config() == NexusConfig::BackgroundThread && cache_slot1_.is_null()) {
    cache_slot1_ = MaybeObjectHandle(g_->NewHandle(feedback));
  }
  return feedback;
}

std::pair<MaybeObject, MaybeObject> FeedbackNexus::GetFeedbackPair() const {
  if (g_->config() == NexusConfig::BackgroundThread &&
      !cache_slot1_.is_null()) {
    return std::pair<MaybeObject, MaybeObject>(*cache_slot1_, *cache_slot2_);
  }
  const int index = vector().GetIndex(slot());
  auto pair = g_->GetFeedbackPair(vector(), index);
  FeedbackVector::AssertNoLegacyTypes(pair.first);
  if (g_->config() == NexusConfig::BackgroundThread && cache_slot1_.is_null()) {
    cache_slot1_ = MaybeObjectHandle(g_->NewHandle(pair.first));
    cache_slot2_ = MaybeObjectHandle(g_->NewHandle(pair.second));
  }
  return pair;
}

void FeedbackNexus::SetFeedback(Object feedback, WriteBarrierMode mode) {
  const int index = vector().GetIndex(slot());
  g_->SetFeedback(vector(), index, MaybeObject::FromObject(feedback), mode);
}

void FeedbackNexus::SetFeedback(MaybeObject feedback, WriteBarrierMode mode) {
  FeedbackVector::AssertNoLegacyTypes(feedback);
  const int index = vector().GetIndex(slot());
  g_->SetFeedback(vector(), index, feedback, mode);
}

void FeedbackNexus::SetFeedback(Object feedback, WriteBarrierMode mode,
                                Object feedback_extra,
                                WriteBarrierMode mode_extra) {
  FeedbackVector::AssertNoLegacyTypes(MaybeObject::FromObject(feedback));
  const int index = vector().GetIndex(slot());
  g_->SetFeedbackPair(vector(), index, MaybeObject::FromObject(feedback), mode,
                      MaybeObject::FromObject(feedback_extra), mode_extra);
}

void FeedbackNexus::SetFeedback(Object feedback, WriteBarrierMode mode,
                                MaybeObject feedback_extra,
                                WriteBarrierMode mode_extra) {
  FeedbackVector::AssertNoLegacyTypes(MaybeObject::FromObject(feedback));
  const int index = vector().GetIndex(slot());
  g_->SetFeedbackPair(vector(), index, MaybeObject::FromObject(feedback), mode,
                      feedback_extra, mode_extra);
}

void FeedbackNexus::SetFeedback(MaybeObject feedback, WriteBarrierMode mode,
                                Object feedback_extra,
                                WriteBarrierMode mode_extra) {
  FeedbackVector::AssertNoLegacyTypes(feedback);
  const int index = vector().GetIndex(slot());
  g_->SetFeedbackPair(vector(), index, feedback, mode,
                      MaybeObject::FromObject(feedback_extra), mode_extra);
}

void FeedbackNexus::SetFeedback(MaybeObject feedback, WriteBarrierMode mode,
                                MaybeObject feedback_extra,
                                WriteBarrierMode mode_extra) {
  FeedbackVector::AssertNoLegacyTypes(feedback);
#ifdef DEBUG
  FeedbackVector::AssertNoLegacyTypes(feedback_extra);
#endif
  const int index = vector().GetIndex(slot());
  g_->SetFeedbackPair(vector(), index, feedback, mode, feedback_extra,
                      mode_extra);
}

bool FeedbackNexus::vector_needs_update() const {
  DCHECK_LT(1, FeedbackMetadata::GetSlotSize(kind()));
  // TODO(mvstanton): This is a bizarre request.
  const int index = vector().GetIndex(slot());
  std::pair<MaybeObject, MaybeObject> pair =
      g_->GetFeedbackPair(vector(), index);
  return pair.second.ToSmi().value() != ELEMENT;
}

Isolate* FeedbackNexus::GetIsolate() const { return vector().GetIsolate(); }
}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FEEDBACK_VECTOR_INL_H_
