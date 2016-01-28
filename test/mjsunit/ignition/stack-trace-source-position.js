// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --ignition --ignition-filter=f

function f() {
  return new Error().stack;
}

// TODO(yangguo): this is just a dummy source position calculated for
// interpreter bytecode. Update this once something better comes along.
assertTrue(/at f.*?:\d+:\d+/.test(f()));
