// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Normalizer.
 * This renames variables so that we don't have collisions when combining
 * different files. It also simplifies other logic when e.g. determining the
 * type of an identifier. This also normalizes all loop bodies with blocks.
 */

'use strict';

const babelTypes = require('@babel/types');

const common = require('./common.js');
const mutator = require('./mutator.js');

class NormalizerContext {
  constructor() {
    this.funcIndex = 0;
    this.varIndex = 0;
    this.classIndex = 0;
  }
}

class IdentifierNormalizer extends mutator.Mutator {
  constructor() {
    super();
    this.normalizerContext = new NormalizerContext();
  }

  get visitor() {
    const normalizerContext = this.normalizerContext;
    const renamed = new WeakSet();
    const globalMappings = new Map();

    const loopStatement = {
      enter(path) {
        if (path.node.body && !babelTypes.isBlockStatement(path.node.body)) {
          path.node.body = babelTypes.blockStatement([path.node.body]);
        }
      }
    };

    return [{
      Scope(path) {
        for (const [name, binding] of Object.entries(path.scope.bindings)) {
          if (renamed.has(binding.identifier)) {
            continue;
          }

          renamed.add(binding.identifier);

          if (babelTypes.isClassDeclaration(binding.path.node) ||
              babelTypes.isClassExpression(binding.path.node)) {
            path.scope.rename(name, '__c_' + normalizerContext.classIndex++);
          } else if (babelTypes.isFunctionDeclaration(binding.path.node) ||
                     babelTypes.isFunctionExpression(binding.path.node)) {
            path.scope.rename(name, '__f_' + normalizerContext.funcIndex++);
          } else {
            path.scope.rename(name, '__v_' + normalizerContext.varIndex++);
          }
        }
      },

      AssignmentExpression(path) {
        // Find assignments for which we have no binding in the scope. We assume
        // that these are globals which are local to our script (which weren't
        // declared with var/let/const etc).
        const ids = path.getBindingIdentifiers();
        for (const name in ids) {
          if (!path.scope.getBinding(name)) {
            globalMappings.set(name, '__v_' + normalizerContext.varIndex++);
          }
        }
      },

      WhileStatement: loopStatement,
      DoWhileStatement: loopStatement,
      ForStatement: loopStatement,
    }, {
      // Second pass to rename globals that weren't declared with
      // var/let/const etc.
      Identifier(path) {
        if (!globalMappings.has(path.node.name)) {
          return;
        }

        if (path.scope.getBinding(path.node.name)) {
          // Don't rename if there is a binding that hides the global.
          return;
        }

        if (path.node.name === "constructor" &&
            babelTypes.isClassMethod(path.parent) &&
            path.parent.key == path.node) {
          // Don't touch constructors.
          return;
        }

        path.node.name = globalMappings.get(path.node.name);
      },
    }];
  }
}

module.exports = {
  IdentifierNormalizer: IdentifierNormalizer,
};
