// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_FWD_H_
#define V8_ZONE_ZONE_FWD_H_

namespace v8 {
namespace internal {

//
// This header contains forward declarations for Zone-related objects and
// containers.
//

class Zone;

template <typename T>
class ZoneList;

// ZonePtrList is a ZoneList of pointers to ZoneObjects allocated in the same
// zone as the list object.
template <typename T>
using ZonePtrList = ZoneList<T*>;

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_ZONE_FWD_H_
