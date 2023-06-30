// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-compiler-definitions.h"

#include "src/base/strings.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-module.h"

namespace v8::internal::compiler {

base::Vector<const char> GetDebugName(Zone* zone,
                                      const wasm::WasmModule* module,
                                      const wasm::WireBytesStorage* wire_bytes,
                                      int index) {
  base::Optional<wasm::ModuleWireBytes> module_bytes =
      wire_bytes->GetModuleBytes();
  if (module_bytes.has_value() &&
      (v8_flags.trace_turbo || v8_flags.trace_turbo_scheduled ||
       v8_flags.trace_turbo_graph || v8_flags.print_wasm_code)) {
    wasm::WireBytesRef name = module->lazily_generated_names.LookupFunctionName(
        module_bytes.value(), index);
    if (!name.is_empty()) {
      int name_len = name.length();
      char* index_name = zone->NewArray<char>(name_len);
      memcpy(index_name, module_bytes->start() + name.offset(), name_len);
      return base::Vector<const char>(index_name, name_len);
    }
  }

  constexpr int kBufferLength = 24;

  base::EmbeddedVector<char, kBufferLength> name_vector;
  int name_len = SNPrintF(name_vector, "wasm-function#%d", index);
  DCHECK(name_len > 0 && name_len < name_vector.length());

  char* index_name = zone->NewArray<char>(name_len);
  memcpy(index_name, name_vector.begin(), name_len);
  return base::Vector<const char>(index_name, name_len);
}

}  // namespace v8::internal::compiler
