// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_WASM_GDB_SERVER_GDB_REMOTE_UTIL_H_
#define V8_DEBUG_WASM_GDB_SERVER_GDB_REMOTE_UTIL_H_

#include <string>
#include <vector>
#include "src/flags/flags.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

#define TRACE_GDB_REMOTE(...)                                            \
  do {                                                                   \
    if (FLAG_trace_wasm_gdb_remote) PrintF("[gdb-remote] " __VA_ARGS__); \
  } while (false)

// Convert from 0-255 to a pair of ASCII chars (0-9,a-f).
void UInt8ToHex(uint8_t byte, char chars[2]);

// Convert a pair of hex chars into a value 0-255 or return false if either
// input character is not a valid nibble.
bool HexToUInt8(const char chars[2], uint8_t* byte);

// Convert from ASCII (0-9,a-f,A-F) to 4b unsigned or return false if the
// input char is unexpected.
bool NibbleToUInt8(char ch, uint8_t* byte);

// Convert the memory pointed to by {mem} into a hex string in GDB-remote
// format.
std::string Mem2Hex(const uint8_t* mem, size_t count);
std::string Mem2Hex(const std::string& str);

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_WASM_GDB_SERVER_GDB_REMOTE_UTIL_H_
