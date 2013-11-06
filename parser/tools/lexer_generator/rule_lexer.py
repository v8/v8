# Copyright 2013 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import ply.lex as lex

class RuleLexer:

  tokens = (
    'DEFAULT',
    'IDENTIFIER',
    'STRING',
    'REGEX',
    'CHARACTER_CLASS_REGEX',

    'PLUS',
    'QUESTION_MARK',
    'EQUALS',
    'OR',
    'STAR',
    'LEFT_PARENTHESIS',
    'RIGHT_PARENTHESIS',
    'LESS_THAN',
    'GREATER_THAN',
    'SEMICOLON',
    'ACTION_OPEN',
    'ACTION_CLOSE',

    'LEFT_BRACKET',
    'RIGHT_BRACKET',

    'CODE_FRAGMENT',
  )

  states = (
    ('code','exclusive'),
  )

  t_ignore = " \t\n\r"
  t_code_ignore = ""

  def t_COMMENT(self, t):
    r'\#.*[\n\r]+'
    pass

  def t_IDENTIFIER(self, t):
    r'[a-zA-Z][a-zA-Z0-9_]*'
    if t.value == 'default':
      t.type = 'DEFAULT'
    return t

  t_STRING = r'"((\\("|\w|\\))|[^\\"])+"'
  t_REGEX = r'/[^\/]+/'
  t_CHARACTER_CLASS_REGEX = r'\[([^\]]|\\\])+\]'

  t_PLUS = r'\+'
  t_QUESTION_MARK = r'\?'
  t_STAR = r'\*'
  t_OR = r'\|'
  t_EQUALS = '='
  t_LEFT_PARENTHESIS = r'\('
  t_RIGHT_PARENTHESIS = r'\)'
  t_LESS_THAN = '<'
  t_GREATER_THAN = '>'
  t_SEMICOLON = ';'
  t_ACTION_OPEN = '<<'
  t_ACTION_CLOSE = '>>'

  def t_LEFT_BRACKET(self, t):
    r'{'
    self.lexer.push_state('code')
    self.nesting = 1
    return t

  t_code_CODE_FRAGMENT = r'[^{}]+'

  def t_code_LEFT_BRACKET(self, t):
    r'{'
    self.nesting += 1
    t.type = 'CODE_FRAGMENT'
    return t

  def t_code_RIGHT_BRACKET(self, t):
    r'}'
    self.nesting -= 1
    if self.nesting:
      t.type = 'CODE_FRAGMENT'
    else:
      self.lexer.pop_state()
    return t

  def t_ANY_error(self, t):
    raise Exception("Illegal character '%s'" % t.value[0])

  def build(self, **kwargs):
    self.lexer = lex.lex(module=self, **kwargs)
