// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --use-strong

// Check that d8 still works with the flag.

// ...and that the flag is active.
// TODO(rossberg): use something that doesn't require eval.
assertThrows("0 == 0", SyntaxError);
