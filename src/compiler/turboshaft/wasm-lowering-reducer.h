// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

#define LOAD_INSTANCE_FIELD(instance_node, name, representation)     \
  __ Load(instance_node, LoadOp::Kind::TaggedBase(), representation, \
          WasmInstanceObject::k##name##Offset);

template <class Next>
class WasmLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()

  OpIndex REDUCE(GlobalGet)(OpIndex instance, const wasm::WasmGlobal* global) {
    return LowerGlobalSetOrGet(instance, OpIndex::Invalid(), global,
                               GlobalMode::kLoad);
  }

  OpIndex REDUCE(GlobalSet)(OpIndex instance, OpIndex value,
                            const wasm::WasmGlobal* global) {
    return LowerGlobalSetOrGet(instance, value, global, GlobalMode::kStore);
  }

  OpIndex REDUCE(Null)(wasm::ValueType type) { return Null(type); }

  OpIndex REDUCE(IsNull)(OpIndex object, wasm::ValueType type) {
    // TODO(14108): Optimize for compressed-pointer, static-root builds.
    return __ TaggedEqual(object, Null(type));
  }

  OpIndex REDUCE(AssertNotNull)(OpIndex object, wasm::ValueType type,
                                TrapId trap_id) {
    if (trap_id == TrapId::kTrapNullDereference) {
      // Skip the check altogether if null checks are turned off.
      if (!v8_flags.experimental_wasm_skip_null_checks) {
        // Use an explicit null check if
        // (1) we cannot use trap handler or
        // (2) the object might be a Smi or
        // (3) the object might be a JS object.
        if (null_check_strategy_ == NullCheckStrategy::kExplicit ||
            wasm::IsSubtypeOf(wasm::kWasmI31Ref.AsNonNull(), type, module_) ||
            wasm::IsSubtypeOf(type, wasm::kWasmExternRef, module_)) {
          __ TrapIf(__ IsNull(object, type), OpIndex::Invalid(), trap_id);
        } else {
          // Otherwise, load the word after the map word.
          static_assert(WasmStruct::kHeaderSize > kTaggedSize);
          static_assert(WasmArray::kHeaderSize > kTaggedSize);
          static_assert(WasmInternalFunction::kHeaderSize > kTaggedSize);
          __ Load(object, LoadOp::Kind::TrapOnNull(),
                  MemoryRepresentation::Int32(), kTaggedSize);
        }
      }
    } else {
      __ TrapIf(__ IsNull(object, type), OpIndex::Invalid(), trap_id);
    }
    return object;
  }

 private:
  enum class GlobalMode { kLoad, kStore };

  static constexpr MemoryRepresentation kMaybeSandboxedPointer =
      V8_ENABLE_SANDBOX_BOOL ? MemoryRepresentation::SandboxedPointer()
                             : MemoryRepresentation::PointerSized();

  MemoryRepresentation RepresentationFor(wasm::ValueType type) {
    return MemoryRepresentation::FromRegisterRepresentation(
        turboshaft::RepresentationFor(type), true);
  }

  OpIndex LowerGlobalSetOrGet(OpIndex instance, OpIndex value,
                              const wasm::WasmGlobal* global, GlobalMode mode) {
    if (global->mutability && global->imported) {
      OpIndex imported_mutable_globals =
          LOAD_INSTANCE_FIELD(instance, ImportedMutableGlobals,
                              MemoryRepresentation::TaggedPointer());
      int field_offset =
          FixedAddressArray::kHeaderSize + global->index * kSystemPointerSize;
      if (global->type.is_reference()) {
        OpIndex buffers =
            LOAD_INSTANCE_FIELD(instance, ImportedMutableGlobalsBuffers,
                                MemoryRepresentation::TaggedPointer());
        int offset_in_buffers =
            FixedArray::kHeaderSize + global->offset * kTaggedSize;
        OpIndex base =
            __ Load(buffers, LoadOp::Kind::TaggedBase(),
                    MemoryRepresentation::AnyTagged(), offset_in_buffers);
        V<Word32> index = __ Load(imported_mutable_globals, OpIndex::Invalid(),
                                  LoadOp::Kind::TaggedBase(),
                                  MemoryRepresentation::Int32(), field_offset);
        V<WordPtr> index_ptr = __ ChangeInt32ToIntPtr(index);
        if (mode == GlobalMode::kLoad) {
          return __ Load(base, index_ptr, LoadOp::Kind::TaggedBase(),
                         MemoryRepresentation::AnyTagged(),
                         FixedArray::kObjectsOffset, kTaggedSizeLog2);
        } else {
          __ Store(base, index_ptr, value, StoreOp::Kind::TaggedBase(),
                   MemoryRepresentation::AnyTagged(),
                   WriteBarrierKind::kFullWriteBarrier,
                   FixedArray::kObjectsOffset, kTaggedSizeLog2);
          return OpIndex::Invalid();
        }
      } else {
        // Global is imported mutable but not a reference.
        OpIndex base = __ Load(imported_mutable_globals, OpIndex::Invalid(),
                               LoadOp::Kind::TaggedBase(),
                               kMaybeSandboxedPointer, field_offset);
        if (mode == GlobalMode::kLoad) {
          return __ Load(base, LoadOp::Kind::RawAligned(),
                         RepresentationFor(global->type), 0);
        } else {
          __ Store(base, value, StoreOp::Kind::RawAligned(),
                   RepresentationFor(global->type),
                   WriteBarrierKind::kNoWriteBarrier, 0);
          return OpIndex::Invalid();
        }
      }
    } else if (global->type.is_reference()) {
      OpIndex base = LOAD_INSTANCE_FIELD(instance, TaggedGlobalsBuffer,
                                         MemoryRepresentation::TaggedPointer());
      int offset = FixedArray::kHeaderSize + global->offset * kTaggedSize;
      if (mode == GlobalMode::kLoad) {
        return __ Load(base, LoadOp::Kind::TaggedBase(),
                       MemoryRepresentation::AnyTagged(), offset);
      } else {
        __ Store(base, value, StoreOp::Kind::TaggedBase(),
                 MemoryRepresentation::AnyTagged(),
                 WriteBarrierKind::kFullWriteBarrier, offset);
        return OpIndex::Invalid();
      }
    } else {
      OpIndex base =
          LOAD_INSTANCE_FIELD(instance, GlobalsStart, kMaybeSandboxedPointer);
      if (mode == GlobalMode::kLoad) {
        return __ Load(base, LoadOp::Kind::RawAligned(),
                       RepresentationFor(global->type), global->offset);
      } else {
        __ Store(base, value, StoreOp::Kind::RawAligned(),
                 RepresentationFor(global->type),
                 WriteBarrierKind::kNoWriteBarrier, global->offset);
        return OpIndex::Invalid();
      }
    }
  }

  OpIndex Null(wasm::ValueType type) {
    OpIndex roots = __ LoadRootRegister();
    RootIndex index = wasm::IsSubtypeOf(type, wasm::kWasmExternRef, module_)
                          ? RootIndex::kNullValue
                          : RootIndex::kWasmNull;
    return __ Load(roots, LoadOp::Kind::RawAligned(),
                   MemoryRepresentation::PointerSized(),
                   IsolateData::root_slot_offset(index));
  }

  const wasm::WasmModule* module_ = PipelineData::Get().wasm_module();
  const NullCheckStrategy null_check_strategy_ =
      trap_handler::IsTrapHandlerEnabled() && V8_STATIC_ROOTS_BOOL
          ? NullCheckStrategy::kTrapHandler
          : NullCheckStrategy::kExplicit;
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_LOWERING_REDUCER_H_
