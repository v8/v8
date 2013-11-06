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

from nfa import Nfa, NfaBuilder
from dfa import Dfa
from rule_parser import RuleParser, RuleParserState

def process_rules(parser_state):
  rule_map = {}
  builder = NfaBuilder()
  builder.set_character_classes(parser_state.character_classes)
  for k, v in parser_state.rules.items():
    graphs = []
    for (rule_type, graph, identifier, action) in v:
      graphs.append(NfaBuilder.add_action(graph, (action, identifier)))
    rule_map[k] = builder.nfa(NfaBuilder.or_graphs(graphs))
  for rule_name, nfa in rule_map.items():
    # print "Rule %s" % rule_name
    (start, dfa_nodes) = nfa.compute_dfa()
    dfa = Dfa(start, dfa_nodes)
    # print nfa.to_dot()
    # print dfa.to_dot()

def parse_file(file_name):
  parser_state = RuleParserState()
  with open(file_name, 'r') as f:
    RuleParser.parse(f.read(), parser_state)
  process_rules(parser_state)

if __name__ == '__main__':
  parse_file('src/lexer/lexer_py.re')
