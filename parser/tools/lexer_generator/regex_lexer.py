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

class RegexLexer:

  tokens = (

    'GROUP_BEGIN',
    'GROUP_END',

    'CLASS_BEGIN',
    'CLASS_END',

    'OR',
    'ONE_OR_MORE',
    'ZERO_OR_MORE',
    'ZERO_OR_ONE',
    'ANY',

    'LITERAL',

    'RANGE',
    'NOT',
    'CLASS_LITERAL',
  )

  states = (
    ('class','exclusive'),
  )

  def t_ESCAPED_LITERAL(self, t):
    r'\\\(|\\\)|\\\[|\\\]|\\\||\\\+|\\\*|\\\?|\\\.|\\\\'
    t.type = 'LITERAL'
    t.value = t.value[1:]
    return t

  t_GROUP_BEGIN = r'\('
  t_GROUP_END = r'\)'

  t_OR = r'\|'
  t_ONE_OR_MORE = r'\+'
  t_ZERO_OR_MORE = r'\*'
  t_ZERO_OR_ONE = r'\?'

  t_ANY = r'\.'

  t_LITERAL = r'.'

  def t_CLASS_BEGIN(self, t):
    r'\['
    self.lexer.push_state('class')
    return t

  def t_class_CLASS_END(self, t):
    r'\]'
    self.lexer.pop_state()
    return t

  t_class_RANGE = '-'
  t_class_NOT = '\^'

  def t_class_ESCAPED_CLASS_LITERAL(self, t):
    r'\\\^|\\-|\\\[|\\\]'
    t.type = 'CLASS_LITERAL'
    t.value = t.value[1:]
    return t

  t_class_CLASS_LITERAL = r'[a-zA-Z]' # fix this

  t_ANY_ignore  = '\n'

  def t_ANY_error(self, t):
    raise Exception("Illegal character '%s'" % t.value[0])

  def build(self, **kwargs):
    self.lexer = lex.lex(module=self, **kwargs)
