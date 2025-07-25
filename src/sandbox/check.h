// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CHECK_H_
#define V8_SANDBOX_CHECK_H_

#include <optional>

#include "src/sandbox/hardware-support.h"

// When the sandbox is enabled, a SBXCHECK behaves exactly like a CHECK, but
// indicates that the check is required for the sandbox, i.e. prevents a
// sandbox bypass. When the sandbox is off, it becomes a DCHECK.
//
// As an example, consider a scenario where an in-sandbox object stores an
// index into an out-of-sandbox array (or a similar data structure). While
// under normal circumstances it can be guaranteed that the index will always
// be in bounds, with the sandbox attacker model, we have to assume that the
// in-sandbox object can be corrupted by an attacker and so the access can go
// out-of-bounds. In that case, a SBXCHECK can be used to both prevent memory
// corruption outside of the sandbox and document that there is a
// security-critical invariant that may be violated when an attacker can
// corrupt memory inside the sandbox, but otherwise holds true.
#ifdef V8_ENABLE_SANDBOX

// It's unsafe to access sandbox memory during a SBXCHECK since such an access
// will be inherently racy as we need to assume an attacker can modify the value
// inside the sandbox right before and after the check. If you run into this,
// you might want to read the value outside of the SBXCHECK first to ensure that
// the SBXCHECK and the code that relies on it use the same value. And if in
// doubt, feel free to add someone from the security team as a reviewer. If
// sandbox hardware support is enabled, we'll block these accesses temporarily
// in debug builds.
// Wrap DisallowSandboxAccess into optional to allow the CHECK to be used in
// constexpr contexts.
// TODO: Remove it after switching to C++23.
#define SBXCHECK(condition)                                               \
  do {                                                                    \
    std::optional<v8::internal::DisallowSandboxAccess> no_sandbox_access; \
    if (!std::is_constant_evaluated()) {                                  \
      no_sandbox_access.emplace();                                        \
    }                                                                     \
    CHECK(condition);                                                     \
  } while (false)

#define SBXCHECK_WRAPPED(CONDITION, lhs, rhs)                             \
  do {                                                                    \
    std::optional<v8::internal::DisallowSandboxAccess> no_sandbox_access; \
    if (!std::is_constant_evaluated()) {                                  \
      no_sandbox_access.emplace();                                        \
    }                                                                     \
    CHECK_##CONDITION(lhs, rhs);                                          \
  } while (false)

#define SBXCHECK_EQ(lhs, rhs) SBXCHECK_WRAPPED(EQ, lhs, rhs)
#define SBXCHECK_NE(lhs, rhs) SBXCHECK_WRAPPED(NE, lhs, rhs)
#define SBXCHECK_GT(lhs, rhs) SBXCHECK_WRAPPED(GT, lhs, rhs)
#define SBXCHECK_GE(lhs, rhs) SBXCHECK_WRAPPED(GE, lhs, rhs)
#define SBXCHECK_LT(lhs, rhs) SBXCHECK_WRAPPED(LT, lhs, rhs)
#define SBXCHECK_LE(lhs, rhs) SBXCHECK_WRAPPED(LE, lhs, rhs)
#define SBXCHECK_BOUNDS(index, limit) SBXCHECK_WRAPPED(BOUNDS, index, limit)
#define SBXCHECK_IMPLIES(when, then) SBXCHECK_WRAPPED(IMPLIES, when, then)
#else  // V8_ENABLE_SANDBOX
#define SBXCHECK(condition) DCHECK(condition)
#define SBXCHECK_EQ(lhs, rhs) DCHECK_EQ(lhs, rhs)
#define SBXCHECK_NE(lhs, rhs) DCHECK_NE(lhs, rhs)
#define SBXCHECK_GT(lhs, rhs) DCHECK_GT(lhs, rhs)
#define SBXCHECK_GE(lhs, rhs) DCHECK_GE(lhs, rhs)
#define SBXCHECK_LT(lhs, rhs) DCHECK_LT(lhs, rhs)
#define SBXCHECK_LE(lhs, rhs) DCHECK_LE(lhs, rhs)
#define SBXCHECK_BOUNDS(index, limit) DCHECK_BOUNDS(index, limit)
#define SBXCHECK_IMPLIES(when, then) DCHECK_IMPLIES(when, then)
#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_CHECK_H_
