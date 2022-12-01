// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/memory-optimization.h"

#include "src/codegen/interface-descriptors-inl.h"
#include "src/compiler/linkage.h"

namespace v8::internal::compiler::turboshaft {

const TSCallDescriptor* CreateAllocateBuiltinDescriptor(Zone* zone) {
  return TSCallDescriptor::Create(
      Linkage::GetStubCallDescriptor(
          zone, AllocateDescriptor{},
          AllocateDescriptor{}.GetStackParameterCount(),
          CallDescriptor::kCanUseRoots, Operator::kNoThrow,
          StubCallMode::kCallCodeObject),
      zone);
}

}  // namespace v8::internal::compiler::turboshaft
