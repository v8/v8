// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-regexp-sequence

// Normal usage.
assertDoesNotThrow("/\\p{Basic_Emoji}/u");
assertTrue(/\p{Basic_Emoji}/u.test("\u{1F6E2}\uFE0F"));

assertDoesNotThrow("/\\p{RGI_Emoji}/u");
assertTrue(/\p{RGI_Emoji}/u.test("\u{1F1E9}\u{1F1EA}"));
assertTrue(/\p{RGI_Emoji}/u.test("\u0023\uFE0F\u20E3"));

assertDoesNotThrow("/\\p{RGI_Emoji_Modifier_Sequence}/u");
assertTrue(/\p{RGI_Emoji_Modifier_Sequence}/u.test("\u26F9\u{1F3FF}"));

assertDoesNotThrow("/\\p{RGI_Emoji_ZWJ_Sequence}/u");
assertTrue(/\p{RGI_Emoji_ZWJ_Sequence}/u.test("\u{1F468}\u{200D}\u{1F467}"));

// Without Unicode flag.
assertDoesNotThrow("/\\p{RGI_Emoji}/");
assertFalse(/\p{RGI_Emoji}/.test("\u{1F1E9}\u{1F1EA}"));
assertTrue(/\p{RGI_Emoji}/.test("\\p{RGI_Emoji}"));

// Negated and/or inside a character class.
assertThrows("/\\P{Basic_Emoji}/u");
assertThrows("/\\P{RGI_Emoji_Modifier_Sequence}/u");
assertThrows("/\\P{RGI_Emoji_Tag_Sequence}/u");
assertThrows("/\\P{RGI_Emoji_ZWJ_Sequence}/u");
assertThrows("/\\P{RGI_Emoji}/u");

assertThrows("/[\\p{Basic_Emoji}]/u");
assertThrows("/[\\p{RGI_Emoji_Modifier_Sequence}]/u");
assertThrows("/[\\p{RGI_Emoji_Tag_Sequence}]/u");
assertThrows("/[\\p{RGI_Emoji_ZWJ_Sequence}]/u");
assertThrows("/[\\p{RGI_Emoji}]/u");

assertThrows("/[\\P{Basic_Emoji}]/u");
assertThrows("/[\\P{RGI_Emoji_Modifier_Sequence}]/u");
assertThrows("/[\\P{RGI_Emoji_Tag_Sequence}]/u");
assertThrows("/[\\P{RGI_Emoji_ZWJ_Sequence}]/u");
assertThrows("/[\\P{RGI_Emoji}]/u");

assertThrows("/[\\w\\p{Basic_Emoji}]/u");
assertThrows("/[\\w\\p{RGI_Emoji_Modifier_Sequence}]/u");
assertThrows("/[\\w\\p{RGI_Emoji_Tag_Sequence}]/u");
assertThrows("/[\\w\\p{RGI_Emoji_ZWJ_Sequence}]/u");
assertThrows("/[\\w\\p{RGI_Emoji}]/u");

assertThrows("/[\\w\\P{Basic_Emoji}]/u");
assertThrows("/[\\w\\P{RGI_Emoji_Modifier_Sequence}]/u");
assertThrows("/[\\w\\P{RGI_Emoji_Tag_Sequence}]/u");
assertThrows("/[\\w\\P{RGI_Emoji_ZWJ_Sequence}]/u");
assertThrows("/[\\w\\P{RGI_Emoji}]/u");

// Two regional indicators, but not a country.
assertFalse(/\p{RGI_Emoji}/u.test("\u{1F1E6}\u{1F1E6}"));

// ZWJ sequence as in two ZWJ elements joined by a ZWJ, but not in the list.
assertFalse(/\p{RGI_Emoji_ZWJ_Sequence}/u.test("\u{1F467}\u{200D}\u{1F468}"));

// Unsupported properties.
assertThrows("/\\p{Emoji_Flag_Sequence}/u");
assertThrows("/\\p{Emoji_Keycap_Sequence}/u");
assertThrows("/\\p{Emoji_Modifier_Sequence}/u");
assertThrows("/\\p{Emoji_Tag_Sequence}/u");
assertThrows("/\\p{Emoji_ZWJ_Sequence}/u");

// More complex regexp.
assertEquals(
    ["country flag: \u{1F1E6}\u{1F1F9}"],
    /Country Flag: \p{RGI_Emoji}/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austria"));
assertEquals(
    ["country flag: \u{1F1E6}\u{1F1F9}", "\u{1F1E6}\u{1F1F9}"],
    /Country Flag: (\p{RGI_Emoji})/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austria"));
assertEquals(
    ["country flag: \u{1F1E6}\u{1F1F9}"],
    /Country Flag: ..(?<=\p{RGI_Emoji})/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austria"));
assertEquals(
    ["flag: \u{1F1E6}\u{1F1F9}", "\u{1F1E6}\u{1F1F9}"],
    /Flag: ..(?<=(\p{RGI_Emoji})|\p{Basic_Emoji})/iu.exec(
        "this is an example of a country flag: \u{1F1E6}\u{1F1F9} is Austria"));

// Partial sequences.
assertFalse(/\p{Basic_Emoji}/u.test("\u{1F6E2}_"));
assertFalse(/\p{RGI_Emoji_Modifier_Sequence}/u.test("\u261D_"));
assertFalse(/\p{RGI_Emoji_Tag_Sequence}/u.test("\u{1F3F4}\u{E0067}\u{E0062}\u{E0065}\u{E006E}\u{E0067}_"));
assertFalse(/\p{RGI_Emoji_ZWJ_Sequence}/u.test("\u{1F468}\u200D\u2764\uFE0F\u200D_"));
assertFalse(/\p{RGI_Emoji}/u.test("\u{1F1E6}_"));
assertFalse(/\p{RGI_Emoji}/u.test("2\uFE0F_"));
