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
from regex_parser import RegexParser
from nfa_builder import NfaBuilder
from dfa import Dfa

def dfa_from_nfa(nfa):
  (start_name, dfa_nodes) = nfa.compute_dfa()
  return Dfa(start_name, dfa_nodes)

def build_automata(string):
  nfa = NfaBuilder().nfa(RegexParser.parse(string))
  dfa = dfa_from_nfa(nfa)
  return (nfa, dfa, dfa.minimize())

class AutomataTestCase(unittest.TestCase):

    # (pattern, should match, should not match)
    __test_data = [
      ("a", ["a"], ["b", ""]),
      ("ab", ["ab"], ["bb", ""]),
      ("a+b", ["ab", "aab", "aaab"], ["a", "b", ""]),
      ("a?b", ["ab", "b"], ["a", "c", ""]),
      ("a*b", ["ab", "aaab", "b"], ["a", "c", ""]),
      ("a|b", ["a", "b"], ["ab", "c", ""]),
      (".", ["a", "b"], ["", "aa"]),
      (".*", ["", "a", "abcaabbcc"], []),
      ("a.b", ["aab", "abb", "acb"], ["ab", ""]),
      ("a.?b", ["aab", "abb", "acb", "ab"], ["aaab", ""]),
      ("a.+b", ["aab", "abb", "acb"], ["aaac", "ab", ""]),
      (".|.", ["a", "b"], ["aa", ""]),
      ("//.", ["//a"], ["aa", ""]),
      ("[ab]{2}", ["aa", "ab", "ba", "bb"], ["", "a", "b", "aaa", "bbb"]),
      ("[ab]{2,3}", ["aa", "ab", "ba", "bb", "aab", "baa", "bbb"],
       ["", "a", "b", "aaaa", "bbba"]),
      ("[ab]{2,4}", ["aa", "ab", "ba", "bb", "aab", "baa", "bbb", "abab"],
       ["", "a", "b", "aaaba", "bbbaa"]),
      ("[\\101]", ["A"], ["B"])
    ]

    def test_matches(self):
      for (regex, matches, not_matches) in self.__test_data:
        automata = build_automata(regex)
        for string in matches:
          for automaton in automata:
            self.assertTrue(automaton.matches(string))
        for string in not_matches:
          for automaton in automata:
            self.assertFalse(automaton.matches(string))

    def test_can_construct_dot(self):
      for (regex, matches, not_matches) in self.__test_data:
        for automaton in build_automata(regex):
          automaton.to_dot()

    def test_actions(self):
      left_action = Action("LEFT_ACTION")
      right_action = Action("RIGHT_ACTION")
      left = RegexParser.parse("left")
      right = RegexParser.parse("right")
      left = NfaBuilder.add_action(left, left_action)
      right = NfaBuilder.add_action(right, right_action)
      composite = ('ONE_OR_MORE', NfaBuilder.or_graphs([left, right]))
      nfa = NfaBuilder().nfa(composite)
      dfa = dfa_from_nfa(nfa)
      def verify(string, expected):
        actions = list(dfa.collect_actions(string))
        self.assertEqual(len(expected), len(actions))
        for i, action in enumerate(actions):
          self.assertEqual(action, expected[i])
      def verify_miss(string, expected):
        verify(string, expected + [Action('MISS',)])
      def verify_hit(string, expected):
        verify(string, expected + [Action('TERMINATE',)])
      (l, r) = left_action, right_action
      verify_hit("left", [l])
      verify_miss("lefta", [l])
      verify_hit("leftrightleftright", [l, r, l, r])
      verify_miss("leftrightleftrightx", [l, r, l, r])
