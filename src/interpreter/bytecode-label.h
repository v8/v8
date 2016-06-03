// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_LABEL_H_
#define V8_INTERPRETER_BYTECODE_LABEL_H_

namespace v8 {
namespace internal {
namespace interpreter {

// A label representing a branch target in a bytecode array. When a
// label is bound, it represents a known position in the bytecode
// array. For labels that are forward references there can be at most
// one reference whilst it is unbound.
class BytecodeLabel final {
 public:
  BytecodeLabel() : bound_(false), offset_(kInvalidOffset) {}

  bool is_bound() const { return bound_; }
  size_t offset() const { return offset_; }

 private:
  static const size_t kInvalidOffset = static_cast<size_t>(-1);

  void bind_to(size_t offset) {
    DCHECK(!bound_ && offset != kInvalidOffset);
    offset_ = offset;
    bound_ = true;
  }

  void set_referrer(size_t offset) {
    DCHECK(!bound_ && offset != kInvalidOffset && offset_ == kInvalidOffset);
    offset_ = offset;
  }

  bool is_forward_target() const {
    return offset() != kInvalidOffset && !is_bound();
  }

  // There are three states for a label:
  //                    bound_   offset_
  //  UNSET             false    kInvalidOffset
  //  FORWARD_TARGET    false    Offset of referring jump
  //  BACKWARD_TARGET    true    Offset of label in bytecode array when bound
  bool bound_;
  size_t offset_;

  friend class BytecodeArrayWriter;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_LABEL_H_
