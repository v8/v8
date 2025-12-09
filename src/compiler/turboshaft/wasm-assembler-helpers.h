// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_WASM_ASSEMBLER_HELPERS_H_
#define V8_COMPILER_TURBOSHAFT_WASM_ASSEMBLER_HELPERS_H_

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#include <type_traits>

#include "src/compiler/turboshaft/operations.h"
#include "src/roots/roots.h"

namespace v8::internal::compiler::turboshaft {

template <RootIndex>
struct RootType;

#define DEFINE_ROOT_TYPE(type, name, CamelName) \
  template <>                                   \
  struct RootType<RootIndex::k##CamelName> {    \
    using value = type;                         \
  };
ROOT_LIST(DEFINE_ROOT_TYPE)
#undef DEFINE_ROOT_TYPE

// TODO(mliedtke): Integrate this with the LoadRoot for JS in assembler.h.
template <RootIndex index, typename AssemblerT>
V<typename RootType<index>::value> LoadRootHelper(AssemblerT&& assembler,
                                                  Isolate* isolate = nullptr) {
  using RootObjectType = RootType<index>::value;
  if (RootsTable::IsImmortalImmovable(index)) {
    if (isolate != nullptr) {
      Handle<Object> root = isolate->root_handle(index);
      const bool is_smi = i::IsSmi(*root);
      if constexpr (std::is_convertible_v<Smi, RootObjectType>) {
        if (is_smi) {
          return assembler.SmiConstant(Cast<Smi>(*root));
        }
      }
      CHECK(!is_smi);
      return assembler.HeapConstantMaybeHole(i::Cast<RootObjectType>(root));
    }
    return assembler.Load(assembler.LoadRootRegister(),
                          LoadOp::Kind::RawAligned().Immutable(),
                          MemoryRepresentation::AnyUncompressedTagged(),
                          IsolateData::root_slot_offset(index));
  } else {
    return assembler.Load(assembler.LoadRootRegister(),
                          LoadOp::Kind::RawAligned(),
                          MemoryRepresentation::AnyUncompressedTagged(),
                          IsolateData::root_slot_offset(index));
  }
}

#define LOAD_INSTANCE_FIELD(instance, name, representation)           \
  __ Load(instance, compiler::turboshaft::LoadOp::Kind::TaggedBase(), \
          representation, WasmTrustedInstanceData::k##name##Offset)

#define LOAD_PROTECTED_INSTANCE_FIELD(instance, name, type)       \
  V<type>::Cast(__ LoadProtectedPointerField(                     \
      instance, compiler::turboshaft::LoadOp::Kind::TaggedBase(), \
      WasmTrustedInstanceData::kProtected##name##Offset))

#define LOAD_IMMUTABLE_PROTECTED_INSTANCE_FIELD(instance, name, type)         \
  V<type>::Cast(__ LoadProtectedPointerField(                                 \
      instance, compiler::turboshaft::LoadOp::Kind::TaggedBase().Immutable(), \
      WasmTrustedInstanceData::kProtected##name##Offset))

#define LOAD_IMMUTABLE_INSTANCE_FIELD(instance, name, representation)   \
  __ Load(instance,                                                     \
          compiler::turboshaft::LoadOp::Kind::TaggedBase().Immutable(), \
          representation, WasmTrustedInstanceData::k##name##Offset)

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_ASSEMBLER_HELPERS_H_
