// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODE_REFERENCE_H_
#define V8_CODE_REFERENCE_H_

#include "src/handles.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

class Code;

namespace wasm {
class WasmCode;
}

struct CodeReference {
  explicit CodeReference(const wasm::WasmCode* wasm_code = nullptr)
      : kind(WASM), code(wasm_code) {}
  explicit CodeReference(Handle<Code> js_code) : kind(JS), code(js_code) {}

  Address constant_pool() const;
  Address instruction_start() const;
  Address instruction_end() const;
  int instruction_size() const;
  const byte* relocation_start() const;
  int relocation_size() const;
  bool is_null() const {
    return kind == JS ? !code.js.is_null() : code.wasm != nullptr;
  }

 private:
  enum { JS, WASM } kind;
  union CodeUnion {
    explicit CodeUnion(Handle<Code> js_code) : js(js_code) {}
    explicit CodeUnion(const wasm::WasmCode* wasm_code) : wasm(wasm_code) {}
    CodeUnion() : wasm(nullptr) {}

    const wasm::WasmCode* wasm;
    Handle<Code> js;
  } code;

  DISALLOW_NEW_AND_DELETE()
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODE_REFERENCE_H_
