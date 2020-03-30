// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_STACK_H_
#define V8_HEAP_CPPGC_STACK_H_

#include "src/base/macros.h"

namespace cppgc {
namespace internal {

class StackVisitor {
 public:
  virtual void VisitPointer(const void* address) = 0;
};

// Abstraction over the stack. Supports handling of:
// - native stack;
// - ASAN/MSAN;
// - SafeStack: https://releases.llvm.org/10.0.0/tools/clang/docs/SafeStack.html
class V8_EXPORT_PRIVATE Stack final {
 public:
  explicit Stack(const void* stack_start);

  // Returns true if |slot| is part of the stack and false otherwise.
  bool IsOnStack(void* slot) const;

  // Word-aligned iteration of the stack. Slot values are passed on to
  // |visitor|.
  //
  // TODO(chromium:1056170): Implement all platforms.
#ifdef V8_TARGET_ARCH_X64
  void IteratePointers(StackVisitor* visitor) const;
#endif  // V8_TARGET_ARCH_X64

 private:
  void IteratePointersImpl(StackVisitor* visitor, intptr_t* stack_end) const;

  const void* stack_start_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_STACK_H_
