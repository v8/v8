// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

function HarmonyToStringExtendSymbolPrototype() {
  %CheckIsBootstrapping();

  InstallConstants(global.Symbol, [
     // TODO(dslomov, caitp): Move to symbol.js when shipping
     "toStringTag", symbolToStringTag
  ]);
}

HarmonyToStringExtendSymbolPrototype();
