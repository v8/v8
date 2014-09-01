// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_ARM

#include "src/codegen.h"
#include "src/ic/ic-conventions.h"

namespace v8 {
namespace internal {

// IC register specifications
const Register LoadConvention::ReceiverRegister() { return r1; }
const Register LoadConvention::NameRegister() { return r2; }


const Register VectorLoadConvention::SlotRegister() { return r0; }


const Register FullVectorLoadConvention::VectorRegister() { return r3; }


const Register StoreConvention::ReceiverRegister() { return r1; }
const Register StoreConvention::NameRegister() { return r2; }
const Register StoreConvention::ValueRegister() { return r0; }
const Register StoreConvention::MapRegister() { return r3; }


const Register InstanceofConvention::left() { return r0; }
const Register InstanceofConvention::right() { return r1; }
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_ARM
