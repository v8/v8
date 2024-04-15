// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_BUFFER_H_
#define V8_SANDBOX_EXTERNAL_BUFFER_H_

#include "src/common/globals.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

// TODO(v8:14585): Replace with ExternalBufferTag.
template <ExternalPointerTag tag>
class ExternalBufferMember {
 public:
  ExternalBufferMember() = default;

  void Init(IsolateForSandbox isolate, std::pair<Address, size_t> value);

  inline std::pair<Address, size_t> load(const IsolateForSandbox isolate) const;
  inline void store(IsolateForSandbox isolate,
                    std::pair<Address, size_t> value);

  Address storage_address() { return reinterpret_cast<Address>(storage_); }

 private:
  alignas(alignof(Tagged_t)) char storage_[sizeof(ExternalBuffer_t)];
};

// Creates and initializes an entry in the external buffer table and writes the
// handle for that entry to the field.
template <ExternalPointerTag tag>
V8_INLINE void InitExternalBufferField(Address field_address,
                                       IsolateForSandbox isolate,
                                       std::pair<Address, size_t> value);

// If the sandbox is enabled: reads the ExternalBufferHandle from the field and
// loads the corresponding (external pointer, size) tuple from the external
// buffer table. If the sandbox is disabled: loads the (external pointer,
// kEmptySize) from the field.
template <ExternalPointerTag tag>
V8_INLINE std::pair<Address, size_t> ReadExternalBufferField(
    Address field_address, IsolateForSandbox isolate);

// If the sandbox is enabled: reads the ExternalBufferHandle from the field and
// stores the (external pointer, size) tuple to the corresponding entry in the
// external buffer table. If the sandbox is disabled: stores the external
// pointer to the field.
template <ExternalPointerTag tag>
V8_INLINE void WriteExternalBufferField(Address field_address,
                                        IsolateForSandbox isolate,
                                        std::pair<Address, size_t> value);

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_BUFFER_H_
