// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flushes deferred actions on more registers than fit inline, with captures
// that must be restored or cleared on backtrack.

const kGroups = 40;
const body = '(.)'.repeat(kGroups);

function chars(offset) {
  let s = '';
  for (let i = 0; i < kGroups; i++) {
    s += String.fromCharCode(0x100 + offset + i);
  }
  return s;
}

const s1 = chars(0);
const s2 = chars(kGroups);

function check(re, subject, iteration, tail) {
  const m = re.exec(subject);
  assertNotNull(m);
  assertEquals(kGroups + 2, m.length);
  for (let i = 0; i < kGroups; i++) {
    assertEquals(
        iteration === undefined ? undefined : iteration[i], m[i + 1]);
  }
  assertEquals(tail, m[kGroups + 1]);
}

const greedy = new RegExp('^(?:' + body + ')*(.{3})z$');
const lazy = new RegExp('^(?:' + body + ')*?(.{3})z$');

for (const re of [greedy, lazy]) {
  check(re, s1 + s2 + 'abcz', s2, 'abc');
  check(re, s1 + 'abcz', s1, 'abc');
  check(re, 'abcz', undefined, 'abc');
  assertNull(re.exec(s1 + s2 + 'abc'));
}

// The loop flushes the captures of the first alternative; failing on 'X'
// must clear them again before the second alternative matches.
const alternatives = new RegExp('^(?:' + body + 'q*X|(.*)Y)$');
check(alternatives, s1 + 'Y', undefined, s1);
check(alternatives, s1 + 'qqX', s1, undefined);
