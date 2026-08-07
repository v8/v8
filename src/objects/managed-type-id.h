// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_MANAGED_TYPE_ID_H_
#define V8_OBJECTS_MANAGED_TYPE_ID_H_

#include <stdint.h>

#include "include/v8-internal.h"

namespace v8::internal {

constexpr const char* ToString(ManagedTypeId type_id) {
  switch (type_id) {
    case ManagedTypeId::kBackingStore:
      return "BackingStore";
    case ManagedTypeId::kTestDeleteCounter:
      return "TestDeleteCounter";
    case ManagedTypeId::kWasmStreaming:
      return "WasmStreaming";
    case ManagedTypeId::kWasmFuncData:
      return "WasmFuncData";
  }
}

}  // namespace v8::internal

#endif  // V8_OBJECTS_MANAGED_TYPE_ID_H_
