// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SRC_IC_ACCESSOR_ASSEMBLER_H_
#define V8_SRC_IC_ACCESSOR_ASSEMBLER_H_

#include "src/globals.h"

namespace v8 {
namespace internal {

namespace compiler {
class CodeAssemblerState;
}

class AccessorAssembler {
 public:
  static void GenerateLoadIC(compiler::CodeAssemblerState* state);
  static void GenerateLoadICTrampoline(compiler::CodeAssemblerState* state);
  static void GenerateLoadICProtoArray(compiler::CodeAssemblerState* state);
  static void GenerateLoadGlobalIC(compiler::CodeAssemblerState* state);
  static void GenerateLoadGlobalICTrampoline(
      compiler::CodeAssemblerState* state);
  static void GenerateKeyedLoadICTF(compiler::CodeAssemblerState* state);
  static void GenerateKeyedLoadICTrampolineTF(
      compiler::CodeAssemblerState* state);
  static void GenerateKeyedLoadICMegamorphic(
      compiler::CodeAssemblerState* state);
  static void GenerateStoreIC(compiler::CodeAssemblerState* state);
  static void GenerateStoreICTrampoline(compiler::CodeAssemblerState* state);
  static void GenerateKeyedStoreICTF(compiler::CodeAssemblerState* state,
                                     LanguageMode language_mode);
  static void GenerateKeyedStoreICTrampolineTF(
      compiler::CodeAssemblerState* state, LanguageMode language_mode);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SRC_IC_ACCESSOR_ASSEMBLER_H_
