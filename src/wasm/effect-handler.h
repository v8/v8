// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_EFFECT_HANDLER_H_
#define V8_WASM_EFFECT_HANDLER_H_

#include <cstdint>

#include "src/base/bit-field.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/value-type.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace wasm {

struct EffectHandlerTagIndex {
  using IsSwitchField = base::BitField<bool, 0, 1>;
  using IndexField = IsSwitchField::Next<uint32_t, 31>;
  uint32_t tag_and_kind;

  bool is_switch() const { return IsSwitchField::decode(tag_and_kind); }
  uint32_t index() const { return IndexField::decode(tag_and_kind); }

  void encode(bool is_switch, uint32_t tag_index) {
    tag_and_kind =
        IsSwitchField::encode(is_switch) | IndexField::encode(tag_index);
  }

  uint32_t raw_value() const { return tag_and_kind; }
};
static_assert(sizeof(EffectHandlerTagIndex) == sizeof(uint32_t));
static_assert(alignof(EffectHandlerTagIndex) == alignof(uint32_t));

#if V8_ENABLE_WEBASSEMBLY
// Fixed-size entry stored in WasmCode's sorted effect handler table. Entries
// are sorted ascending by {pc_offset} while preserving handler source order
// within each call site (first matching tag wins). For switch handlers
// ({tag_and_kind.is_switch()}), {handler_offset} and {sig} are unused.
struct EffectHandlerEntry {
  uint32_t pc_offset;
  EffectHandlerTagIndex tag_and_kind;
  uint32_t handler_offset;
  CanonicalTypeIndex sig;
};
static_assert(sizeof(EffectHandlerEntry) == 16);
static_assert(alignof(EffectHandlerEntry) == 4);
ASSERT_TRIVIALLY_COPYABLE(EffectHandlerEntry);
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_EFFECT_HANDLER_H_
