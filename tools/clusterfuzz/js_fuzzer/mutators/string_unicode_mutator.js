// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview String Unicode mutator.
 * This mutator randomly replaces characters in identifiers with
 * their corresponding \uXXXX Unicode escape sequence.
 * E.g., "a" becomes "\u0061"
 */

'use strict';

const babelTypes = require('@babel/types');
const mutator = require('./mutator.js');
const random = require('../random.js');

function toUnicodeEscape(char) {
  const hex = char.charCodeAt(0).toString(16);
  return '\\u' + hex.padStart(4, '0');
}

class StringUnicodeMutator extends mutator.Mutator {
  get visitor() {
    if (!random.choose(this.settings.ENABLE_UNICODE_ESCAPE_MUTATOR)) {
      return {};
    }
    const settings = this.settings;

    return {
      Identifier(path) {
        const node = path.node;
        const name = node.name;

        if (name.startsWith('__') || name.startsWith('%')) {
          return;
        }

        let newRawName = "";
        let mutated = false;
        for (const char of name) {
          if (random.choose(settings.MUTATE_UNICODE_ESCAPE_PROB)) {
            newRawName += toUnicodeEscape(char);
            mutated = true;
          } else {
            newRawName += char;
          }
        }

        if (mutated && newRawName !== name) {
          node.name = newRawName;
          path.skip();
        }
      }
    }
  }
}

module.exports = {
  StringUnicodeMutator: StringUnicodeMutator,
};
