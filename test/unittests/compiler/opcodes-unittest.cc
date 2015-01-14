// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>

#include "src/compiler/opcodes.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace compiler {

TEST(IrOpcodeTest, Mnemonic) {
  EXPECT_STREQ("UnknownOpcode",
               IrOpcode::Mnemonic(static_cast<IrOpcode::Value>(123456789)));
#define OPCODE(Opcode) \
  EXPECT_STREQ(#Opcode, IrOpcode::Mnemonic(IrOpcode::k##Opcode));
  ALL_OP_LIST(OPCODE)
#undef OPCODE
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
