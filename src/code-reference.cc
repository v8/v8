// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-reference.h"

#include "src/handles-inl.h"
#include "src/objects-inl.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {

Address CodeReference::constant_pool() const {
  return kind == JS ? code.js->constant_pool() : code.wasm->constant_pool();
}

Address CodeReference::instruction_start() const {
  return kind == JS ? code.js->InstructionStart()
                    : code.wasm->instructions().start();
}

Address CodeReference::instruction_end() const {
  return kind == JS ? code.js->InstructionEnd()
                    : code.wasm->instructions().start() +
                          code.wasm->instructions().size();
}

int CodeReference::instruction_size() const {
  return kind == JS ? code.js->InstructionSize()
                    : static_cast<int>(code.wasm->instructions().size());
}

const byte* CodeReference::relocation_start() const {
  return kind == JS ? code.js->relocation_start()
                    : code.wasm->reloc_info().start();
}

int CodeReference::relocation_size() const {
  return kind == JS ? code.js->relocation_size()
                    : static_cast<int>(code.wasm->reloc_info().size());
}

}  // namespace internal
}  // namespace v8
