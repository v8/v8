// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_
#define V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_

#include "src/compiler/feedback-source.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/objects/instance-type-inl.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class MaglevEarlyLoweringReducer : public Next {
  // This Reducer provides some helpers that are used during
  // MaglevGraphBuildingPhase to lower some Maglev operators. Depending on what
  // we decide going forward (regarding SimplifiedLowering for instance), we
  // could introduce new Simplified or JS operations instead of using these
  // helpers to lower, and turn the helpers into regular REDUCE methods in the
  // new simplified lowering or in MachineLoweringReducer.

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(MaglevEarlyLowering)

  void CheckInstanceType(V<Object> input, V<FrameState> frame_state,
                         const FeedbackSource& feedback,
                         InstanceType first_instance_type,
                         InstanceType last_instance_type, bool check_smi) {
    if (check_smi) {
      __ DeoptimizeIf(__ IsSmi(input), frame_state,
                      DeoptimizeReason::kWrongInstanceType, feedback);
    }

    V<i::Map> map = __ LoadMapField(input);

    if (first_instance_type == last_instance_type) {
#if V8_STATIC_ROOTS_BOOL
      if (InstanceTypeChecker::UniqueMapOfInstanceType(first_instance_type)) {
        base::Optional<RootIndex> expected_index =
            InstanceTypeChecker::UniqueMapOfInstanceType(first_instance_type);
        CHECK(expected_index.has_value());
        Handle<HeapObject> expected_map = Handle<HeapObject>::cast(
            isolate_->root_handle(expected_index.value()));
        __ DeoptimizeIfNot(__ TaggedEqual(map, __ HeapConstant(expected_map)),
                           frame_state, DeoptimizeReason::kWrongInstanceType,
                           feedback);
        return;
      }
#endif  // V8_STATIC_ROOTS_BOOL
      V<Word32> instance_type = __ LoadInstanceTypeField(map);
      __ DeoptimizeIfNot(__ Word32Equal(instance_type, first_instance_type),
                         frame_state, DeoptimizeReason::kWrongInstanceType,
                         feedback);
    } else {
      __ DeoptimizeIfNot(CompareInstanceTypeRange(map, first_instance_type,
                                                  last_instance_type),
                         frame_state, DeoptimizeReason::kWrongInstanceType,
                         feedback);
    }
  }

  V<InternalizedString> CheckedInternalizedString(
      V<Object> object, OpIndex frame_state, bool check_smi,
      const FeedbackSource& feedback) {
    if (check_smi) {
      __ DeoptimizeIf(__ IsSmi(object), frame_state, DeoptimizeReason::kSmi,
                      feedback);
    }

    Label<InternalizedString> done(this);
    V<Map> map = __ LoadMapField(object);
    V<Word32> instance_type = __ LoadInstanceTypeField(map);

    // Go to the slow path if this is a non-string, or a non-internalised
    // string.
    static_assert((kStringTag | kInternalizedTag) == 0);
    IF (UNLIKELY(__ Word32BitwiseAnd(
            instance_type, kIsNotStringMask | kIsNotInternalizedMask))) {
      // Deopt if this isn't a string.
      __ DeoptimizeIf(__ Word32BitwiseAnd(instance_type, kIsNotStringMask),
                      frame_state, DeoptimizeReason::kWrongMap, feedback);
      // Deopt if this isn't a thin string.
      static_assert(base::bits::CountPopulation(kThinStringTagBit) == 1);
      __ DeoptimizeIfNot(__ Word32BitwiseAnd(instance_type, kThinStringTagBit),
                         frame_state, DeoptimizeReason::kWrongMap, feedback);
      // Load internalized string from thin string.
      V<InternalizedString> intern_string =
          __ template LoadField<InternalizedString>(
              object, AccessBuilder::ForThinStringActual());
      GOTO(done, intern_string);
    } ELSE {
      GOTO(done, V<InternalizedString>::Cast(object));
    }

    BIND(done, result);
    return result;
  }

  void CheckValueEqualsString(V<Object> object, InternalizedStringRef value,
                              V<FrameState> frame_state,
                              const FeedbackSource& feedback) {
    IF_NOT (LIKELY(__ TaggedEqual(object, __ HeapConstant(value.object())))) {
      __ DeoptimizeIfNot(__ ObjectIsString(object), frame_state,
                         DeoptimizeReason::kNotAString, feedback);
      V<Boolean> is_same_string_bool =
          __ StringEqual(V<String>::Cast(object),
                         __ template HeapConstant<String>(value.object()));
      __ DeoptimizeIf(
          __ RootEqual(is_same_string_bool, RootIndex::kFalseValue, isolate_),
          frame_state, DeoptimizeReason::kWrongValue, feedback);
    }
  }

  V<Object> CheckConstructResult(V<Object> construct_result,
                                 V<Object> implicit_receiver) {
    // If the result is an object (in the ECMA sense), we should get rid
    // of the receiver and use the result; see ECMA-262 version 5.1
    // section 13.2.2-7 on page 74.
    Label<Object> done(this);

    GOTO_IF(
        __ RootEqual(construct_result, RootIndex::kUndefinedValue, isolate_),
        done, implicit_receiver);

    // If the result is a smi, it is *not* an object in the ECMA sense.
    GOTO_IF(__ IsSmi(construct_result), done, implicit_receiver);

    // Check if the type of the result is not an object in the ECMA sense.
    GOTO_IF(JSAnyIsNotPrimitive(construct_result), done, construct_result);

    // Throw away the result of the constructor invocation and use the
    // implicit receiver as the result.
    GOTO(done, implicit_receiver);

    BIND(done, result);
    return result;
  }

  void CheckConstTrackingLetCellTagged(V<Context> context, V<Object> value,
                                       int index, V<FrameState> frame_state,
                                       const FeedbackSource& feedback) {
    V<Object> old_value =
        __ LoadTaggedField(context, Context::OffsetOfElementAt(index));
    IF_NOT (__ TaggedEqual(old_value, value)) {
      CheckConstTrackingLetCell(context, index, frame_state, feedback);
    }
  }

  void CheckConstTrackingLetCell(V<Context> context, int index,
                                 V<FrameState> frame_state,
                                 const FeedbackSource& feedback) {
    // Load the const tracking let side data.
    V<Object> side_data = __ LoadTaggedField(
        context, Context::OffsetOfElementAt(
                     Context::CONST_TRACKING_LET_SIDE_DATA_INDEX));
    V<Object> index_data = __ LoadTaggedField(
        side_data, FixedArray::OffsetOfElementAt(
                       index - Context::MIN_CONTEXT_EXTENDED_SLOTS));
    // If the field is already marked as "not a constant", storing a
    // different value is fine. But if it's anything else (including the hole,
    // which means no value was stored yet), deopt this code. The lower tier
    // code will update the side data and invalidate DependentCode if needed.
    V<Word32> is_const = __ TaggedEqual(
        index_data, __ SmiConstant(ConstTrackingLetCell::kNonConstMarker));
    __ DeoptimizeIfNot(is_const, frame_state,
                       DeoptimizeReason::kConstTrackingLet, feedback);
  }

  V<Smi> UpdateJSArrayLength(V<Word32> length_raw, V<JSArray> object,
                             V<Word32> index) {
    Label<Smi> done(this);
    IF (__ Uint32LessThan(index, length_raw)) {
      GOTO(done, __ TagSmi(length_raw));
    } ELSE {
      V<Word32> new_length_raw =
          __ Word32Add(index, 1);  // This cannot overflow.
      V<Smi> new_length_tagged = __ TagSmi(new_length_raw);
      __ Store(object, new_length_tagged, StoreOp::Kind::TaggedBase(),
               MemoryRepresentation::TaggedSigned(),
               WriteBarrierKind::kNoWriteBarrier, JSArray::kLengthOffset);
      GOTO(done, new_length_tagged);
    }

    BIND(done, length_tagged);
    return length_tagged;
  }

 private:
  V<Word32> JSAnyIsNotPrimitive(V<Object> heap_object) {
    V<Map> map = __ LoadMapField(heap_object);
    if (V8_STATIC_ROOTS_BOOL) {
      // All primitive object's maps are allocated at the start of the read only
      // heap. Thus JS_RECEIVER's must have maps with larger (compressed)
      // addresses.
      return __ Uint32LessThanOrEqual(
          InstanceTypeChecker::kNonJsReceiverMapLimit,
          __ TruncateWordPtrToWord32(__ BitcastTaggedToWordPtr(map)));
    } else {
      static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
      return __ Uint32LessThanOrEqual(FIRST_JS_RECEIVER_TYPE,
                                      __ LoadInstanceTypeField(map));
    }
  }

  V<Word32> CompareInstanceTypeRange(V<Map> map,
                                     InstanceType first_instance_type,
                                     InstanceType last_instance_type) {
    V<Word32> instance_type = __ LoadInstanceTypeField(map);

    if (first_instance_type == 0) {
      return __ Uint32LessThanOrEqual(instance_type, last_instance_type);
    } else {
      return __ Uint32LessThanOrEqual(
          __ Word32Sub(instance_type, first_instance_type),
          last_instance_type - first_instance_type);
    }
  }

  LocalIsolate* isolate_ = __ data() -> isolate()->AsLocalIsolate();
  JSHeapBroker* broker_ = __ data() -> broker();
  LocalFactory* factory_ = isolate_->factory();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_
