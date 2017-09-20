// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/counters.h"
#include "src/objects-inl.h"
#include "src/objects/bigint.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_BigIntEqual) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, lhs, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, rhs, 1);
  bool result = lhs->IsBigInt() && rhs->IsBigInt() &&
                BigInt::Equal(BigInt::cast(*lhs), BigInt::cast(*rhs));
  return *isolate->factory()->ToBoolean(result);
}

RUNTIME_FUNCTION(Runtime_BigIntToBoolean) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(BigInt, bigint, 0);
  return *isolate->factory()->ToBoolean(bigint->ToBoolean());
}

}  // namespace internal
}  // namespace v8
