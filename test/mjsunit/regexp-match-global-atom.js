// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-externalize-string
// Flags: --no-enable-experimental-regexp-engine

function referenceMatch(subject, pattern) {
  const result = [];
  let last = -1;
  for (let i = subject.indexOf(pattern); i != -1;
       i = subject.indexOf(pattern, i + pattern.length)) {
    result.push(pattern);
    last = i;
  }
  return {result: result.length ? result : null, last};
}

function check(subject, re) {
  const pattern = patternOf.get(re);
  const expected = referenceMatch(subject, pattern);
  const actual = subject.match(re);
  assertEquals(expected.result, actual);
  if (expected.last != -1) {
    assertEquals(expected.last, RegExp.leftContext.length);
    assertEquals(pattern, RegExp.lastMatch);
  }
}

function makeSubject(length, every, two_byte) {
  let s = '';
  for (let i = 0; i < length; i++) {
    if (i % every == every - 1) {
      s += '\n';
    } else {
      s += String.fromCharCode(97 + (i * 7) % 26);
    }
  }
  if (two_byte) s = '\u1234' + s.substring(1);
  return s;
}

const patternOf = new Map();
function atom(source, pattern = source) {
  const re = new RegExp(source, 'g');
  assertEquals('ATOM', %RegexpTypeTag(re));
  patternOf.set(re, pattern);
  return re;
}
const NL = atom('\n');
const NL_ESCAPED = atom('\\n', '\n');
const A = atom('a');
const AB = atom('ab');
const AA = atom('aa');
// The only multi-char pattern here that occurs in makeSubject() strings.
const AH = atom('ah');
const patterns = [NL, NL_ESCAPED, A, atom('\u1234'), AB, AA, AH, atom('xyz')];

// Lengths around SIMD block boundaries.
const lengths = [0, 1, 2, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256, 257,
                 4079, 4080, 4081, 4096, 70000];

for (const two_byte of [false, true]) {
  for (const every of [1, 3, 40, 1000000]) {
    for (const length of lengths) {
      const s = makeSubject(length, every, two_byte);
      for (const re of patterns) check(s, re);
    }
  }
}

// Overlapping candidates.
check('aaaaa', AA);
check('a'.repeat(100), AA);
check('a'.repeat(101), A);

// Large match counts (also exercises large-object allocation).
{
  const s = '\n'.repeat(300000);
  assertEquals(300000, s.match(NL).length);
  const t = ('\u1234\n').repeat(150000);
  assertEquals(150000, t.match(NL).length);
}

// Non-flat subjects.
{
  const base = makeSubject(5000, 40, false);
  const cons = %ConstructConsString(base.substring(0, 2500),
                                    base.substring(2500));
  check(cons, NL);
  const sliced = %ConstructSlicedString(base, 17);
  check(sliced, NL);
  check(sliced, AB);
}

// External subjects, and slices of them.
{
  const ext = createExternalizableString(makeSubject(5000, 40, false));
  externalizeString(ext);
  assertTrue(isOneByteString(ext));
  check(ext, NL);
  check(%ConstructSlicedString(ext, 13), NL);
  const ext2 = createExternalizableString(makeSubject(5000, 40, true));
  externalizeString(ext2);
  assertFalse(isOneByteString(ext2));
  check(ext2, NL);
  check(%ConstructSlicedString(ext2, 13), NL);
}

// Growing and shrinking slices of the same string, which use the results
// cache.
for (const two_byte of [false, true]) {
  const base = makeSubject(20000, 37, two_byte);
  const indices = [5000, 6000, 5990, 5000, 4000, 4000, 12000, 30, 0, 1, 36,
                   37, 38, 36, 20000, 19999, 2000, 3000];
  for (const re of [NL, NL_ESCAPED, A, AB, AH]) {
    for (const i of indices) check(base.substring(0, i), re);
    for (let i = 3000; i > 2000; i -= 3) check(base.substring(0, i), re);
    for (let i = 3000; i > 2000; i -= 101) {
      check(base.substring(0, i), re);
      check(base.substring(0, i + 50), re);
    }
  }
  // Slices at a non-zero offset.
  for (const i of [100, 5000, 50, 4000]) {
    check(base.substring(10, i), NL);
  }
}

// A shrunk slice without any remaining matches.
{
  const base = 'x'.repeat(100) + '\n' + 'x'.repeat(100);
  check(base.substring(0, 150), NL);
  check(base.substring(0, 101), NL);
  check(base.substring(0, 100), NL);
  check(base.substring(0, 50), NL);
  check(base.substring(0, 150), NL);
}

// A multi-char pattern, with shrunk slices that end after, inside, and before
// the cached matches.
{
  const base = 'x'.repeat(50) + 'ab' + 'x'.repeat(50) + 'ab' + 'x'.repeat(50);
  assertEquals(2, base.substring(0, 150).match(AB).length);
  check(base.substring(0, 150), AB);
  check(base.substring(0, 120), AB);
  check(base.substring(0, 130), AB);
  check(base.substring(0, 104), AB);
  check(base.substring(0, 103), AB);
  check(base.substring(0, 150), AB);
  check(base.substring(0, 60), AB);
  check(base.substring(0, 150), AB);
  check(base.substring(0, 51), AB);
  check(base.substring(0, 150), AB);
}
