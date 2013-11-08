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
from automaton import *
from inspect import getmembers

class NfaState(AutomatonState):

  def __init__(self, node_number):
    super(NfaState, self).__init__(node_number)
    self.__transitions = {}
    self.__unclosed = set()
    self.__epsilon_closure = None
    self.__transition_action = None

  def epsilon_closure(self):
    return self.__epsilon_closure

  def set_epsilon_closure(self, closure):
    assert self.is_closed()
    assert self.__epsilon_closure == None
    self.__epsilon_closure = frozenset(closure)

  def set_transition_action(self, action):
    assert not self.is_closed()
    assert self.__transition_action == None
    self.__transition_action = action

  def transitions(self):
    assert self.is_closed()
    return self.__transitions

  def next_states(self, key_filter):
    assert self.is_closed()
    first = lambda v: [x[0] for x in v]
    f = lambda acc, (k, v) : acc if not key_filter(k) else acc | set(first(v))
    return reduce(f, self.__transitions.items(), set())

  def __add_transition(self, key, action, next_state):
    if next_state == None:
      assert not action
      assert not self.is_closed(), "already closed"
      self.__unclosed.add(key)
      return
    if not key in self.__transitions:
      self.__transitions[key] = set()
    self.__transitions[key].add((next_state, action))

  def add_unclosed_transition(self, key):
    assert key != TransitionKey.epsilon()
    self.__add_transition(key, None, None)

  def add_epsilon_transition(self, state):
    assert state != None
    self.__add_transition(TransitionKey.epsilon(), None, state)

  def is_closed(self):
    return self.__unclosed == None

  def close(self, end):
    assert not self.is_closed()
    unclosed, self.__unclosed = self.__unclosed, None
    action, self.__transition_action = self.__transition_action, None
    if end == None:
      assert not unclosed and not action
      return
    for key in unclosed:
      self.__add_transition(key, action, end)
    if not unclosed:
      self.__add_transition(TransitionKey.epsilon(), action, end)

  def __matches(self, match_func, value):
    f = lambda acc, (k, vs): acc | vs if match_func(k, value) else acc
    return reduce(f, self.__transitions.items(), set())

  def char_matches(self, value):
    return self.__matches(lambda k, v : k.matches_char(v), value)

  def key_matches(self, value):
    return self.__matches(lambda k, v : k.matches_key(v), value)

  def __str__(self):
    return "NfaState(" + str(self.node_number()) + ")"

  @staticmethod
  def gather_transition_keys(state_set):
    f = lambda acc, state: acc | set(state.__transitions.keys())
    return TransitionKey.disjoint_keys(reduce(f, state_set, set()))

class NfaBuilder:

  def __init__(self):
    self.__node_number = 0
    self.__operation_map = {}
    self.__members = getmembers(self)
    self.__character_classes = {}
    self.__states = []

  def set_character_classes(self, classes):
    self.__character_classes = classes

  def __new_state(self):
    self.__node_number += 1
    return NfaState(self.__node_number - 1)

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

  def __repeat(self, graph):
    param_min = int(graph[1])
    param_max = int(graph[2])
    subgraph = graph[3]
    (start, ends) = self.__process(subgraph)
    for i in xrange(1, param_min):
      (start2, ends2) = self.__process(subgraph)
      self.__patch_ends(ends, start2)
      ends = ends2
    if param_min == param_max:
      return (start, ends)

    midpoints = []
    for i in xrange(param_min, param_max):
      midpoint =  self.__new_state()
      self.__patch_ends(ends, midpoint)
      (start2, ends) = self.__process(subgraph)
      midpoint.add_epsilon_transition(start2)
      midpoints.append(midpoint)

    return (start, ends + midpoints)

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
    return self.__key_state(
      TransitionKey.character_class(graph, self.__character_classes))

  def __not_class(self, graph):
    return self.__key_state(
      TransitionKey.character_class(graph, self.__character_classes))

  def __any(self, graph):
    return self.__key_state(TransitionKey.any())

  def __action(self, graph):
    incoming = graph[3]
    (start, ends) = self.__process(graph[1])
    action = graph[2]
    if incoming:
      new_start = self.__new_state()
      new_start.set_transition_action(action)
      new_start.close(start)
      start = new_start
    else:
      new_end = self.__new_state()
      for end in ends:
        end.set_transition_action(action)
      self.__patch_ends(ends, new_end)
      ends = [new_end]
    return (start, ends)

  def __continue(self, graph):
    (start, ends) = self.__process(graph[1])
    state = self.__peek_state()
    if not state['start_node']:
      state['start_node'] = self.__new_state()
    end = self.__new_state()
    self.__patch_ends(ends, end)
    ends = [end]
    end.add_epsilon_transition(state['start_node'])
    return (start, ends)

  def __join(self, graph):
    (graph, name, subgraph) = graph[1:]
    subgraphs = self.__peek_state()['subgraphs']
    if not name in subgraphs:
      subgraphs[name] = self.__nfa(subgraph)
    (subgraph_start, subgraph_end, nodes_in_subgraph) = subgraphs[name]
    (start, ends) = self.__process(graph)
    self.__patch_ends(ends, subgraph_start)
    end = self.__new_state()
    subgraph_end.add_epsilon_transition(end)
    return (start, [end])

  def __process(self, graph):
    assert type(graph) == TupleType
    method = "_NfaBuilder__" + graph[0].lower()
    if not method in self.__operation_map:
      matches = filter(lambda (name, func): name == method, self.__members)
      assert len(matches) == 1
      self.__operation_map[method] = matches[0][1]
    return self.__operation_map[method](graph)

  def __patch_ends(self, ends, new_end):
    for end in ends:
      end.close(new_end)

  def __push_state(self):
    self.__states.append({
      'start_node' : None,
      'subgraphs' : {},
    })

  def __pop_state(self):
    return self.__states.pop()

  def __peek_state(self):
    return self.__states[len(self.__states) - 1]

  def __nfa(self, graph):
    start_node_number = self.__node_number
    self.__push_state()
    (start, ends) = self.__process(graph)
    state = self.__pop_state()
    if state['start_node']:
      state['start_node'].close(start)
      start = state['start_node']
    for k, subgraph in state['subgraphs'].items():
      subgraph[1].close(None)
    end =  self.__new_state()
    self.__patch_ends(ends, end)
    return (start, end, self.__node_number - start_node_number)

  def nfa(self, graph):
    (start, end, nodes_created) = self.__nfa(graph)
    end.close(None)
    return Nfa(start, end, nodes_created)

  @staticmethod
  def add_action(graph, action):
    return ('ACTION', graph, action, False)

  @staticmethod
  def add_incoming_action(graph, action):
    return ('ACTION', graph, action, True)

  @staticmethod
  def add_continue(graph):
    return ('CONTINUE', graph)

  @staticmethod
  def join_subgraph(graph, name, subgraph):
    return ('JOIN', graph, name, subgraph)

  @staticmethod
  def or_graphs(graphs):
    return reduce(lambda acc, g: ('OR', acc, g), graphs)

  @staticmethod
  def cat_graphs(graphs):
    return reduce(lambda acc, g: ('CAT', acc, g), graphs)

  __modifer_map = {
    '+': 'ONE_OR_MORE',
    '?': 'ZERO_OR_ONE',
    '*': 'ZERO_OR_MORE',
  }

  @staticmethod
  def apply_modifier(modifier, graph):
    return (NfaBuilder.__modifer_map[modifier], graph)

class Nfa(Automaton):

  def __init__(self, start, end, nodes_created):
    super(Nfa, self).__init__()
    self.__start = start
    self.__end = end
    self.__epsilon_closure_computed = False
    self.__verify(nodes_created)

  def __visit_all_edges(self, visitor, state):
    edge = set([self.__start])
    next_edge = lambda node: node.next_states(lambda x : True)
    return self.visit_edges(edge, next_edge, visitor, state)

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
      is_epsilon = lambda k: k == TransitionKey.epsilon()
      next_edge = lambda node : node.next_states(is_epsilon)
      edge = next_edge(node)
      closure = self.visit_edges(edge, next_edge, inner, set())
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
      transitions = reduce(f, valid_states, set())
      if not transitions:
        return False
      valid_states = Nfa.__close(set([x[0] for x in transitions]))
    return self.__end in valid_states

  @staticmethod
  def __to_dfa(nfa_state_set, dfa_nodes, end_node):
    nfa_state_set = Nfa.__close(nfa_state_set)
    assert nfa_state_set
    name = ".".join(str(x.node_number()) for x in sorted(nfa_state_set))
    if name in dfa_nodes:
      return name
    dfa_nodes[name] = {
      'transitions': {},
      'terminal': end_node in nfa_state_set}
    for key in NfaState.gather_transition_keys(nfa_state_set):
      f = lambda acc, state: acc | state.key_matches(key)
      transitions = reduce(f, nfa_state_set, set())
      match_states = set()
      actions = []
      for (state, action) in transitions:
        match_states.add(state)
        if action:
          actions.append(action)

        # Pull in actions which can be taken with an epsilon transition from the
        # match state.
        e = TransitionKey.epsilon()
        if e in state.transitions():
          for e_trans in state.transitions()[e]:
            if e_trans[1]:
              actions.append(e_trans[1])
        for s in state.epsilon_closure():
          if e in s.transitions():
            for e_trans in s.transitions()[e]:
              if e_trans[1]:
                actions.append(e_trans[1])

      assert len(match_states) == len(transitions)

      actions.sort()
      action = actions[0] if actions else None
      transition_state = Nfa.__to_dfa(match_states, dfa_nodes, end_node)
      dfa_nodes[name]['transitions'][key] = (transition_state, action)
    return name

  def compute_dfa(self):
    self.__compute_epsilon_closures()
    dfa_nodes = {}
    start_name = self.__to_dfa(set([self.__start]), dfa_nodes, self.__end)
    return (start_name, dfa_nodes)

  def to_dot(self):
    iterator = lambda visitor, state: self.__visit_all_edges(visitor, state)
    return self.generate_dot(self.__start, set([self.__end]), iterator)
