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
from action import Term, Action
from transition_keys import TransitionKey

class AutomatonState(object):
  '''A base class for dfa and nfa states.  Immutable'''

  __node_number_counter = 0

  def __init__(self):
    self.__node_number = AutomatonState.__node_number_counter
    AutomatonState.__node_number_counter += 1

  def __hash__(self):
    return hash(self.__node_number)

  def __eq__(self, other):
    return (isinstance(other, self.__class__) and
            self.__node_number == other.__node_number)

  def node_number(self):
    return self.__node_number

  def __str__(self):
    return "%s(%d)" % (type(self), self.node_number())

  __pass = lambda x : True

  def key_iter(self, key_filter = __pass):
    return self.key_state_iter(
      key_filter = key_filter, yield_func = lambda x, y: x)

  def state_iter(self, key_filter = __pass, state_filter = __pass):
    return self.key_state_iter(
      key_filter = key_filter, state_filter = state_filter,
      yield_func = lambda x, y: y)

  def transition_state_iter_for_char(self, value):
    return self.key_state_iter(
      match_func = lambda k, v : k.matches_char(value),
      yield_func = lambda x, y: y)

  def transition_state_iter_for_key(self, value):
    return self.key_state_iter(
      match_func = lambda k, v : k.is_superset_of_key(value),
      yield_func = lambda x, y: y)

class Automaton(object):

  def __init__(self, encoding):
    self.__encoding = encoding

  def encoding(self):
    return self.__encoding

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

  def start_set(self):
    return set([self.start_state()])

  def visit_all_states(self, visitor, visit_state = None, state_iter = None):
    return self.visit_states(self.start_set(), visitor, visit_state, state_iter)

  @staticmethod
  def epsilon_closure(states):
    f = lambda state : state.epsilon_closure_iter()
    return set(chain(iter(states), *map(f, states)))

  @staticmethod
  def __transition_states_for_char(valid_states, c):
    f = lambda state : state.transition_state_iter_for_char(c)
    return set(chain(*map(f, Automaton.epsilon_closure(valid_states))))

  def matches(self, string):
    valid_states = self.start_set()
    for c in string:
      valid_states = Automaton.__transition_states_for_char(valid_states, c)
      if not valid_states:
        return False
    valid_states = self.epsilon_closure(valid_states)
    return len(self.terminal_set().intersection(valid_states)) > 0

  def lex(self, string, default_action):
    last_position = 0
    valid_states = self.start_set()
    for pos, c in enumerate(string):
      transitions = Automaton.__transition_states_for_char(valid_states, c)
      if transitions:
        valid_states = transitions
        continue
      action = Action.dominant_action(valid_states)
      if not action:
        assert default_action
        action = default_action
      yield (action, last_position, pos)
      last_position = pos
      # lex next token
      valid_states = Automaton.__transition_states_for_char(self.start_set(), c)
      assert valid_states
    valid_states = self.epsilon_closure(valid_states)
    action = Action.dominant_action(valid_states)
    if not action:
      assert default_action
      action = default_action
    yield (action, last_position, len(string))

  def to_dot(self):

    def escape(v):
      v = str(v)
      v = v.replace('\r', '\\\\r').replace('\t', '\\\\t').replace('\n', '\\\\n')
      v = v.replace('\\', '\\\\').replace('\"', '\\\"')
      return v

    def f(node, (node_content, edge_content)):
      if node.action():
        action_text = escape(node.action())
        node_content.append('  S_l%s[shape = box, label="%s"];' %
                            (node.node_number(), action_text))
        node_content.append('  S_%s -> S_l%s [arrowhead = none];' %
                            (node.node_number(), node.node_number()))
      for key, state in node.key_state_iter():
        if key == TransitionKey.epsilon():
          key = "&epsilon;"
        else:
          key = key.to_string(self.encoding())
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
