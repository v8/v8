// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_TURBOSHAFT_BUILTIN_ASSEMBLER_H_
#define V8_CODEGEN_TURBOSHAFT_BUILTIN_ASSEMBLER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/define-assembler-macros.inc"

namespace v8::internal {

class TurboshaftBuiltinAssembler : public compiler::turboshaft::TSAssembler<> {
 public:
  // TODO(nicohartmann): Find a solution for the namespace problem.
  using PipelineData = compiler::turboshaft::PipelineData;
  using Word32 = compiler::turboshaft::Word32;
  template <typename T>
  using V = compiler::turboshaft::V<T>;

  TurboshaftBuiltinAssembler(PipelineData* data, Isolate* isolate,
                             compiler::turboshaft::Graph& graph, Zone* zone)
      : TSAssembler(data, graph, graph, zone), isolate_(isolate) {}

  // TODO(nicohartmann): Could move the following into the assembler.
  template <typename BitField>
  V<Word32> DecodeWord32(V<Word32> word32) {
    return DecodeWord32(word32, BitField::kShift, BitField::kMask);
  }

  V<Word32> DecodeWord32(V<Word32> word32, uint32_t shift, uint32_t mask) {
    DCHECK_EQ((mask >> shift) << shift, mask);
    if ((std::numeric_limits<uint32_t>::max() >> shift) ==
        ((std::numeric_limits<uint32_t>::max() & mask) >> shift)) {
      return __ Word32ShiftRightLogical(word32, shift);
    } else {
      return __ Word32BitwiseAnd(__ Word32ShiftRightLogical(word32, shift),
                                 (mask >> shift));
    }
  }

  V<Boolean> TrueConstant();
  V<Boolean> FalseConstant();

  Isolate* isolate() { return isolate_; }

 private:
  Isolate* isolate_;
};

}  // namespace v8::internal

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

#endif  // V8_CODEGEN_TURBOSHAFT_BUILTIN_ASSEMBLER_H_
