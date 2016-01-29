// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-unicode-regexps

// test262/data/test/language/literals/regexp/u-dec-esc
assertThrows("/\\1/u");
// test262/language/literals/regexp/u-invalid-char-range-a
assertThrows("/[\\w-a]/u");
// test262/language/literals/regexp/u-invalid-char-range-b
assertThrows("/[a-\\w]/u");
// test262/language/literals/regexp/u-invalid-char-esc
assertThrows("/\\c/u");
assertThrows("/\\c0/u");
// test262/built-ins/RegExp/unicode_restricted_quantifiable_assertion
assertThrows("/(?=.)*/u");
// test262/built-ins/RegExp/unicode_restricted_octal_escape
assertThrows("/[\\1]/u");
assertThrows("/\\00/u");
assertThrows("/\\09/u");
// test262/built-ins/RegExp/unicode_restricted_identity_escape_alpha
assertThrows("/[\\c]/u");
// test262/built-ins/RegExp/unicode_restricted_identity_escape_c
assertThrows("/[\\c0]/u");
// test262/built-ins/RegExp/unicode_restricted_incomple_quantifier
assertThrows("/a{/u");
assertThrows("/a{1,/u");
assertThrows("/{/u");
assertThrows("/}/u");
// test262/data/test/built-ins/RegExp/unicode_restricted_brackets
assertThrows("/]/u");
// test262/built-ins/RegExp/unicode_identity_escape
/\//u;
