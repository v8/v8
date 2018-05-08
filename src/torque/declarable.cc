// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>

#include "src/torque/declarable.h"

namespace v8 {
namespace internal {
namespace torque {

bool Type::IsSubtypeOf(const Type* supertype) const {
  const Type* subtype = this;
  while (subtype != nullptr) {
    if (subtype == supertype) return true;
    subtype = subtype->parent();
  }
  return false;
}

std::string Type::GetGeneratedTNodeTypeName() const {
  std::string result = GetGeneratedTypeName();
  DCHECK_EQ(result.substr(0, 6), "TNode<");
  result = result.substr(6, result.length() - 7);
  return result;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
