// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_DATA_VIEW_GEN_H_
#define V8_BUILTINS_BUILTINS_DATA_VIEW_GEN_H_

#include "src/elements-kind.h"
#include "torque-generated/builtins-base-from-dsl-gen.h"

namespace v8 {
namespace internal {

class DataViewBuiltinsAssembler : public BaseBuiltinsFromDSLAssembler {
 public:
  explicit DataViewBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : BaseBuiltinsFromDSLAssembler(state) {}

  TNode<Smi> LoadDataViewByteOffset(TNode<JSDataView> data_view) {
    return LoadObjectField<Smi>(data_view, JSDataView::kByteOffsetOffset);
  }

  TNode<Smi> LoadDataViewByteLength(TNode<JSDataView> data_view) {
    return LoadObjectField<Smi>(data_view, JSDataView::kByteLengthOffset);
  }

  TNode<Int32T> LoadUint8(TNode<RawPtrT> data_pointer, TNode<IntPtrT> offset) {
    return UncheckedCast<Int32T>(
        Load(MachineType::Uint8(), data_pointer, offset));
  }

  TNode<Int32T> LoadInt8(TNode<RawPtrT> data_pointer, TNode<IntPtrT> offset) {
    return UncheckedCast<Int32T>(
        Load(MachineType::Int8(), data_pointer, offset));
  }

  TNode<Uint32T> UncheckedCastInt32ToUint32(TNode<Int32T> value) {
    return Unsigned(value);
  }
};

int32_t DataViewElementSize(ElementsKind elements_kind) {
  return ElementsKindToByteSize(elements_kind);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_DATA_VIEW_GEN_H_
