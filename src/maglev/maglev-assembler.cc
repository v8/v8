// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-assembler-inl.h"
#include "src/maglev/maglev-code-generator.h"

namespace v8 {
namespace internal {
namespace maglev {

Register MaglevAssembler::FromAnyToRegister(const Input& input,
                                            Register scratch) {
  if (input.operand().IsConstant()) {
    input.node()->LoadToRegister(this, scratch);
    return scratch;
  }
  const compiler::AllocatedOperand& operand =
      compiler::AllocatedOperand::cast(input.operand());
  if (operand.IsRegister()) {
    return ToRegister(input);
  } else {
    DCHECK(operand.IsStackSlot());
    Move(scratch, ToMemOperand(input));
    return scratch;
  }
}

void MaglevAssembler::LoadSingleCharacterString(Register result,
                                                int char_code) {
  DCHECK_GE(char_code, 0);
  DCHECK_LT(char_code, String::kMaxOneByteCharCode);
  Register table = result;
  LoadRoot(table, RootIndex::kSingleCharacterStringTable);
  DecompressAnyTagged(
      result, FieldMemOperand(
                  table, FixedArray::kHeaderSize + char_code * kTaggedSize));
}

void MaglevAssembler::CheckMaps(ZoneVector<compiler::MapRef> const& maps,
                                Register map, Label* is_number,
                                Label* no_match) {
  Label done;
  bool has_heap_number_map = false;
  for (auto it = maps.begin(); it != maps.end(); ++it) {
    if (it->IsHeapNumberMap()) {
      has_heap_number_map = true;
    }
    CompareTagged(map, it->object());
    if (it == maps.end() - 1) {
      JumpIf(kNotEqual, no_match);
      // Fallthrough...
    } else {
      JumpIf(kEqual, &done);
    }
  }
  // Bind number case here if one of the maps is HeapNumber.
  if (has_heap_number_map) {
    DCHECK(!is_number->is_bound());
    bind(is_number);
  }
  bind(&done);
}

void MaglevAssembler::LoadDataField(
    const compiler::PropertyAccessInfo& access_info, Register result,
    Register object, Register scratch) {
  DCHECK(access_info.IsDataField() || access_info.IsFastDataConstant());
  // TODO(victorgomes): Support ConstantDataFields.
  Register load_source = object;
  // Resolve property holder.
  if (access_info.holder().has_value()) {
    load_source = scratch;
    Move(load_source, access_info.holder().value().object());
  }
  FieldIndex field_index = access_info.field_index();
  if (!field_index.is_inobject()) {
    Register load_source_object = load_source;
    if (load_source == object) {
      load_source = scratch;
    }
    // The field is in the property array, first load it from there.
    AssertNotSmi(load_source_object);
    DecompressAnyTagged(load_source,
                        FieldMemOperand(load_source_object,
                                        JSReceiver::kPropertiesOrHashOffset));
  }
  AssertNotSmi(load_source);
  DecompressAnyTagged(result,
                      FieldMemOperand(load_source, field_index.offset()));
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
