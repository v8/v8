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
      'ALIAS',
      'EQUALS',
      'REGEX',
      'CONDITION',
      'CONDITION_BEGIN',
      'CONDITION_END',
      'REGEX_AND_TRANSITION',
      'REGEX_AND_BODY',
      )

  t_ANY_ignore = " \t\n"

  states = (
    ('afterAlias', 'exclusive'),
    ('afterAliasEquals', 'exclusive'),
    ('inCondition', 'exclusive'),
    ('seenCondition', 'exclusive'),
    ('afterCondition', 'exclusive'))

  def t_ALIAS(self, t):
    r'[a-zA-Z0-9_]+'
    self.lexer.begin('afterAlias')
    return t

  def t_afterAlias_EQUALS(self, t):
    r'='
    self.lexer.begin('afterAliasEquals')
    return t

  def t_afterAliasEquals_REGEX(self, t):
    r'(?P<regex>.+)\s*;'
    self.lexer.begin('INITIAL')
    return t

  def t_CONDITION_BEGIN(self, t):
    r'<'
    self.lexer.begin('inCondition')
    return t

  def t_inCondition_CONDITION(self, t):
    r'[a-zA-Z0-9_]+'
    self.lexer.begin('seenCondition')
    return t

  def t_seenCondition_CONDITION_END(self, t):
    r'>'
    self.lexer.begin('afterCondition')
    return t

  def t_afterCondition_REGEX_AND_TRANSITION(self, t):
    r'(?P<regex>.+)\s*:=>\s*(?P<new>.+)\s*'
    self.lexer.begin('INITIAL')
    return t

  def t_afterCondition_REGEX_AND_BODY(self, t):
    r'(?P<regex>.+)\s*{\s*(?P<body>.+)\s*}\s*'
    self.lexer.begin('INITIAL')
    return t

  def t_error(self, t):
    raise Exception("Illegal character '%s'" % t.value[0])

  def build(self, **kwargs):
    self.lexer = lex.lex(module=self, **kwargs)
