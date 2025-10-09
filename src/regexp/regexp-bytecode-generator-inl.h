// Copyright 2008-2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_BYTECODE_GENERATOR_INL_H_
#define V8_REGEXP_REGEXP_BYTECODE_GENERATOR_INL_H_

#include "src/regexp/regexp-bytecode-generator.h"
// Include the non-inl header before the rest of the headers.

#include "src/regexp/regexp-bytecodes.h"

namespace v8 {
namespace internal {

void RegExpBytecodeGenerator::EmitWord(BCWordT word) {
  DCHECK(pc_ + sizeof(BCWordT) <= buffer_.size());
  *reinterpret_cast<BCWordT*>(buffer_.data() + pc_) = word;
  pc_ += sizeof(BCWordT);
}

void RegExpBytecodeGenerator::EnsureCapacity(size_t size) {
  if (V8_UNLIKELY(pc_ + size > buffer_.size())) {
    ExpandBuffer();
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_BYTECODE_GENERATOR_INL_H_
