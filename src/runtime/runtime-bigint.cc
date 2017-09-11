// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/counters.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_BigInt) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_SMI_ARG_CHECKED(value, 0);

  // For the moment, this is the only way to create a BigInt.

  // Since we currently don't want ClusterFuzz to generate BigInts, we always
  // throw here if the --harmony-bigint flag is disabled. (All --harmony-* flags
  // are blacklisted for ClusterFuzz.)
  if (!FLAG_harmony_bigint) {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                   NewTypeError(MessageTemplate::kUnsupported));
  }

  Handle<BigInt> result = isolate->factory()->NewBigInt();
  result->set_value(value);
  return *result;
}

RUNTIME_FUNCTION(Runtime_BigIntEqual) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, lhs, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, rhs, 1);
  bool result = lhs->IsBigInt() && rhs->IsBigInt() &&
                BigInt::cast(*lhs)->Equals(BigInt::cast(*rhs));
  return *isolate->factory()->ToBoolean(result);
}

RUNTIME_FUNCTION(Runtime_BigIntToBoolean) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_HANDLE_CHECKED(BigInt, bigint, 0);
  return *isolate->factory()->ToBoolean(bigint->value());
}

}  // namespace internal
}  // namespace v8
