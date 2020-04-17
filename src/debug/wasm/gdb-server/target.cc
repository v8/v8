// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/wasm/gdb-server/target.h"

#include "src/base/platform/time.h"
#include "src/debug/wasm/gdb-server/gdb-remote-util.h"
#include "src/debug/wasm/gdb-server/gdb-server.h"
#include "src/debug/wasm/gdb-server/packet.h"
#include "src/debug/wasm/gdb-server/session.h"
#include "src/debug/wasm/gdb-server/transport.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

Target::Target(GdbServer* gdb_server)
    : status_(Status::Running), session_(nullptr) {}

void Target::Terminate() {
  // Executed in the Isolate thread.
  status_ = Status::Terminated;
}

void Target::Run(Session* session) {
  // Executed in the GdbServer thread.

  session_ = session;
  do {
    WaitForDebugEvent();
    ProcessCommands();
  } while (!IsTerminated() && session_->IsConnected());
  session_ = nullptr;
}

void Target::WaitForDebugEvent() {
  // Executed in the GdbServer thread.

  if (status_ != Status::Terminated) {
    // Wait for either:
    //   * the thread to fault (or single-step)
    //   * an interrupt from LLDB
    session_->WaitForDebugStubEvent();
  }
}

void Target::ProcessCommands() {
  // GDB-remote messages are processed in the GDBServer thread.

  if (IsTerminated()) {
    return;
  }

  // Now we are ready to process commands.
  // Loop through packets until we process a continue packet or a detach.
  Packet recv, reply;
  do {
    if (!session_->GetPacket(&recv)) continue;
    reply.Clear();
    if (ProcessPacket(&recv, &reply)) {
      // If this is a continue type command, break out of this loop.
      break;
    }
    // Otherwise send the response.
    session_->SendPacket(&reply);
  } while (session_->IsConnected());
}

bool Target::ProcessPacket(const Packet* pktIn, Packet* pktOut) {
  // Pull out the sequence.
  int32_t seq = -1;
  if (pktIn->GetSequence(&seq)) {
    pktOut->SetSequence(seq);
  }

  // Ignore all commands and returns an error.
  pktOut->SetError(Packet::ErrDef::Failed);

  return false;
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
