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

import unittest
from automaton import Action
from rule_parser import RuleProcessor

class LexerTestCase(unittest.TestCase):

  def __verify_action_stream(self, rules, string, expected):
    expected = map(lambda (action, s) : (Action(None, (action, None)), s), expected)
    expected.append((Action(None, ('terminate', None)), '\0'))
    automata = RuleProcessor.parse(rules).default_automata()
    for automaton in [automata.nfa(), automata.dfa(), automata.minimal_dfa()]:
        for i, (action, start, stop) in enumerate(automaton.lex(string)):
          self.assertEquals(expected[i][0], action)
          self.assertEquals(expected[i][1], string[start : stop])

  def test_simple(self):
    rules = '''
    eos = [:eos:];
    <<default>>
    "("           <|LBRACE|>
    ")"           <|RBRACE|>

    "foo"         <|FOO|>
    eos           <|terminate|>'''

    string = 'foo()'
    self.__verify_action_stream(rules, string,
        [('FOO', 'foo'), ('LBRACE', '('), ('RBRACE', ')')])

  def test_maximal_matching(self):
    rules = '''
    eos = [:eos:];
    <<default>>
    "<"           <|LT|>
    "<<"          <|SHL|>
    " "           <|SPACE|>
    eos           <|terminate|>'''

    string = '<< <'
    self.__verify_action_stream(rules, string,
        [('SHL', '<<'), ('SPACE', ' '), ('LT', '<')])

  def test_consecutive_epsilon_transitions(self):
    rules = '''
    eos = [:eos:];
    digit = [0-9];
    number = (digit+ ("." digit+)?);
    <<default>>
    number        <|NUMBER|>
    eos           <|terminate|>'''

    string = '555'
    self.__verify_action_stream(rules, string, [('NUMBER', '555')])
