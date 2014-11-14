// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

function GetTemplateCallSite(siteObj, rawStrings) {
  // TODO(caitp): ensure same template callsite is used for subsequent tag calls

  %AddNamedProperty(siteObj, "raw", %ObjectFreeze(rawStrings),
      READ_ONLY | DONT_ENUM | DONT_DELETE);

  return %ObjectFreeze(siteObj);
}
