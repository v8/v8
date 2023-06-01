// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/interface-descriptors-inl.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-graph.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

void MaglevAssembler::Allocate(RegisterSnapshot register_snapshot,
                               Register object, int size_in_bytes,
                               AllocationType alloc_type,
                               AllocationAlignment alignment) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::AllocateHeapNumber(RegisterSnapshot register_snapshot,
                                         Register result,
                                         DoubleRegister value) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StoreTaggedFieldWithWriteBarrier(
    Register object, int offset, Register value,
    RegisterSnapshot register_snapshot, ValueIsCompressed value_is_compressed,
    ValueCanBeSmi value_can_be_smi) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::ToBoolean(Register value, CheckType check_type,
                                ZoneLabelRef is_true, ZoneLabelRef is_false,
                                bool fallthrough_when_true) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::TestTypeOf(
    Register object, interpreter::TestTypeOfFlags::LiteralFlag literal,
    Label* is_true, Label::Distance true_distance, bool fallthrough_when_true,
    Label* is_false, Label::Distance false_distance,
    bool fallthrough_when_false) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::Prologue(Graph* graph) { MAGLEV_NOT_IMPLEMENTED(); }

void MaglevAssembler::MaybeEmitDeoptBuiltinsCall(size_t eager_deopt_count,
                                                 Label* eager_deopt_entry,
                                                 size_t lazy_deopt_count,
                                                 Label* lazy_deopt_entry) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::AllocateTwoByteString(RegisterSnapshot register_snapshot,
                                            Register result, int length) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StringFromCharCode(RegisterSnapshot register_snapshot,
                                         Label* char_code_fits_one_byte,
                                         Register result, Register char_code,
                                         Register scratch) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::LoadSingleCharacterString(Register result,
                                                Register char_code,
                                                Register scratch) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StringCharCodeOrCodePointAt(
    BuiltinStringPrototypeCharCodeOrCodePointAt::Mode mode,
    RegisterSnapshot& register_snapshot, Register result, Register string,
    Register index, Register instance_type, Label* result_fits_one_byte) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::TruncateDoubleToInt32(Register dst, DoubleRegister src) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::TryTruncateDoubleToInt32(Register dst, DoubleRegister src,
                                               Label* fail) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StringLength(Register result, Register string) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StoreFixedArrayElementWithWriteBarrier(
    Register array, Register index, Register value,
    RegisterSnapshot register_snapshot) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::StoreFixedArrayElementNoWriteBarrier(Register array,
                                                           Register index,
                                                           Register value) {
  MAGLEV_NOT_IMPLEMENTED();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
