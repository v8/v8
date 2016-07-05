// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

v3 = Math.floor(0xFFFFFFFF / 4) + 1;
Object.prototype.__defineGetter__(1, function() {
this[1] = Array(0x8000).join();
})
try { v38 = new ArrayBuffer(v3); } catch (e) {}
try { v41 = new Intl.DateTimeFormat(); } catch (e) {}
