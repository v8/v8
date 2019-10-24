// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

const kLength = Math.min(536870912, %StringMaxLength());
const v2 = "foo".padEnd(kLength);
delete v2[v2];
