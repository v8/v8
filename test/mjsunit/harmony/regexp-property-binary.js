// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-regexp-property

function t(re, s) { assertTrue(re.test(s)); }
function f(re, s) { assertFalse(re.test(s)); }

t(/\p{Bidi_Control}+/u, "\u200E");
f(/\p{Bidi_C}+/u, "On a dark desert highway, cool wind in my hair");
t(/\p{AHex}+/u, "DEADBEEF");
t(/\p{Alphabetic}+/u, "abcdefg");
t(/\P{Alphabetic}+/u, "1234");
t(/\p{White_Space}+/u, "\u00A0");
t(/\p{Uppercase}+/u, "V");
f(/\p{Lower}+/u, "U");
t(/\p{Ideo}+/u, "字");
f(/\p{Ideo}+/u, "x");
t(/\p{Noncharacter_Code_Point}+/u, "\uFDD0");
t(/\p{Default_Ignorable_Code_Point}+/u, "\u00AD");
t(/\p{ASCII}+/u, "a");
f(/\p{ASCII}+/u, "äöü");
t(/\p{ID_Start}+/u, "a");
f(/\p{ID_Start}+/u, "1\\");
t(/\p{ID_Continue}+/u, "1");
f(/\p{ID_Continue}+/u, "%\\");
t(/\p{Join_Control}+/u, "\u200c");
f(/\p{Join_Control}+/u, "a1");
t(/\p{Emoji_Presentation}+/u, "\u{1F308}");
f(/\p{Emoji_Presentation}+/u, "x");
t(/\p{Emoji_Modifier}+/u, "\u{1F3FE}");
f(/\p{Emoji_Modifier}+/u, "x");
t(/\p{Emoji_Modifier_Base}+/u, "\u{1F6CC}");
f(/\p{Emoji_Modifier_Base}+/u, "x");

assertThrows("/\\p{Hiragana}/u");
assertThrows("/\\p{Bidi_Class}/u");
assertThrows("/\\p{Bidi_C=False}/u");
assertThrows("/\\P{Bidi_Control=Y}/u");
assertThrows("/\\p{AHex=Yes}/u");
