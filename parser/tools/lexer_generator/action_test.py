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
from rule_parser import RuleParser, RuleParserState
from nfa_builder import NfaBuilder
from dfa import Dfa

def dfa_from_nfa(nfa):
  (start_name, dfa_nodes) = nfa.compute_dfa()
  return Dfa(start_name, dfa_nodes)

def process_rules(parser_state):
  rule_map = {}
  builder = NfaBuilder()
  for k, v in parser_state.rules.items():
    graphs = []
    for (graph, action) in v['regex']:
      graphs.append(NfaBuilder.add_action(graph, action))
    nfa = builder.nfa(NfaBuilder.or_graphs(graphs))
    dfa = dfa_from_nfa(nfa)
    rule_map[k] = (nfa, dfa)
  return rule_map

class ActionTestCase(unittest.TestCase):

    def __verify_last_action(self, dfa, string, expected_code,
                             expected_condition):
      actions = list(dfa.collect_actions(string))
      self.assertEqual(actions[-1], ('TERMINATE',))
      self.assertEqual(actions[-2][1], expected_code)
      self.assertEqual(actions[-2][2], expected_condition)

    def test_action_precedence(self):
      parser_state = RuleParserState()
      rules = '''<default>
                 "key" { KEYWORD } <<break>>
                 /[a-z]+/ { ID } <<break>>'''
      RuleParser.parse(rules, parser_state)
      automata_for_conditions = process_rules(parser_state)
      self.assertEqual(len(automata_for_conditions), 1)
      self.assertTrue('default' in automata_for_conditions)
      (nfa, dfa) = automata_for_conditions['default']

      self.__verify_last_action(dfa, 'foo', 'ID', 'break')
      self.__verify_last_action(dfa, 'key', 'KEYWORD', 'break')
      self.__verify_last_action(dfa, 'k', 'ID', 'break')
      self.__verify_last_action(dfa, 'ke', 'ID', 'break')
      self.__verify_last_action(dfa, 'keys', 'ID', 'break')

    def test_wrong_action_precedence(self):
      parser_state = RuleParserState()
      rules = '''<default>
                 /[a-z]+/ { ID } <<break>>
                 "key" { KEYWORD } <<break>>'''
      RuleParser.parse(rules, parser_state)
      automata_for_conditions = process_rules(parser_state)
      self.assertEqual(len(automata_for_conditions), 1)
      self.assertTrue('default' in automata_for_conditions)
      (nfa, dfa) = automata_for_conditions['default']

      # The keyword is not recognized because of the rule preference order (ID
      # is preferred over KEYWORD).
      self.__verify_last_action(dfa, 'foo', 'ID', 'break')
      self.__verify_last_action(dfa, 'key', 'ID', 'break')
