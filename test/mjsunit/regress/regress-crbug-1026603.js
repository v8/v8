// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function f() {
  with ({}) {
    // Make sure that variable conflict resulution and variable lookup through
    // deserialized scopes use the same cache scope. Declare a variable which
    // checks for (and fails to find) a conflict, allocating the f variable as
    // it goes, then access f, which also looks up f.
    eval("var f; f;");
  }
})();
