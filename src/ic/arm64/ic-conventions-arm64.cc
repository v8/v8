// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_ARM64

#include "src/codegen.h"
#include "src/ic/ic-conventions.h"

namespace v8 {
namespace internal {

// IC register specifications
const Register LoadConvention::ReceiverRegister() { return x1; }
const Register LoadConvention::NameRegister() { return x2; }

const Register VectorLoadConvention::SlotRegister() {
  DCHECK(FLAG_vector_ics);
  return x0;
}


const Register FullVectorLoadConvention::VectorRegister() {
  DCHECK(FLAG_vector_ics);
  return x3;
}


const Register StoreConvention::ReceiverRegister() { return x1; }
const Register StoreConvention::NameRegister() { return x2; }
const Register StoreConvention::ValueRegister() { return x0; }


const Register StoreConvention::MapRegister() { return x3; }
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_ARM64
