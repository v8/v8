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

def generate_html(data):
  scripts = []
  loads = []
  for i, (name, nfa, dfa) in enumerate(data):
    if name == 'Normal': continue
    (nfa_i, dfa_i) = ("nfa_%d" % i, "dfa_%d" % i)
    scripts.append(script_template % (nfa_i, nfa.to_dot()))
    scripts.append(script_template % (dfa_i, dfa.to_dot()))
    loads.append(load_template % ("nfa [%s]" % name, nfa_i))
    loads.append(load_template % ("dfa [%s]" % name, dfa_i))
  body = "\n".join(scripts) + (load_outer_template % "\n".join(loads))
  return file_template % body

def process_rules(parser_state):
  rule_map = {}
  builder = NfaBuilder()
  builder.set_character_classes(parser_state.character_classes)
  for k, v in parser_state.rules.items():
    graphs = []
    for (graph, code, action) in v['regex']:
      # graphs.append(NfaBuilder.add_action(graph, (action, identifier)))
      graphs.append(graph)
    rule_map[k] = builder.nfa(NfaBuilder.or_graphs(graphs))
  html_data = []
  for rule_name, nfa in rule_map.items():
    (start, dfa_nodes) = nfa.compute_dfa()
    dfa = Dfa(start, dfa_nodes)
    html_data.append((rule_name, nfa, dfa))
  html = generate_html(html_data)
  # print html

def parse_file(file_name):
  parser_state = RuleParserState()
  with open(file_name, 'r') as f:
    RuleParser.parse(f.read(), parser_state)
  process_rules(parser_state)

if __name__ == '__main__':
  parse_file('src/lexer/lexer_py.re')
