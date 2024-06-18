// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/turboshaft-builtin-assembler.h"

#include "src/compiler/turboshaft/define-assembler-macros.inc"
#include "src/handles/handles-inl.h"

namespace v8::internal {

using namespace compiler::turboshaft;

V<Boolean> TurboshaftBuiltinAssembler::TrueConstant() {
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kTrueValue));
  Handle<Object> root = isolate()->root_handle(RootIndex::kTrueValue);
  return V<Boolean>::Cast(__ HeapConstant(Cast<HeapObject>(root)));
}
V<Boolean> TurboshaftBuiltinAssembler::FalseConstant() {
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kFalseValue));
  Handle<Object> root = isolate()->root_handle(RootIndex::kFalseValue);
  return V<Boolean>::Cast(__ HeapConstant(Cast<HeapObject>(root)));
}

}  // namespace v8::internal

#include "src/compiler/turboshaft/undef-assembler-macros.inc"
