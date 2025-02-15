// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Analyzer.
 * Mutator that acquires additional information used in subsequent mutators.
 * This mutator doesn't change the structure, but only marks nodes with
 * meta information.
 */

'use strict';

const babelTypes = require('@babel/types');

const common = require('./common.js');
const mutator = require('./mutator.js');

// Additional resource needed if the AsyncIterator ID is found in the code.
const ASYNC_ITERATOR_ID = 'AsyncIterator'
const ASYNC_ITERATOR_RESOURCE = 'async_iterator.js'

class ContextAnalyzer extends mutator.Mutator {
  get visitor() {
    const thisMutator = this;
    return {
      Identifier(path) {
        if (path.node.name === ASYNC_ITERATOR_ID) {
          thisMutator.context.extraResources.add(ASYNC_ITERATOR_RESOURCE);
        }
      },
      FunctionDeclaration(path) {
        if (path.node.id && babelTypes.isIdentifier(path.node.id)) {
          if (common.isFunctionIdentifier(path.node.id.name) &&
              path.node.async) {
            thisMutator.context.asyncFunctions.add(path.node.id.name);
          }
        }
      },
    };
  }
}

module.exports = {
  ContextAnalyzer: ContextAnalyzer,
};
