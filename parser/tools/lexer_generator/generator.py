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
from nfa import Nfa
from nfa_builder import NfaBuilder
from dfa import Dfa
from rule_parser import RuleParser, RuleParserState, RuleProcessor
from code_generator import CodeGenerator

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

def generate_html(rule_processor):
  scripts = []
  loads = []
  for i, (name, automata) in enumerate(list(rule_processor.automata_iter())):
    (nfa, dfa) = (automata.nfa(), automata.dfa())
    mdfa = None if name == 'default' else automata.minimal_dfa()
    (nfa_i, dfa_i, mdfa_i) = ("nfa_%d" % i, "dfa_%d" % i, "mdfa_%d" % i)
    scripts.append(script_template % (nfa_i, nfa.to_dot()))
    loads.append(load_template % ("nfa [%s]" % name, nfa_i))
    scripts.append(script_template % (dfa_i, dfa.to_dot()))
    loads.append(load_template % ("dfa [%s]" % name, dfa_i))
    if mdfa and mdfa != dfa:
      scripts.append(script_template % (mdfa_i, mdfa.to_dot()))
      loads.append(load_template % ("mdfa [%s]" % name, mdfa_i))
    body = "\n".join(scripts) + (load_outer_template % "\n".join(loads))
  return file_template % body

def generate_code(rule_processor):
  return CodeGenerator.rule_processor_to_code(rule_processor)

def lex(rule_processor, string):
  for t in rule_processor.lex(string + '\0'):
    print t

if __name__ == '__main__':

  parser = argparse.ArgumentParser()
  parser.add_argument('--html')
  parser.add_argument('--re', default='src/lexer/lexer_py.re')
  parser.add_argument('--input')
  parser.add_argument('--code')
  args = parser.parse_args()

  re_file = args.re
  print "parsing %s" % re_file
  with open(re_file, 'r') as f:
    rule_processor = RuleProcessor.parse(f.read(), False)

  html_file = args.html
  if html_file:
    html = generate_html(rule_processor)
    with open(args.html, 'w') as f:
      f.write(html)
      print "wrote html to %s" % html_file

  code_file = args.code
  if code_file:
    code = generate_code(rule_processor)
    with open(code_file, 'w') as f:
      f.write(code)
      print "wrote code to %s" % code_file

  input_file = args.input
  if input_file:
    with open(input_file, 'r') as f:
      lex(rule_processor, f.read())
