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

import argparse
from nfa import Nfa, NfaBuilder
from dfa import Dfa
from rule_parser import RuleParser, RuleParserState

# FIXME: We need to move this to a common place!
def process_rules(parser_state):
  dfas = {}
  builder = NfaBuilder()
  builder.set_character_classes(parser_state.character_classes)
  for k, v in parser_state.rules.items():
    graphs = []
    for (graph, action) in v['regex']:
      graphs.append(NfaBuilder.add_action(graph, action))
    nfa = builder.nfa(NfaBuilder.or_graphs(graphs))
    (start_name, dfa_nodes) = nfa.compute_dfa()
    dfas[k] = Dfa(start_name, dfa_nodes)
  return dfas

# Lexes strings with the help of DFAs procuded by the grammar. For sanity
# checking the automata.
class Lexer(object):

  def __init__(self, rules):
    parser_state = RuleParserState()
    RuleParser.parse(rules, parser_state)
    self.dfas = process_rules(parser_state)

  def lex(self, string):
    dfa = self.dfas['default'] # FIXME

    action_stream = []
    terminate_seen = False
    offset = 0
    while not terminate_seen and string:
      result = list(dfa.lex(string))
      last_position = 0
      for (action, position) in result:
        action_stream.append((action[1], action[2], last_position + offset, position + 1 + offset, string[last_position:(position + 1)]))
        last_position = position
        if action[2] == 'terminate':
          terminate_seen = True
      string = string[(last_position + 1):]
      offset += last_position
    return action_stream

if __name__ == '__main__':

  parser = argparse.ArgumentParser()
  parser.add_argument('--rules')
  parser.add_argument('--input')
  args = parser.parse_args()

  re_file = args.rules
  input_file = args.input

  with open(re_file, 'r') as f:
    rules = f.read()
  with open(input_file, 'r') as f:
    input_text = f.read() + '\0'

  lexer = Lexer(rules)
  for t in lexer.lex(input_text):
    print t
