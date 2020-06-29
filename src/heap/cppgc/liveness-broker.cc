// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/liveness-broker.h"

#include "src/heap/cppgc/heap-object-header-inl.h"
#include "src/heap/cppgc/liveness-broker.h"

namespace cppgc {

bool LivenessBroker::IsHeapObjectAliveImpl(const void* payload) const {
  return internal::HeapObjectHeader::FromPayload(payload).IsMarked();
}

namespace internal {

// static
cppgc::LivenessBroker LivenessBrokerFactory::Create() {
  return cppgc::LivenessBroker();
}

}  // namespace internal

}  // namespace cppgc
