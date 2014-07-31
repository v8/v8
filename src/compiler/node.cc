// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node.h"

namespace v8 {
namespace internal {
namespace compiler {

OStream& operator<<(OStream& os, const Operator& op) { return op.PrintTo(os); }


OStream& operator<<(OStream& os, const Node& n) {
  os << n.id() << ": " << *n.op();
  if (n.op()->InputCount() != 0) {
    os << "(";
    for (int i = 0; i < n.op()->InputCount(); ++i) {
      if (i != 0) os << ", ";
      os << n.InputAt(i)->id();
    }
    os << ")";
  }
  return os;
}
}
}
}  // namespace v8::internal::compiler
