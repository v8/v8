// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_SHA_256_H_
#define V8_UTILS_SHA_256_H_

#include <stddef.h>
#include <stdint.h>

const size_t kSizeOfSha256Digest = 32;
const size_t kSizeOfFormattedSha256Digest = (kSizeOfSha256Digest * 2) + 1;

namespace v8 {
namespace internal {

// Convenience method. Returns digest address.
const uint8_t* SHA256_hash(const void* data, size_t len, uint8_t* digest);

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_SHA_256_H_
