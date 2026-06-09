// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --dispatch-table-gc-interval=100 --heap-snapshot-on-gc=0 --heap-snapshot-path=/dev/null

// This test ensures that forcing a GC/Heap Snapshot during bootstrap
// (when JSFunctions are partially initialized with null dispatch handles)
// does not crash the heap snapshot generator.
