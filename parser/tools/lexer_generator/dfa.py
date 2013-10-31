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

class DfaState:

  def __init__(self, name, node_number):
    self.__name = name
    self.__node_number = node_number
    self.__transitions = {}

  def name(self):
    return self.__name

  def node_number(self):
    return self.__node_number

  def add_transition(self, key, state):
    assert not self.__transitions.has_key(key)
    self.__transitions[key] = state

  def transitions(self):
    return self.__transitions

class Dfa:

  def __init__(self, start_name, mapping, end_names):
    name_map = {}
    offset = 0
    self.__terminal_set = set()
    for name in mapping.keys():
      dfa_state = DfaState(name, offset)
      name_map[name] = dfa_state
      offset += 1
      if name in end_names:
        self.__terminal_set.add(dfa_state)
    for name, values in mapping.items():
      for key, value in values.items():
        name_map[name].add_transition(key, name_map[value])
    self.__start = name_map[start_name]
    assert self.__terminal_set

  @staticmethod
  def __visit_edges(start, visitor, state):
    edge = set([start])
    visited = set()
    while edge:
      f = lambda (next_edge, state), node: (
        next_edge | set(node.transitions().values()),
        visitor(node, state))
      (next_edge, state) = reduce(f, edge, (set(), state))
      visited |= edge
      edge = next_edge - visited
    return state

  def to_dot(self):

    def f(node, node_content):
      for key, value in node.transitions().items():
        node_content.append(
          "  S_%s -> S_%s [ label = \"%s\" ];" %
            (node.node_number(), value.node_number(), key))
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
