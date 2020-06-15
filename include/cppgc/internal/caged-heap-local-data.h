// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_LOCAL_DATA_H_
#define INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_LOCAL_DATA_H_

namespace cppgc {
namespace internal {

struct CagedHeapLocalData final {
  bool is_marking_in_progress = false;
};

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_LOCAL_DATA_H_
