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
from regex_parser import RegexParser
from nfa import NfaBuilder
from dfa import Dfa

def build_automata(string):
  parser = RegexParser()
  parser.build()
  graph = parser.parse(string)
  nfa = NfaBuilder().nfa(graph)
  (start_name, dfa_nodes, end_nodes) = nfa.compute_dfa()
  dfa = Dfa(start_name, dfa_nodes, end_nodes)
  return (nfa, dfa)

class AutomataTestCase(unittest.TestCase):

    # (pattern, should match, shouldn't match)
    __test_data = [
      ("a", ["a"], ["b"]),
      ("ab", ["ab"], ["bb"]),
      ("a+b", ["ab", "aab", "aaab"], ["a", "b"]),
      ("a?b", ["ab", "b"], ["a", "c"]),
      ("a*b", ["ab", "aaab", "b"], ["a", "c"]),
      ("a|b", ["a", "b"], ["ab", "c"]),
    ]

    def test_matches(self):
      for (regex, matches, not_matches) in AutomataTestCase.__test_data:
        (nfa, dfa) = build_automata(regex)
        for string in matches:
          self.assertTrue(nfa.matches(string))
          self.assertTrue(dfa.matches(string))
        for string in not_matches:
          self.assertFalse(nfa.matches(string))
          self.assertFalse(dfa.matches(string))

if __name__ == '__main__':
    unittest.main()
