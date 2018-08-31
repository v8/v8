// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/interpreter/bytecode-operands.h"
#include "test/cctest/cctest.h"

namespace {

TEST(GetBytecodeHandler) {
#ifdef V8_EMBEDDED_BYTECODE_HANDLERS
  using Bytecode = i::interpreter::Bytecode;
  using OperandScale = i::interpreter::OperandScale;
  using Builtins = i::Builtins;

  Builtins* builtins = CcTest::i_isolate()->builtins();

  CHECK_EQ(builtins->GetBytecodeHandler(Bytecode::kWide, OperandScale::kSingle),
           builtins->builtin(Builtins::kWideHandler));

  CHECK_EQ(builtins->GetBytecodeHandler(Bytecode::kLdaImmutableContextSlot,
                                        OperandScale::kSingle),
           builtins->builtin(Builtins::kLdaImmutableContextSlotHandler));

  CHECK_EQ(builtins->GetBytecodeHandler(Bytecode::kLdaImmutableContextSlot,
                                        OperandScale::kDouble),
           builtins->builtin(Builtins::kLdaImmutableContextSlotWideHandler));

  CHECK_EQ(
      builtins->GetBytecodeHandler(Bytecode::kLdaImmutableContextSlot,
                                   OperandScale::kQuadruple),
      builtins->builtin(Builtins::kLdaImmutableContextSlotExtraWideHandler));
#endif  // V8_EMBEDDED_BYTECODE_HANDLERS
}

}  // namespace
