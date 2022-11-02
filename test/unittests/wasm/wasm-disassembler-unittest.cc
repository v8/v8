// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <regex>
#include <string>

#include "src/base/vector.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-disassembler-impl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace wasm {

class WasmDisassemblerTest : public ::v8::TestWithPlatform {};

TEST_F(WasmDisassemblerTest, Mvp) {
  // If you want to extend this test:
  // 1. Modify the .wat.inc file included below, e.g., add more instructions.
  // 2. Convert the Wasm text file to a Wasm binary with `wat2wasm`.
  // 3. Convert the Wasm binary to an array init expression with
  // `wami --full-hexdump` and paste it into the included file below.
  // One liner (Linux):
  // wat2wasm wasm-disassembler-unittest-mvp.wat.inc --output=-
  // | wami --full-hexdump
  // | head -n-1 | tail -n+2 > wasm-disassembler-unittest-mvp.wasm.inc

  constexpr byte module_bytes_array[] = {
#include "wasm-disassembler-unittest-mvp.wasm.inc"
  };
  base::Vector<const byte> module_bytes_vector =
      base::ArrayVector(module_bytes_array);

  AccountingAllocator allocator;

  ModuleResult module_result = DecodeWasmModuleForDisassembler(
      module_bytes_vector.begin(), module_bytes_vector.end(), &allocator);
  DCHECK(module_result.ok());
  WasmModule* module = module_result.value().get();

  ModuleWireBytes wire_bytes(module_bytes_vector);
  NamesProvider names(module, module_bytes_vector);

  MultiLineStringBuilder output_sb;

  ModuleDisassembler md(output_sb, module, &names, wire_bytes, &allocator);
  md.PrintModule({0, 2});

  std::ostringstream output;
  output_sb.WriteTo(output);

  // Little trick: polyglot C++/WebAssembly text file.
  // We want to include the expected disassembler text output as a string into
  // this test (instead of reading it from the file at runtime, which would make
  // it dependent on the current working directory).
  // At the same time, we want the included file itself to be valid WAT, such
  // that it can be processed e.g. by wat2wasm to build the module bytes above.
  // For that to work, we abuse that ;; starts a line comment in WAT, but at
  // the same time, ;; in C++ are just two empty statements, which are no
  // harm when including the file here either.
  std::string expected;
#include "wasm-disassembler-unittest-mvp.wat.inc"
  // Remove comment lines which cannot be recovered by a disassembler.
  // They were also used as part of the C++/WAT polyglot trick above.
  expected = std::regex_replace(expected, std::regex(" *;;[^\\n]*\\n?"), "");

  EXPECT_EQ(output.str(), expected);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
