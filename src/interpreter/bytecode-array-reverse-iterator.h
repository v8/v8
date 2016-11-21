// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_ARRAY_REVERSE_ITERATOR_H_
#define V8_INTERPRETER_BYTECODE_ARRAY_REVERSE_ITERATOR_H_

#include "src/interpreter/bytecode-array-accessor.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace interpreter {

class V8_EXPORT_PRIVATE BytecodeArrayReverseIterator final
    : public BytecodeArrayAccessor {
 public:
  explicit BytecodeArrayReverseIterator(Handle<BytecodeArray> bytecode_array,
                                        Zone* zone);

  void Advance();
  void Reset();
  bool done() const;

 private:
  ZoneVector<int> offsets_;
  ZoneVector<int>::const_reverse_iterator it_offsets_;

  void UpdateOffsetFromIterator();

  DISALLOW_COPY_AND_ASSIGN(BytecodeArrayReverseIterator);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_ARRAY_REVERSE_ITERATOR_H_
