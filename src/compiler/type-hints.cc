// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/type-hints.h"

namespace v8 {
namespace internal {
namespace compiler {

std::ostream& operator<<(std::ostream& os, BinaryOperationHints::Hint hint) {
  switch (hint) {
    case BinaryOperationHints::kNone:
      return os << "None";
    case BinaryOperationHints::kSignedSmall:
      return os << "SignedSmall";
    case BinaryOperationHints::kSigned32:
      return os << "Signed32";
    case BinaryOperationHints::kNumber:
      return os << "Number";
    case BinaryOperationHints::kString:
      return os << "String";
    case BinaryOperationHints::kAny:
      return os << "Any";
  }
  UNREACHABLE();
  return os;
}


std::ostream& operator<<(std::ostream& os, BinaryOperationHints hints) {
  return os << hints.left() << "*" << hints.right() << "->" << hints.result();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
