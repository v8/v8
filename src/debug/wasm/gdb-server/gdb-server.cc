// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/wasm/gdb-server/gdb-server.h"

#include "src/debug/wasm/gdb-server/gdb-server-thread.h"
#include "src/wasm/wasm-engine.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

// static
std::unique_ptr<GdbServer> GdbServer::Create() {
  DCHECK(FLAG_wasm_gdb_remote);

  std::unique_ptr<GdbServer> gdb_server(new GdbServer());
  gdb_server->thread_ = std::make_unique<GdbServerThread>(gdb_server.get());
  if (!gdb_server->thread_->StartAndInitialize()) {
    TRACE_GDB_REMOTE(
        "Cannot initialize thread, GDB-remote debugging will be disabled.\n");
    return nullptr;
  }
  return gdb_server;
}

GdbServer::~GdbServer() {
  if (thread_) {
    thread_->Stop();
    thread_->Join();
  }
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
