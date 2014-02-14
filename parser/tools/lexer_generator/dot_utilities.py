# Copyright 2014 the V8 project authors. All rights reserved.
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

from types import IntType, StringType
from encoding import KeyEncoding
from action import Term
from transition_keys import TransitionKey

def escape_for_dot(v):
  v = str(v)
  v = v.replace('\r', '\\\\r').replace('\t', '\\\\t').replace('\n', '\\\\n')
  v = v.replace('\\', '\\\\').replace('\"', '\\\"')
  return v

def map_characters(encoding, term):
  if term.name() == 'LITERAL':
    f = lambda x : KeyEncoding.to_str(encoding, x)
    term = Term('LITERAL', "'%s'" % ''.join(map(f, term.args())))
  elif term.name() == 'RANGE':
    f = lambda x : "'%s'" % KeyEncoding.to_str(encoding, x)
    term = Term('RANGE', *map(f, term.args()))
  return term

def term_to_dot(term, term_mapper = None):
  node_ix = [0]
  node_template = 'node [label="%s"]; N_%d;'
  edge_template = 'N_%d -> N_%d'
  nodes = []
  edges = []

  def process(term):
    if type(term) == StringType or type(term) == IntType:
      node_ix[0] += 1
      nodes.append(node_template % (escape_for_dot(str(term)), node_ix[0]))
      return node_ix[0]
    elif isinstance(term, Term):
      term = term if not term_mapper else term_mapper(term)
      child_ixs = map(process, term.args())
      node_ix[0] += 1
      nodes.append(node_template % (escape_for_dot(term.name()), node_ix[0]))
      for child_ix in child_ixs:
        edges.append(edge_template % (node_ix[0], child_ix))
      return node_ix[0]
    raise Exception

  process(term)
  return 'digraph { %s %s }' % ('\n'.join(nodes), '\n'.join(edges))

def automaton_to_dot(automaton):

  def f(node, (node_content, edge_content)):
    if node.action():
      action_text = escape_for_dot(node.action())
      node_content.append('  S_l%s[shape = box, label="%s"];' %
                          (node.node_number(), action_text))
      node_content.append('  S_%s -> S_l%s [arrowhead = none];' %
                          (node.node_number(), node.node_number()))
    for key, state in node.key_state_iter():
      if key == TransitionKey.epsilon():
        key = "&epsilon;"
      elif key == TransitionKey.omega():
        key = "&omega;"
      else:
        key = key.to_string(automaton.encoding())
      edge_content.append("  S_%s -> S_%s [ label = \"%s\" ];" % (
          node.node_number(), state.node_number(), escape_for_dot(key)))
    return (node_content, edge_content)

  (node_content, edge_content) = automaton.visit_all_states(f, ([], []))

  start_set = automaton.start_set()
  assert len(start_set) == 1
  start_node = iter(start_set).next()
  terminal_set = automaton.terminal_set()

  terminals = ["S_%d;" % x.node_number() for x in terminal_set]
  start_number = start_node.node_number()
  start_shape = "circle"
  if start_node in terminal_set:
    start_shape = "doublecircle"

  return '''
digraph finite_state_machine {
  rankdir=LR;
  node [shape = %s, style=filled, bgcolor=lightgrey]; S_%s
  node [shape = doublecircle, style=unfilled]; %s
  node [shape = circle];
  %s
  %s
}
  ''' % (start_shape,
         start_number,
         " ".join(terminals),
         "\n".join(edge_content),
         "\n".join(node_content))
