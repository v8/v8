// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_POINTER_H_
#define V8_SANDBOX_CODE_POINTER_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// Initializes the 'self' pointer of a Code object.
// Creates and initializes an entry in the CodePointerTable and writes a handle
// for that entry into the field. The entry will contain a pointer to the
// owning code object as well as to the entrypoint.
//
// Only available when the sandbox is enabled.
V8_INLINE void InitSelfCodePointerField(Address field_address, Isolate* isolate,
                                        Tagged<HeapObject> owning_code,
                                        Address entrypoint);

// Read the pointer to a Code's entrypoint via a code pointer.
// Only available when the sandbox is enabled as it requires the code pointer
// table.
V8_INLINE Address ReadCodeEntrypointViaCodePointerField(Address field_address);

// Writes the pointer to a Code's entrypoint via a code pointer.
// Only available when the sandbox is enabled as it requires the code pointer
// table.
V8_INLINE void WriteCodeEntrypointViaCodePointerField(Address field_address,
                                                      Address value);

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_CODE_POINTER_H_
