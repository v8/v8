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
from automaton import Term, Action
from rule_parser import RuleProcessor

class LexerTestCase(unittest.TestCase):

  def __verify_action_stream(self, rules, string, expected):
    expected = map(lambda (action, s) : (
        Action(Term.empty_term(), Term(action)), s),
        expected)
    rule_processor = RuleProcessor(rules, 'latin1')
    automata = rule_processor.default_automata()
    for automaton in [automata.nfa(), automata.dfa(), automata.minimal_dfa()]:
        for i, (action, start, stop) in enumerate(
            automaton.lex(string, rule_processor.default_action())):
          self.assertEquals(expected[i][0], action)
          self.assertEquals(expected[i][1], string[start : stop])

  @staticmethod
  def __terminate():
    return ('terminate', '\0')

  def test_simple(self):
    rules = '''
    <<default>>
    "("           <|LBRACE|>
    ")"           <|RBRACE|>

    "foo"         <|FOO|>
    eos           <terminate>'''

    string = 'foo()'
    self.__verify_action_stream(rules, string,
        [('FOO', 'foo'), ('LBRACE', '('), ('RBRACE', ')'), self.__terminate()])

  def test_maximal_matching(self):
    rules = '''
    <<default>>
    "<"           <|LT|>
    "<<"          <|SHL|>
    " "           <|SPACE|>
    eos           <terminate>'''

    string = '<< <'
    self.__verify_action_stream(rules, string,
        [('SHL', '<<'), ('SPACE', ' '), ('LT', '<'), self.__terminate()])

  def test_consecutive_epsilon_transitions(self):
    rules = '''
    digit = [0-9];
    number = (digit+ ("." digit+)?);
    <<default>>
    number        <|NUMBER|>'''

    string = '555'
    self.__verify_action_stream(rules, string, [('NUMBER', '555')])

  def test_action_precedence(self):
    rules = '''
    <<default>>
    "key" <|KEYWORD|>
    /[a-z]+/ <|ID|>'''

    self.__verify_action_stream(rules, 'ke', [('ID', 'ke')])
    self.__verify_action_stream(rules, 'key', [('KEYWORD', 'key')])
    self.__verify_action_stream(rules, 'keys', [('ID', 'keys')])

  def test_wrong_action_precedence(self):
    rules = '''
    <<default>>
    /[a-z]+/ <|ID|>
    "key" <|KEYWORD|>'''

    # The keyword is not recognized because of the rule preference order (ID
    # is preferred over KEYWORD).
    self.__verify_action_stream(rules, 'ke', [('ID', 'ke')])
    self.__verify_action_stream(rules, 'key', [('ID', 'key')])
    self.__verify_action_stream(rules, 'keys', [('ID', 'keys')])

  def test_simple_subgraph(self):
    rules = '''
    <<default>>
    /[a-z]/ <|ID|Identifier>
    " "     <|SPACE|>
    <<Identifier>>
    /[a-z]/ <|ID|continue>
    '''
    self.__verify_action_stream(rules, 'a bc def',
                                [('ID', 'a'), ('SPACE', ' '), ('ID', 'bc'),
                                 ('SPACE', ' '), ('ID', 'def')])

  def test_entering_subgraph_without_match_action(self):
    # Note: there is no match action for entering the subgraph. It means that
    # one char identifiers are not accepted.
    rules = '''
    <<default>>
    /[a-z]/ <||Identifier>
    " "     <|SPACE|>
    default_action <ILLEGAL>
    <<Identifier>>
    /[a-z]/ <|ID|continue>
    '''
    self.__verify_action_stream(rules, 'bc a def',
                                [('ID', 'bc'), ('SPACE', ' '), ('ILLEGAL', 'a'),
                                 ('SPACE', ' '), ('ID', 'def')])

  def test_subgraph_with_noncontinue(self):
    # In the "Identifier" subgraph, we have rules which don't have "continue".
    rules = '''
    <<default>>
    /[b-z]/ <|ID|Identifier>
    " "     <|SPACE|>
    <<Identifier>>
    /[b-z]/ <|ID|continue>
    /[a]/   <|INVALID|>
    '''
    self.__verify_action_stream(rules, 'bc ba de',
                                [('ID', 'bc'), ('SPACE', ' '),
                                 ('INVALID', 'ba'), ('SPACE', ' '),
                                 ('ID', 'de')])
