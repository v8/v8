// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WASM_ENGINE_H_
#define WASM_ENGINE_H_

#include <memory>

#include "src/wasm/compilation-manager.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {

namespace wasm {

class CompilationManager;

// The central data structure that represents an engine instance capable of
// loading, instantiating, and executing WASM code.
class WasmEngine {
 public:
  explicit WasmEngine(std::unique_ptr<WasmCodeManager> code_manager)
      : code_manager_(std::move(code_manager)) {}

  CompilationManager* compilation_manager() { return &compilation_manager_; }

  WasmCodeManager* code_manager() const { return code_manager_.get(); }

 private:
  CompilationManager compilation_manager_;
  std::unique_ptr<WasmCodeManager> code_manager_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif
