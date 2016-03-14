// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-regexp-property --harmony-unicode-regexps

function t(re, s) { assertTrue(re.test(s)); }
function f(re, s) { assertFalse(re.test(s)); }

t(/\p{InASCII}+/u, ".");
t(/\p{In ASCII}+/u, "supercalifragilisticexpialidocious");
t(/\p{In Basic Latin}+/u, ".");
t(/\p{InBasicLatin}+/u, "supercalifragilisticexpialidocious");
t(/\p{InBasic_Latin}+/u, ".");
t(/\p{InBasic_Latin}+/u, "supercalifragilisticexpialidocious");
t(/\p{InBasicLatin}+/u, ".");
t(/\p{InBasicLatin}+/u, "supercalifragilisticexpialidocious");

t(/\p{In CJK}+/u, "话说天下大势，分久必合，合久必分");
t(/\p{InCJK}+/u, "吾庄后有一桃园，花开正盛");
f(/\p{InCJKUnifiedIdeographs}+/u, "おはようございます");
f(/\p{In CJK unified ideographs}+/u,
  "Something is rotten in the state of Denmark");

t(/\p{InLatin_1}+/u, "Wie froh bin ich, daß ich weg bin!");
f(/\p{InASCII}+/u, "奔腾千里荡尘埃，渡水登山紫雾开");
f(/\p{In Latin 1}+/u, "いただきます");

t(/\p{InHiragana}/u, "いただきます");
t(/\p{Hiragana}/u, "\u{1b001}");    // This refers to the script "Hiragana".
f(/\p{InHiragana}/u, "\u{1b001}");  // This refers to the block "Hiragana".

t(/\p{InGreekAndCoptic}/u, "ἄνδρα μοι ἔννεπε, μοῦσα, πολύτροπον, ὃς μάλα πολλὰ");
t(/\p{InGreek}/u, "μῆνιν ἄειδε θεὰ Πηληϊάδεω Ἀχιλῆος");

assertThrows("/\\p{In}/u");
assertThrows("/\\pI/u");
assertThrows("/\\p{I}/u");
assertThrows("/\\p{CJK}/u");
