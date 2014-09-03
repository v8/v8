// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-operator.h"
#include "src/types-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

// static
bool StaticParameterTraits<FieldAccess>::Equals(const FieldAccess& lhs,
                                                const FieldAccess& rhs) {
  return lhs.base_is_tagged == rhs.base_is_tagged && lhs.offset == rhs.offset &&
         lhs.machine_type == rhs.machine_type && lhs.type->Is(rhs.type);
}


// static
bool StaticParameterTraits<ElementAccess>::Equals(const ElementAccess& lhs,
                                                  const ElementAccess& rhs) {
  return lhs.base_is_tagged == rhs.base_is_tagged &&
         lhs.header_size == rhs.header_size &&
         lhs.machine_type == rhs.machine_type && lhs.type->Is(rhs.type);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
