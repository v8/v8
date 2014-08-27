// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X87

#include "src/codegen.h"
#include "src/ic/ic-conventions.h"

namespace v8 {
namespace internal {

// IC register specifications

const Register LoadConvention::ReceiverRegister() { return edx; }
const Register LoadConvention::NameRegister() { return ecx; }


const Register VectorLoadConvention::SlotRegister() {
  DCHECK(FLAG_vector_ics);
  return eax;
}


const Register FullVectorLoadConvention::VectorRegister() {
  DCHECK(FLAG_vector_ics);
  return ebx;
}


const Register StoreConvention::ReceiverRegister() { return edx; }
const Register StoreConvention::NameRegister() { return ecx; }
const Register StoreConvention::ValueRegister() { return eax; }
const Register StoreConvention::MapRegister() { return ebx; }
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X87
