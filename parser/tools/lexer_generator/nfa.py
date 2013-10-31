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

from types import TupleType
from transition_keys import TransitionKey
from inspect import getmembers

class NfaState:

  def __init__(self, node_number):
    self.__transitions = {}
    self.__unclosed = set()
    self.__node_number = node_number
    self.__epsilon_closure = None

  def node_number(self):
    return self.__node_number

  def epsilon_closure(self):
    return self.__epsilon_closure

  def set_epsilon_closure(self, closure):
    assert self.is_closed()
    assert self.__epsilon_closure == None
    self.__epsilon_closure = frozenset(closure)

  def transitions(self):
    assert self.is_closed()
    return self.__transitions

  def get_epsilon_transitions(self):
    key = TransitionKey.epsilon();
    if not key in self.__transitions: return frozenset()
    return frozenset(self.__transitions[key])

  def __add_transition(self, key, value):
    if value == None:
      assert not self.is_closed(), "already closed"
      self.__unclosed.add(key)
      return
    if not key in self.__transitions:
      self.__transitions[key] = set()
    self.__transitions[key].add(value)

  def add_unclosed_transition(self, key):
    assert key != TransitionKey.epsilon()
    self.__add_transition(key, None)

  def add_epsilon_transition(self, state):
    assert state != None
    self.__add_transition(TransitionKey.epsilon(), state)

  def is_closed(self):
    return self.__unclosed == None

  def close(self, end):
    assert not self.is_closed()
    if end == None:
      assert not self.__unclosed
    else:
      for key in self.__unclosed:
        self.__add_transition(key, end)
      if not self.__unclosed:
        self.add_epsilon_transition(end)
    self.__unclosed = None

  def __matches(self, match_func, value):
    f = lambda acc, (k, vs): acc | vs if match_func(k, value) else acc
    return reduce(f, self.__transitions.items(), set())

  def char_matches(self, value):
    return self.__matches((lambda k, v : k.matches_char(v)), value)

  def key_matches(self, value):
    return self.__matches((lambda k, v : k.matches_key(v)), value)

  @staticmethod
  def gather_transition_keys(state_set):
    f = lambda acc, state: acc | set(state.__transitions.keys())
    return TransitionKey.merge_key_set(reduce(f, state_set, set()))

class NfaBuilder:

  def __init__(self):
    self.__operation_map = {}
    self.__members = getmembers(self)

  def __new_state(self):
    node_number = self.node_number
    self.node_number = self.node_number + 1
    return NfaState(node_number)

  def __or(self, graph):
    start = self.__new_state()
    ends = []
    for x in [self.__process(graph[1]), self.__process(graph[2])]:
      start.add_epsilon_transition(x[0])
      ends += x[1]
    start.close(None)
    return (start, ends)

  def __one_or_more(self, graph):
    (start, ends) = self.__process(graph[1])
    end =  self.__new_state()
    end.add_epsilon_transition(start)
    self.__patch_ends(ends, end)
    return (start, [end])

  def __zero_or_more(self, graph):
    (node, ends) = self.__process(graph[1])
    start =  self.__new_state()
    start.add_epsilon_transition(node)
    self.__patch_ends(ends, start)
    return (start, [start])

  def __zero_or_one(self, graph):
    (node, ends) = self.__process(graph[1])
    start =  self.__new_state()
    start.add_epsilon_transition(node)
    return (start, ends + [start])

  def __cat(self, graph):
    (left, right) = (self.__process(graph[1]), self.__process(graph[2]))
    self.__patch_ends(left[1], right[0])
    return (left[0], right[1])

  def __key_state(self, key):
    state =  self.__new_state()
    state.add_unclosed_transition(key)
    return (state, [state])

  def __literal(self, graph):
    return self.__key_state(TransitionKey.single_char(graph[1]))

  def __class(self, graph):
    return self.__key_state(TransitionKey.character_class(False, graph[1]))

  def __not_class(self, graph):
    return self.__key_state(TransitionKey.character_class(True, graph[1]))

  def __any(self, graph):
    return self.__key_state(TransitionKey.any())

  def __process(self, graph):
    assert type(graph) == TupleType
    method = "_NfaBuilder__" + graph[0].lower()
    if not method in self.__operation_map:
      matches = filter((lambda (name, func): name == method), self.__members)
      assert len(matches) == 1
      self.__operation_map[method] = matches[0][1]
    return self.__operation_map[method](graph)

  def __patch_ends(self, ends, new_end):
    for end in ends:
      end.close(new_end)

  def nfa(self, graph):
    self.node_number = 0
    (start, ends) = self.__process(graph)
    end =  self.__new_state()
    self.__patch_ends(ends, end)
    end.close(None)
    return Nfa(start, end, self.node_number)

class Nfa:

  def __init__(self, start, end, nodes_created):
    self.__start = start
    self.__end = end
    self.__epsilon_closure_computed = False
    self.__verify(nodes_created)

  @staticmethod
  def __visit_edges(edge, compute_next_edge, visitor, state):
    visited = set()
    while edge:
      f = lambda (next_edge, state), node: (
        next_edge | compute_next_edge(node),
        visitor(node, state))
      (next_edge, state) = reduce(f, edge, (set(), state))
      visited |= edge
      edge = next_edge - visited
    return state

  def __visit_all_edges(self, visitor, state):
    edge = set([self.__start])
    def next_edge(node):
      f = lambda acc, values: acc | values
      return reduce(f, node.transitions().values(), set())
    return Nfa.__visit_edges(edge, next_edge, visitor, state)

  def __verify(self, nodes_created):
    def f(node, node_list):
      assert node.is_closed()
      node_list.append(node)
      return node_list
    node_list = self.__visit_all_edges(f, [])
    assert len(node_list) == nodes_created

  def __compute_epsilon_closures(self):
    if self.__epsilon_closure_computed:
      return
    self.__epsilon_closure_computed = True
    def outer(node, state):
      def inner(node, closure):
        closure.add(node)
        return closure
      next_edge = lambda node : node.get_epsilon_transitions()
      edge = next_edge(node)
      closure = Nfa.__visit_edges(edge, next_edge, inner, set())
      node.set_epsilon_closure(closure)
    self.__visit_all_edges(outer, None)

  @staticmethod
  def __close(states):
    f = lambda acc, node: acc | node.epsilon_closure()
    return reduce(f, states, set(states))

  def matches(self, string):
    self.__compute_epsilon_closures()
    valid_states = Nfa.__close(set([self.__start]))
    for c in string:
      f = lambda acc, state: acc | state.char_matches(c)
      valid_states = Nfa.__close(reduce(f, valid_states, set()))
      if not valid_states:
        return False
    return self.__end in valid_states

  @staticmethod
  def __to_dfa(nfa_state_set, builder):
    nfa_state_set = Nfa.__close(nfa_state_set)
    name = str([x.node_number() for x in nfa_state_set])
    (dfa_nodes, end_nodes, end_node) = builder
    if name in dfa_nodes:
      return name
    dfa_node = {}
    dfa_nodes[name] = dfa_node
    for key in NfaState.gather_transition_keys(nfa_state_set):
      f = lambda acc, state: acc | state.key_matches(key)
      dfa_node[key] = Nfa.__to_dfa(reduce(f, nfa_state_set, set()), builder)
    if end_node in nfa_state_set:
      end_nodes.add(name)
    return name

  def compute_dfa(self):
    self.__compute_epsilon_closures()
    dfa_nodes = {}
    end_nodes = set()
    dfa_builder = (dfa_nodes, end_nodes, self.__end)
    start_name = self.__to_dfa(set([self.__start]), dfa_builder)
    return (start_name, dfa_nodes, end_nodes)

  def to_dot(self):

    def f(node, node_content):
      for key, values in node.transitions().items():
        if key == TransitionKey.epsilon():
          key = "&epsilon;"
        for value in values:
          node_content.append(
            "  S_%d -> S_%d [ label = \"%s\" ];" %
              (node.node_number(), value.node_number(), key))
      return node_content

    node_content = self.__visit_all_edges(f, [])

    return '''
digraph finite_state_machine {
  rankdir=LR;
  node [shape = circle, style=filled, bgcolor=lightgrey]; S_%s
  node [shape = doublecircle, style=unfilled]; S_%s
  node [shape = circle];
%s
}
    ''' % (self.__start.node_number(),
           self.__end.node_number(),
           "\n".join(node_content))
