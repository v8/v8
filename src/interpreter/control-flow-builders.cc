// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/control-flow-builders.h"

namespace v8 {
namespace internal {
namespace interpreter {


LoopBuilder::~LoopBuilder() {
  DCHECK(continue_sites_.empty());
  DCHECK(break_sites_.empty());
}


void LoopBuilder::SetContinueTarget(const BytecodeLabel& target) {
  BindLabels(target, &continue_sites_);
}


void LoopBuilder::SetBreakTarget(const BytecodeLabel& target) {
  BindLabels(target, &break_sites_);
}


void LoopBuilder::EmitJump(ZoneVector<BytecodeLabel>* sites) {
  sites->push_back(BytecodeLabel());
  builder()->Jump(&sites->back());
}


void LoopBuilder::BindLabels(const BytecodeLabel& target,
                             ZoneVector<BytecodeLabel>* sites) {
  for (size_t i = 0; i < sites->size(); i++) {
    BytecodeLabel& site = sites->at(i);
    builder()->Bind(target, &site);
  }
  sites->clear();
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
