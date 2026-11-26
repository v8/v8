// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --bundle --cache=code --trace-deopt --expose-gc --bytecode-old-time=0

// 1. First bundle script compiles and gets serialized into the code cache while
//    NeedsSourcePositions() is true (via --trace-deopt). Serialization must
//    skip line_ends in the serialized output without clearing it on the live
//    Script in the compilation cache.
// 2. Second bundle script flushes the top-level bytecode so the compilation
//    cache holds a partial entry (cached Script with uncompiled top-level SFI).
// 3. Third bundle script has the exact same source as the first, hitting the
//    partial compilation cache and reusing the cached Script, which must still
//    have its line_ends initialized.

// JS_BUNDLE_SCRIPT
!function eager() { return 1; }();
// JS_BUNDLE_SCRIPT
gc();
// JS_BUNDLE_SCRIPT
!function eager() { return 1; }();
