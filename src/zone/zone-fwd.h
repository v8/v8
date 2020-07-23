// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ZONE_ZONE_FWD_H_
#define V8_ZONE_ZONE_FWD_H_

#include "src/common/globals.h"

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

#ifdef V8_COMPRESS_ZONES
static_assert(kSystemPointerSize == 8,
              "Zone compression requires 64-bit architectures");
#define COMPRESS_ZONES_BOOL true
constexpr size_t kZoneReservationSize = static_cast<size_t>(2) * GB;
constexpr size_t kZoneReservationAlignment = static_cast<size_t>(4) * GB;

#else  // V8_COMPRESS_ZONES
#define COMPRESS_ZONES_BOOL false
// These constants must not be used when zone compression is not enabled.
constexpr size_t kZoneReservationSize = 1;
constexpr size_t kZoneReservationAlignment = 1;
#endif  // V8_COMPRESS_ZONES

// The flags controlling whether zones that will be used for allocating
// TurboFan graphs should be compressed or not.
static constexpr bool kCompressGraphZone = COMPRESS_ZONES_BOOL;

}  // namespace internal
}  // namespace v8

#endif  // V8_ZONE_ZONE_FWD_H_
