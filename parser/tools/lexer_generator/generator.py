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

file_template = '''
<html>
  <head>
    <script src="viz.js"></script>
    <script>
      function draw(name, id) {
        code = document.getElementById(id).innerHTML
        document.body.innerHTML += "<h1>" + name + "</h1>";
        try {
          document.body.innerHTML += Viz(code, 'svg');
        } catch(e) {
          document.body.innerHTML += "<h3>error</h3>";
        }
      }
    </script>
  </head>
  <body>
%s
  </body>
</html>'''

script_template = '''    <script type="text/vnd.graphviz" id="%s">
%s
    </script>
'''

load_template = '''      draw('%s', '%s');'''

load_outer_template = '''    <script>
%s
    </script>'''

class Generator(object):

  def __init__(self, rules):
    parser_state = RuleParserState()
    RuleParser.parse(rules, parser_state)
    self.__automata = {}
    self.process_rules(parser_state)

  def generate_html(self):
    scripts = []
    loads = []
    for i, name in enumerate(self.__automata):
      (nfa, dfa) = self.__automata[name]
      if name == 'Normal': continue
      (nfa_i, dfa_i) = ("nfa_%d" % i, "dfa_%d" % i)
      scripts.append(script_template % (nfa_i, nfa.to_dot()))
      scripts.append(script_template % (dfa_i, dfa.to_dot()))
      loads.append(load_template % ("nfa [%s]" % name, nfa_i))
      loads.append(load_template % ("dfa [%s]" % name, dfa_i))
      body = "\n".join(scripts) + (load_outer_template % "\n".join(loads))
    return file_template % body

  def process_rules(self, parser_state):
    rule_map = {}
    builder = NfaBuilder()
    builder.set_character_classes(parser_state.character_classes)
    assert 'default' in parser_state.rules
    def process(k, v):
      assert 'default' in v
      graphs = []
      for (graph, action) in v['regex']:
        (precedence, code, transition) = action
        if code:
          graph = NfaBuilder.add_action(graph, (precedence, code, None))
        if transition == 'continue':
          if not v['default'][1][2] == 'continue':
            graph = NfaBuilder.add_continue(graph)
          else:
            pass # TODO null key
        elif (transition == 'break' or
              transition == 'terminate' or
              transition == 'terminate_illegal'):
          graph = NfaBuilder.add_action(graph, (10000, transition, None))
        else:
          assert k == 'default'
          graph = NfaBuilder.join_subgraph(graph, transition, rule_map[transition])
        graphs.append(graph)
      graph = NfaBuilder.or_graphs(graphs)
      # merge default action
      (precedence, code, transition) = v['default'][1]
      assert transition == 'continue' or transition == 'break'
      if transition == 'continue':
        assert k != 'default'
        graph = NfaBuilder.add_incoming_action(graph, (10000, k, None))
      if code:
        graph = NfaBuilder.add_incoming_action(graph, (precedence, code, None))
      rule_map[k] = graph
    for k, v in parser_state.rules.items():
      if k == 'default': continue
      process(k, v)
    process('default', parser_state.rules['default'])
    for rule_name, graph in rule_map.items():
      nfa = builder.nfa(graph)
      (start, dfa_nodes) = nfa.compute_dfa()
      dfa = Dfa(start, dfa_nodes)
      self.__automata[rule_name] = (nfa, dfa)

  # Lexes strings with the help of DFAs procuded by the grammar. For sanity
  # checking the automata.
  def lex(self, string):
    (nfa, dfa) = self.__automata['default'] # FIXME

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
  parser.add_argument('--html')
  parser.add_argument('--re', default='src/lexer/lexer_py.re')
  parser.add_argument('--input')
  args = parser.parse_args()

  re_file = args.re
  parser_state = RuleParserState()
  print "parsing %s" % re_file
  with open(re_file, 'r') as f:
    generator = Generator(f.read())

  html_file = args.html
  if html_file:
    html = generator.generate_html()
    with open(args.html, 'w') as f:
      f.write(html)
      print "wrote html to %s" % html_file

  input_file = args.input
  if input_file:
    with open(input_file, 'r') as f:
      input_text = f.read() + '\0'
    for t in generator.lex(input_text):
      print t
