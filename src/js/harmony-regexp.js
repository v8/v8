// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function(global, utils) {

'use strict';

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var GetRegExpFlagGetter = utils.ImportNow("GetRegExpFlagGetter");
var GlobalRegExp = global.RegExp;
var MakeTypeError;

utils.Import(function(from) {
  MakeTypeError = from.MakeTypeError;
});

// -------------------------------------------------------------------

// ES6 draft 12-06-13, section 21.2.5.3
// + https://bugs.ecmascript.org/show_bug.cgi?id=3423
function RegExpGetFlags() {
  if (!IS_SPEC_OBJECT(this)) {
    throw MakeTypeError(
        kRegExpNonObject, "RegExp.prototype.flags", TO_STRING(this));
  }
  var result = '';
  if (this.global) result += 'g';
  if (this.ignoreCase) result += 'i';
  if (this.multiline) result += 'm';
  if (this.unicode) result += 'u';
  if (this.sticky) result += 'y';
  return result;
}

%DefineAccessorPropertyUnchecked(GlobalRegExp.prototype, 'flags',
                                 RegExpGetFlags, null, DONT_ENUM);
%SetNativeFlag(RegExpGetFlags);

%DefineGetterPropertyUnchecked(GlobalRegExp.prototype, "sticky",
    GetRegExpFlagGetter("RegExp.prototype.sticky", REGEXP_STICKY_MASK),
    DONT_ENUM);

%DefineGetterPropertyUnchecked(GlobalRegExp.prototype, "unicode",
    GetRegExpFlagGetter("RegExp.prototype.unicode", REGEXP_UNICODE_MASK),
    DONT_ENUM);
})
