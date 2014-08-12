// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_CACHE_H_
#define V8_COMPILER_NODE_CACHE_H_

#include "src/base/macros.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Zone;

namespace compiler {

// Forward declarations.
class Node;

// A cache for nodes based on a key. Useful for implementing canonicalization of
// nodes such as constants, parameters, etc.
template <typename Key>
class NodeCache {
 public:
  explicit NodeCache(int max = 256) : entries_(NULL), size_(0), max_(max) {}

  // Search for node associated with {key} and return a pointer to a memory
  // location in this cache that stores an entry for the key. If the location
  // returned by this method contains a non-NULL node, the caller can use that
  // node. Otherwise it is the responsibility of the caller to fill the entry
  // with a new node.
  // Note that a previous cache entry may be overwritten if the cache becomes
  // too full or encounters too many hash collisions.
  Node** Find(Zone* zone, Key key);

 private:
  struct Entry {
    Key key_;
    Node* value_;
  };

  Entry* entries_;  // lazily-allocated hash entries.
  int size_;
  int max_;

  bool Resize(Zone* zone);

  DISALLOW_COPY_AND_ASSIGN(NodeCache);
};

// Various default cache types.
typedef NodeCache<int32_t> Int32NodeCache;
typedef NodeCache<int64_t> Int64NodeCache;
typedef NodeCache<intptr_t> IntPtrNodeCache;

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_CACHE_H_
