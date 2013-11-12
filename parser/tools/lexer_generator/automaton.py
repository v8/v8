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

from types import TupleType, ListType
from itertools import chain
from transition_keys import TransitionKey

class Action(object):
  pass

class AutomatonState(object):

  def __init__(self, node_number):
    self.__node_number = node_number

  def node_number(self):
    return self.__node_number

  def __str__(self):
    return "%s(%d)" % (type(self), self.node_number())

  __pass = lambda x : True

  def key_iter(self, key_filter = __pass):
    for k in self.transitions().keys():
      if key_filter(k): yield k

  def state_iter(self, key_filter = __pass, state_filter = __pass):
    return self.key_state_iter(key_filter, state_filter, lambda x, y: y)

  def key_state_iter(
    self,
    key_filter = __pass,
    state_filter = __pass,
    yield_func = lambda x, y : (x, y)):
    for key, states in self.transitions().items():
      if key_filter(key):
        if not self.transitions_to_multiple_states():
          if state_filter(states):
            yield yield_func(key, states)
        else:
          for state in states:
            if state_filter(state):
              yield yield_func(key, state)

class Automaton(object):

  @staticmethod
  def visit_states(edge, visitor, visit_state = None, state_iter = None):
    if not state_iter:
      state_iter  = lambda node: node.state_iter()
    visited = set()
    while edge:
      next_edge_iters = []
      def f(visit_state, node):
        next_edge_iters.append(state_iter(node))
        return visitor(node, visit_state)
      visit_state = reduce(f, edge, visit_state)
      next_edge = set(chain(*next_edge_iters))
      visited |= edge
      edge = next_edge - visited
    return visit_state

  def visit_all_states(self, visitor, visit_state = None, state_iter = None):
    return self.visit_states(self.start_set(), visitor, visit_state, state_iter)

  def to_dot(self):

    def escape(v):
      v = str(v).replace('\r', '\\\\r')
      v = str(v).replace('\t', '\\\\t')
      v = str(v).replace('\n', '\\\\n')
      v = str(v).replace('\\', '\\\\')
      v = str(v).replace('\"', '\\\"')
      return v

    def f(node, (node_content, edge_content)):
      if node.action():
        action_text = node.action()[1].split('\n')[0]
        node_content.append('  S_l%s[shape = box, label="%s"];' %
                            (node.node_number(), action_text))
        node_content.append('  S_%s -> S_l%s [arrowhead = none];' %
                            (node.node_number(), node.node_number()))
      for key, state in node.key_state_iter():
        if key == TransitionKey.epsilon():
          key = "&epsilon;"
        edge_content.append("  S_%s -> S_%s [ label = \"%s\" ];" % (
            node.node_number(), state.node_number(), escape(key)))
      return (node_content, edge_content)

    (node_content, edge_content) = self.visit_all_states(f, ([], []))

    start_set = self.start_set()
    assert len(start_set) == 1
    start_node = iter(start_set).next()
    terminal_set = self.terminal_set()

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
