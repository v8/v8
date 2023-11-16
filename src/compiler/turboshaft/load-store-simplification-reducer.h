// Copyright 2023 the V8 project authors. All rights reserved.

// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOAD_STORE_SIMPLIFICATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LOAD_STORE_SIMPLIFICATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// This reducer simplifies Turboshaft's "complex" loads and stores into
// simplified ones that are supported on the given target architecture.
template <class Next>
class LoadStoreSimplificationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  // Turboshaft's loads and stores follow the pattern of
  // *(base + index * element_size_log2 + displacement), but architectures
  // typically support only a limited `element_size_log2`.
#if V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_RISCV64
  static constexpr int kMaxElementSizeLog2 = 0;
#else
  static constexpr int kMaxElementSizeLog2 = 3;
#endif

  OpIndex REDUCE(Load)(OpIndex base, OptionalOpIndex index, LoadOp::Kind kind,
                       MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_size_log2) {
    if (lowering_enabled_) {
      if (element_size_log2 > kMaxElementSizeLog2) {
        DCHECK(index.valid());
        index = __ WordPtrShiftLeft(index.value(), element_size_log2);
        element_size_log2 = 0;
      }

      // TODO(12783): This needs to be extended for all architectures that don't
      // have loads with the base + index * element_size + offset pattern.
#if V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_RISCV64
      // If an index is present, the element_size_log2 is changed to zero
      // (above). So any load follows the form *(base + offset) where offset can
      // either be a dynamic value ("index" in the LoadOp) or a static value
      // ("offset" in the LoadOp). Similarly, as tagged loads result in
      // modfiying the offset by -1, those loads are converted into raw loads.

      if (kind.tagged_base) {
        kind.tagged_base = false;
        offset -= kHeapObjectTag;
        base = __ BitcastTaggedToWord(base);
      }
      if (index.has_value() && offset != 0) {
        index = __ WordPtrAdd(index.value(), offset);
        offset = 0;
      }
      // A lowered load can have either an index or an offset != 0.
      DCHECK_IMPLIES(index.has_value(), offset == 0);
      // If it has an index, the "element size" has to be 1 Byte.
      // Note that the element size does not encode the size of the loaded value
      // as that is encoded by the MemoryRepresentation, it only specifies a
      // factor as a power of 2 to multiply the index with.
      DCHECK_IMPLIES(index.has_value(), element_size_log2 == 0);
#endif
    }
    return Next::ReduceLoad(base, index, kind, loaded_rep, result_rep, offset,
                            element_size_log2);
  }

  OpIndex REDUCE(Store)(OpIndex base, OptionalOpIndex index, OpIndex value,
                        StoreOp::Kind kind, MemoryRepresentation stored_rep,
                        WriteBarrierKind write_barrier, int32_t offset,
                        uint8_t element_size_log2,
                        bool maybe_initializing_or_transitioning,
                        IndirectPointerTag maybe_indirect_pointer_tag) {
    if (lowering_enabled_) {
      if (element_size_log2 > kMaxElementSizeLog2) {
        DCHECK(index.valid());
        index = __ WordPtrShiftLeft(index.value(), element_size_log2);
        element_size_log2 = 0;
      }
    }
    return Next::ReduceStore(base, index, value, kind, stored_rep,
                             write_barrier, offset, element_size_log2,
                             maybe_initializing_or_transitioning,
                             maybe_indirect_pointer_tag);
  }

 private:
  bool is_wasm_ = PipelineData::Get().is_wasm();
  // TODO(12783): Remove this flag once the Turbofan instruction selection has
  // been replaced.
  bool lowering_enabled_ =
      (is_wasm_ && v8_flags.turboshaft_wasm_instruction_selection) ||
      (!is_wasm_ && v8_flags.turboshaft_instruction_selection);
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOAD_STORE_SIMPLIFICATION_REDUCER_H_
