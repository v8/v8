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

from nfa import Nfa
from transition_keys import TransitionKey

class DfaState:

  def __init__(self, name, node_number):
    self.__name = name
    self.__node_number = node_number
    self.__transitions = {}

  def name(self):
    return self.__name

  def node_number(self):
    return self.__node_number

  def add_transition(self, key, action, state):
    assert not self.__transitions.has_key(key)
    self.__transitions[key] = (state, action)

  def set_action(self, action):
    assert self.__action == None
    self.__action = action

  def transitions(self):
    return self.__transitions

class Dfa:

  def __init__(self, start_name, mapping):
    self.__terminal_set = set()
    name_map = {}
    action_map = {}
    for i, name in enumerate(mapping.keys()):
      name_map[name] = DfaState(name, i)
    for name, node_data in mapping.items():
      node = name_map[name]
      if node_data['terminal']:
        self.__terminal_set.add(node)
      inversion = {}
      for key, (state, action) in node_data['transitions'].items():
        if not state in inversion:
          inversion[state] = {}
        # TODO fix this
        action_key = str(action)
        if not action_key in action_map:
          action_map[action_key] = action
        if not action_key in inversion[state]:
          inversion[state][action_key] = []
        inversion[state][action_key].append(key)
      for state, values in inversion.items():
        for action_key, keys in values.items():
          merged_key = TransitionKey.merged_key(keys)
          action = action_map[action_key]
          node.add_transition(merged_key, action, name_map[state])
    self.__start = name_map[start_name]
    assert self.__terminal_set

  @staticmethod
  def __visit_edges(start, visitor, state):
    edge = set([start])
    visited = set()
    first = lambda v: [x[0] for x in v]
    while edge:
      f = lambda (next_edge, state), node: (
        next_edge | set(first(node.transitions().values())),
        visitor(node, state))
      (next_edge, state) = reduce(f, edge, (set(), state))
      visited |= edge
      edge = next_edge - visited
    return state

  def collect_actions(self, string):
    state = self.__start
    for c in string:
      next = [s for k, s in state.transitions().items() if k.matches_char(c)]
      if not next:
        yield ('MISS',)
        return
      assert len(next) == 1
      (state, action) = next[0]
      if action:
        yield action
    if state in self.__terminal_set:
      yield ('TERMINATE', )
    else:
      yield ('MISS',)

  def matches(self, string):
    actions = list(self.collect_actions(string))
    return actions and actions[-1][0] == 'TERMINATE'

  def to_dot(self):

    def f(node, node_content):
      for key, (state, action) in node.transitions().items():
        node_content.append(
          "  S_%s -> S_%s [ label = \"%s\" ];" %
            (node.node_number(), state.node_number(), key))
      return node_content

    node_content = self.__visit_edges(self.__start, f, [])
    terminals = ["S_%d;" % x.node_number() for x in self.__terminal_set]
    start_number = self.__start.node_number()
    start_shape = "circle"
    if self.__start in self.__terminal_set:
      start_shape = "doublecircle"

    return '''
digraph finite_state_machine {
  rankdir=LR;
  node [shape = %s, style=filled, bgcolor=lightgrey]; S_%s
  node [shape = doublecircle, style=unfilled]; %s
  node [shape = circle];
%s
}
    ''' % (start_shape,
           start_number,
           " ".join(terminals),
           "\n".join(node_content))
