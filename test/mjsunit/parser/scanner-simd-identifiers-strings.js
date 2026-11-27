// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-externalize-string

// Identifier and string literal scanning around vector chunk, stream buffer
// and literal buffer boundaries, for one-byte and two-byte sources.

const kSourceKinds = [
  {make: s => s, twoByte: false},
  {
    make: s => {
      const e = createExternalizableString(s);
      externalizeString(e);
      return e;
    },
    twoByte: false
  },
  {make: s => createExternalizableTwoByteString(s), twoByte: true},
  {
    make: s => {
      const e = createExternalizableTwoByteString(s);
      externalizeString(e);
      return e;
    },
    twoByte: true
  },
];

function evalAs(kind, code) {
  // Use a fresh flat copy: the externalizable string helpers return strings
  // that already support externalization as is, which would externalize
  // string constants shared between kinds.
  const src =
      kind.make(%FlattenString(code.substring(0, 1) + code.substring(1)));
  assertEquals(!kind.twoByte && isOneByteString(code), isOneByteString(src));
  return (0, eval)(src);
}

// Code units whose low byte is a string terminator ("'\\\n\r) or an ASCII
// identifier part (a0_).
const kAliasingChars = [
  "\u0122", "\u0127", "\u015c", "\u010a", "\u010d", "\u0161", "\u0130",
  "\u015f",
];

let nextValue = 0;

// Declares a global var for each [source text, expected name] pair and checks
// that each expected name is bound to the last value assigned to it.
function checkIdentifiers(kind, ids, prefix = "") {
  const base = nextValue;
  nextValue += ids.length;
  let code = prefix;
  const expected = new Map();
  ids.forEach(([text, name], i) => {
    code += `var ${text}=${base + i};\n`;
    expected.set(name, base + i);
  });
  evalAs(kind, code);
  for (const [name, value] of expected) {
    assertEquals(value, globalThis[name], name);
  }
}

// Evaluates an array of string literals and checks their values.
function checkStrings(kind, strings, prefix = "") {
  const code = prefix + "[" + strings.map(([text]) => text).join(",\n") + "]";
  assertEquals(strings.map(([, value]) => value), evalAs(kind, code));
}

function identifierCases(oneByte) {
  const ids = [];
  for (let len = 1; len <= 17; len++) {
    const a = "a".repeat(len);
    if (oneByte) {
      const cases = [
        a, "a".repeat(len - 1) + "Z", "a" + "9".repeat(len),
        "$" + "a".repeat(len - 1), "_" + "a".repeat(len - 1),
        "a".repeat(len - 1) + "q", a + "\u00e9", a + "\u00e9" + a,
        "\u00e9" + a
      ];
      for (const id of cases) ids.push([id, id]);
      ids.push([a + "\\u0061", a + "a"]);
      ids.push([a + "\\u0062" + a, a + "b" + a]);
    } else {
      const cases = [
        a + "\u3042", a + "\u3042" + a, a + "\uff21", "\u3042" + a
      ];
      for (const c of kAliasingChars) {
        cases.push(a + c, a + c + a, c + a);
      }
      for (const id of cases) ids.push([id, id]);
    }
  }
  return ids;
}

function stringCases(oneByte) {
  const strings = [];
  for (let len = 0; len <= 17; len++) {
    const a = "x".repeat(len);
    const b = "y".repeat(len);
    if (oneByte) {
      strings.push([`"${a}"`, a]);
      strings.push([`'${a}'`, a]);
      strings.push([`'${a}"quoted"${b}'`, `${a}"quoted"${b}`]);
      strings.push([`"${a}'quoted'${b}"`, `${a}'quoted'${b}`]);
      strings.push([`"${a}\\n${b}"`, `${a}\n${b}`]);
      strings.push([`"${a}\\x41\\u0042${b}"`, `${a}AB${b}`]);
      strings.push([`"${a}\\u3042${b}"`, `${a}\u3042${b}`]);
      strings.push([`"${a}\\\\${b}"`, `${a}\\${b}`]);
      // Code units around the ASCII and Latin-1 boundaries.
      for (const c of ["\x7f", "\x80", "\xe9", "\xff"]) {
        strings.push([`"${a}${c}${b}"`, `${a}${c}${b}`]);
      }
    } else {
      for (const c of ["\u0100", "\u3042", "\uff21", "\u2028", "\u2029",
                       ...kAliasingChars]) {
        strings.push([`"${a}${c}${b}"`, `${a}${c}${b}`]);
        strings.push([`'${a}${c}${b}'`, `${a}${c}${b}`]);
      }
      strings.push([`"${a}\\n\u3042${b}"`, `${a}\n\u3042${b}`]);
    }
  }
  return strings;
}

function runSuite(kind) {
  // 1. Identifiers of lengths 1..17 with varied character classes.
  checkIdentifiers(kind, identifierCases(true));
  if (kind.twoByte) checkIdentifiers(kind, identifierCases(false));

  // 2. Keywords of lengths 2..10 and near-keywords (must not be misclassified).
  evalAs(kind, `
    assertEquals(true, true);
    assertEquals(false, false);
    assertEquals(null, null);
    assertEquals("number", typeof 1);
    assertEquals(true, ({}) instanceof Object);
    var return1 = 42, Return = 43, return_ = 44, return$ = 45;
    assertEquals(174, return1 + Return + return_ + return$);
    var instanceof_ = 1, instanceOf = 2, functionA = 3;
    assertEquals(6, instanceof_ + instanceOf + functionA);
  `);

  // 3. Escaped keyword must still be rejected as a keyword.
  assertThrows(() => evalAs(kind, "var i\\u0066 = 1;"), SyntaxError);
  assertThrows(() => evalAs(kind, "var retur\\u006e = 1;"), SyntaxError);

  // 4. Private names of varying lengths (position_ starts at 1 after '#').
  assertEquals(10, evalAs(kind, `
    class PrivTest {
      #a = 1;
      #abcdefgh = 2;
      #abcdefghij = 3;
      #fooBar123_$ = 4;
      sum() {
        return this.#a + this.#abcdefgh + this.#abcdefghij + this.#fooBar123_$;
      }
    }
    new PrivTest().sum();
  `));

  // 5. Strings of lengths 0..17: ASCII, opposite quotes, escapes, non-ASCII,
  // and one-byte -> two-byte transitions mid-string.
  checkStrings(kind, stringCases(true));
  if (kind.twoByte) checkStrings(kind, stringCases(false));

  // 6. Identifiers and strings straddling the 512-unit stream buffer boundary
  // of one-byte sources, at every offset within the token. Two-byte sources
  // are not buffered.
  for (let pad = 480; !kind.twoByte && pad <= 512; pad++) {
    const str = "boundary_str";
    const src =
        " ".repeat(pad) + `const myBoundaryId = "${str}"; myBoundaryId;`;
    assertEquals(str, evalAs(kind, src));
    const prefix = " ".repeat(pad);
    checkStrings(kind, [[`"${str}\\x41${str}"`, `${str}A${str}`]], prefix);
    checkStrings(kind, [[`"${str}\xe9${str}"`, `${str}\xe9${str}`]], prefix);
  }

  // 7. Literals ending at or crossing the literal buffer's initial capacity
  // (1024), spanning several stream buffers. Chunks of one-byte sources are
  // aligned to the stream buffer, so vary the token start to cover every chunk
  // offset at the capacity.
  for (let offset = 0; offset < (kind.twoByte ? 1 : 8); offset++) {
    const prefix = " ".repeat(offset);
    for (const len of [1024, 1032]) {
      const id = "i" + "d".repeat(len - 1);
      checkIdentifiers(kind, [[id, id]], prefix);
      const s = "s".repeat(len);
      checkStrings(kind, [[`"${s}"`, s]], prefix);
    }
  }
  for (let len = 1023; len <= 1025; len++) {
    const id = "i" + "d".repeat(len - 1);
    checkIdentifiers(kind, [[id + "\\u0061", id + "a"]]);
    const s = "s".repeat(len);
    checkStrings(kind, [[`"${s}\xe9x"`, `${s}\xe9x`]]);
    checkStrings(kind, [[`"${s}\\nx"`, `${s}\nx`]]);
  }

  // 8. Identifiers and strings ending at the end of the source.
  for (let len = 1; len <= 17; len++) {
    const id = "e" + "a".repeat(len - 1);
    globalThis[id] = len;
    assertEquals(len, evalAs(kind, " " + id));
    assertEquals(id, evalAs(kind, `"${id}"`));
    assertThrows(() => evalAs(kind, `"${id}`), SyntaxError);
  }

  // 9. Unterminated string / illegal line terminator inside string.
  assertThrows(() => evalAs(kind, '"unterminated'), SyntaxError);
  assertThrows(() => evalAs(kind, '"line1\nline2"'), SyntaxError);
  assertThrows(() => evalAs(kind, "'line1\rline2'"), SyntaxError);
}

kSourceKinds.forEach(runSuite);
