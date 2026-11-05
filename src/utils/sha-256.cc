// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/utils/sha-256.h"

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>

static_assert(kSizeOfSha256Digest == SHA256_DIGEST_LENGTH);

namespace v8 {
namespace internal {

const uint8_t* SHA256_hash(const void* data, size_t len, uint8_t* digest) {
  return ::SHA256(static_cast<const uint8_t*>(data), len, digest);
}

}  // namespace internal
}  // namespace v8
