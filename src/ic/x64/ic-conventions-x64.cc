// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X64

#include "src/codegen.h"
#include "src/ic/ic-conventions.h"

namespace v8 {
namespace internal {

// IC register specifications
const Register LoadConvention::ReceiverRegister() { return rdx; }
const Register LoadConvention::NameRegister() { return rcx; }


const Register VectorLoadConvention::SlotRegister() {
  DCHECK(FLAG_vector_ics);
  return rax;
}


const Register FullVectorLoadConvention::VectorRegister() {
  DCHECK(FLAG_vector_ics);
  return rbx;
}


const Register StoreConvention::ReceiverRegister() { return rdx; }
const Register StoreConvention::NameRegister() { return rcx; }
const Register StoreConvention::ValueRegister() { return rax; }


const Register StoreConvention::MapRegister() { return rbx; }
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X64
