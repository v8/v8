// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --max-lazy --no-jit-fuzzing

try { Function("") } catch (e) {}
