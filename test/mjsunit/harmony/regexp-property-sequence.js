// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-regexp-sequence

// Normal usage.
assertDoesNotThrow("/\\p{Emoji_Flag_Sequence}/u");
assertTrue(/\p{Emoji_Flag_Sequence}/u.test("\u{1F1E9}\u{1F1EA}"));

assertDoesNotThrow("/\\p{Emoji_Keycap_Sequence}/u");
assertTrue(/\p{Emoji_Keycap_Sequence}/u.test("\u0023\ufe0f\u20e3"));

assertDoesNotThrow("/\\p{Emoji_Keycap_Sequence}/u");
assertFalse(/\p{Emoji_Keycap_Sequence}/u.test("\u0022\ufe0f\u20e3"));

assertDoesNotThrow("/\\p{Emoji_Modifier_Sequence}/u");
assertTrue(/\p{Emoji_Modifier_Sequence}/u.test("\u26f9\u{1f3ff}"));

assertDoesNotThrow("/\\p{Emoji_ZWJ_Sequence}/u");
assertTrue(/\p{Emoji_ZWJ_Sequence}/u.test("\u{1F468}\u{200D}\u{1F467}"));

// Without unicode flag.
assertDoesNotThrow("/\\p{Emoji_Flag_Sequence}/");
assertFalse(/\p{Emoji_Flag_Sequence}/.test("\u{1F1E9}\u{1F1EA}"));
assertTrue(/\p{Emoji_Flag_Sequence}/.test("\\p{Emoji_Flag_Sequence}"));

// Negated and/or inside a character class.
assertThrows("/\\P{Emoji_Flag_Sequence}/u");
assertThrows("/[\\p{Emoji_Flag_Sequence}]/u");
assertThrows("/[\\P{Emoji_Flag_Sequence}]/u");
assertThrows("/[\\w\\p{Emoji_Flag_Sequence}]/u");
assertThrows("/[\\w\\P{Emoji_Flag_Sequence}]/u");

// Two regional indicators, but not a country.
assertFalse(/\p{Emoji_Flag_Sequence}/u.test("\u{1F1E6}\u{1F1E6}"));

// ZWJ sequence as in two ZWJ elements joined by a ZWJ, but not in the list.
assertFalse(/\p{Emoji_ZWJ_Sequence}/u.test("\u{1F467}\u{200D}\u{1F468}"));

// More complex regexp
assertEquals(
    ["country flag: \u{1F1E6}\u{1F1F9}"],
    /Country Flag: \p{Emoji_Flag_Sequence}/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austra"));
assertEquals(
    ["country flag: \u{1F1E6}\u{1F1F9}", "\u{1F1E6}\u{1F1F9}"],
    /Country Flag: (\p{Emoji_Flag_Sequence})/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austra"));
assertEquals(
    ["country flag: \u{1F1E6}\u{1F1F9}"],
    /Country Flag: ..(?<=\p{Emoji_Flag_Sequence})/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austra"));
assertEquals(
    ["flag: \u{1F1E6}\u{1F1F9}", "\u{1F1E6}\u{1F1F9}"],
    /Flag: ..(?<=(\p{Emoji_Flag_Sequence})|\p{Emoji_Keycap_Sequence})/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austra"));
