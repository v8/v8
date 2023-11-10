// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOAD_SIMPLIFICATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LOAD_SIMPLIFICATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// This reducer simplifies Turboshaft's "complex" loads into simplified loads
// that only have either an index or an offset. If an index is present, the
// element_size_log2 is changed to zero.
// So any load follows the form *(base + offset) where offset can either be a
// dynamic value ("index" in the LoadOp) or a static value ("offset" in the
// LoadOp).
// Similarly, as tagged loads result in modfiying the offset by -1, those loads
// are converted into raw loads.
template <class Next>
class LoadSimplificationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE(Load)(OpIndex base, OptionalOpIndex index, LoadOp::Kind kind,
                       MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_size_log2) {
    if (kind.tagged_base) {
      kind.tagged_base = false;
      offset -= kHeapObjectTag;
      base = __ BitcastTaggedToWord(base);
    }
    if (index.has_value()) {
      if (element_size_log2 != 0) {
        index = __ WordPtrShiftLeft(index.value(), element_size_log2);
        element_size_log2 = 0;
      }
      if (offset != 0) {
        index = __ WordPtrAdd(index.value(), offset);
        offset = 0;
      }
    }
    // A lowered load can have either an index or an offset != 0.
    DCHECK_IMPLIES(index.has_value(), offset == 0);
    // If it has an index, the "element size" has to be 1 Byte.
    // Note that the element size does not encode the size of the loaded value
    // as that is encoded by the MemoryRepresentation, it only specifies a
    // factor as a power of 2 to multiply the index with.
    DCHECK_IMPLIES(index.has_value(), element_size_log2 == 0);

    return Next::ReduceLoad(base, index, kind, loaded_rep, result_rep, offset,
                            element_size_log2);
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOAD_SIMPLIFICATION_REDUCER_H_
