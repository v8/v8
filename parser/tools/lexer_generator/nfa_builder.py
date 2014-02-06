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
from inspect import getmembers
from action import *
from nfa import *

# Nfa is built in two stages:
# 1. Build a tree of operations on rules.
#    Each node in the tree is a tuple (operation, subtree1, ... subtreeN).
#    Rule parser builds this tree by invoking static methods of NfaBuilder.
# 2. For each node, perform the operation of the node to produce an Nfa.
#    If an operation is called X, then it is performed by the method
#    of NfaBuilder called __x(). See __process() for mapping from
#    operation to processing methods.

class NfaBuilder(object):

  def __init__(self, encoding, character_classes, subtree_map):
    self.__node_number = 0
    self.__encoding = encoding
    self.__character_classes = character_classes
    self.__states = []
    self.__global_end_node = None
    self.__operation_map = None
    self.__subtree_map = subtree_map

  def __new_state(self):
    self.__node_number += 1
    return NfaState()

  def __global_end(self):
    if not self.__global_end_node:
      self.__global_end_node = self.__new_state()
    return self.__global_end_node

  def __or(self, *trees):
    start = self.__new_state()
    ends = []
    for tree in trees:
      (sub_start, sub_end) = self.__process(tree)
      start.add_epsilon_transition(sub_start)
      ends += sub_end
    start.close(None)
    return (start, ends)

  def __one_or_more(self, subtree):
    (start, ends) = self.__process(subtree)
    end =  self.__new_state()
    end.add_epsilon_transition(start)
    self.__patch_ends(ends, end)
    return (start, [end])

  def __zero_or_more(self, subtree):
    (node, ends) = self.__process(subtree)
    start =  self.__new_state()
    start.add_epsilon_transition(node)
    self.__patch_ends(ends, start)
    return (start, [start])

  def __zero_or_one(self, subtree):
    (node, ends) = self.__process(subtree)
    start =  self.__new_state()
    start.add_epsilon_transition(node)
    return (start, ends + [start])

  def __repeat(self, param_min, param_max, subtree):
    'process regex of form subtree{param_min, param_max}'
    (param_min, param_max) = (int(param_min), int(param_max))
    assert param_min > 1 and param_min <= param_max
    (start, ends) = self.__process(subtree)
    # create a chain of param_min subtrees
    for i in xrange(1, param_min):
      (start2, ends2) = self.__process(subtree)
      self.__patch_ends(ends, start2)
      ends = ends2
    if param_min == param_max:
      return (start, ends)
    # join in (param_max - param_min) optional subtrees
    midpoints = []
    for i in xrange(param_min, param_max):
      midpoint =  self.__new_state()
      self.__patch_ends(ends, midpoint)
      (start2, ends) = self.__process(subtree)
      midpoint.add_epsilon_transition(start2)
      midpoints.append(midpoint)
    return (start, ends + midpoints)

  def __cat(self, *trees):
    (start, ends) = (None, None)
    for tree in trees:
      (sub_start, sub_ends) = self.__process(tree)
      if start == None:
        start = sub_start
      else:
        assert sub_ends, "this creates unreachable nodes"
        self.__patch_ends(ends, sub_start)
      ends = sub_ends
    return (start, ends)

  def __key_state(self, key):
    state =  self.__new_state()
    state.add_unclosed_transition(key)
    return (state, [state])

  def __literal(self, chars):
    terms = map(lambda c : Term('SINGLE_CHAR', c), chars)
    return self.__process(self.cat_terms(terms))

  def __single_char(self, char):
    return self.__key_state(
      TransitionKey.single_char(self.__encoding, char))

  def __class(self, subtree):
    return self.__key_state(TransitionKey.character_class(
      self.__encoding, Term('CLASS', subtree), self.__character_classes))

  def __not_class(self, subtree):
    return self.__key_state(TransitionKey.character_class(
      self.__encoding, Term('NOT_CLASS', subtree), self.__character_classes))

  def __any(self):
    return self.__key_state(TransitionKey.any(self.__encoding))

  def __action(self, subtree, action_term):
    (start, ends) = self.__process(subtree)
    action = Action.from_term(action_term)
    end = self.__new_state()
    self.__patch_ends(ends, end)
    end.set_action(action)
    # Force all match actions to be terminal.
    if action.match_action():
      global_end = self.__global_end()
      end.add_epsilon_transition(global_end)
    return (start, [end])

  def __continue(self, subtree, depth):
    'add an epsilon transitions to the start node of the current subtree'
    (start, ends) = self.__process(subtree)
    index = -1 - min(int(depth), len(self.__states) - 1)
    state = self.__states[index]
    if not state['start_node']:
      state['start_node'] = self.__new_state()
    self.__patch_ends(ends, state['start_node'])
    return (start, [])

  def __unique_key(self, name):
    return self.__key_state(TransitionKey.unique(name))

  def __join(self, tree, name):
    (subtree_start, subtree_end, nodes_in_subtree) = self.__nfa(name)
    if tree:
      (start, ends) = self.__process(tree)
      self.__patch_ends(ends, subtree_start)
    else:
      start = subtree_start
    if subtree_end:
      return (start, [subtree_end])
    else:
      return (start, [])

  def __get_method(self, name):
    'lazily build map of all private bound methods and return the one requested'
    if not self.__operation_map:
      prefix = "_NfaBuilder__"
      self.__operation_map = {name[len(prefix):] : f for (name, f) in
        filter(lambda (name, f): name.startswith(prefix), getmembers(self))}
    return self.__operation_map[name]

  def __process(self, term):
    assert isinstance(term, Term)
    method = self.__get_method(term.name().lower())
    return method(*term.args())

  def __patch_ends(self, ends, new_end):
    for end in ends:
      end.close(new_end)

  def __push_state(self, name):
    for state in self.__states:
      assert state['name'] != name, 'recursive subgraph'
    self.__states.append({
      'start_node' : None,
      'name' : name
    })

  def __pop_state(self):
    return self.__states.pop()

  def __nfa(self, name):
    tree = self.__subtree_map[name]
    start_node_number = self.__node_number
    self.__push_state(name)
    (start, ends) = self.__process(tree)
    state = self.__pop_state()
    # move saved start node into start
    if state['start_node']:
      state['start_node'].close(start)
      start = state['start_node']
    # Don't create an end node for the subgraph if it would be unused (ends can
    # be an empty list e.g., in the case when everything inside the subgraph is
    # "continue").
    end = None
    if ends:
      end = self.__new_state()
      self.__patch_ends(ends, end)
    return (start, end, self.__node_number - start_node_number)

  @staticmethod
  def __compute_epsilon_closures(start_state):
    def outer(node, state):
      def inner(node, closure):
        closure.add(node)
        return closure
      is_epsilon = lambda k: k == TransitionKey.epsilon()
      state_iter = lambda node : node.state_iter(key_filter = is_epsilon)
      edge = set(state_iter(node))
      closure = Automaton.visit_states(
          edge, inner, state_iter=state_iter, visit_state=set())
      node.set_epsilon_closure(closure)
    Automaton.visit_states(set([start_state]), outer)

  @staticmethod
  def __replace_catch_all(encoding, state):
    catch_all = TransitionKey.unique('catch_all')
    transitions = state.transitions()
    if not catch_all in transitions:
      return
    f = lambda acc, state: acc | set(state.epsilon_closure_iter())
    reachable_states = reduce(f, transitions[catch_all], set())
    f = lambda acc, state: acc | set(state.transitions().keys())
    keys = reduce(f, reachable_states, set())
    keys.discard(TransitionKey.epsilon())
    keys.discard(catch_all)
    keys.discard(TransitionKey.unique('eos'))
    inverse_key = TransitionKey.inverse_key(encoding, keys)
    if inverse_key:
      transitions[inverse_key] = transitions[catch_all]
    del transitions[catch_all]

  @staticmethod
  def nfa(encoding, character_classes, subtree_map, name):
    self = NfaBuilder(encoding, character_classes, subtree_map)
    (start, end, nodes_created) = self.__nfa(name)
    # close all ends
    global_end_to_return = self.__global_end_node or end
    assert global_end_to_return, "no terminal nodes, default tree continues"
    if end and self.__global_end_node:
      end.close(self.__global_end_node)
    global_end_to_return.close(None)
    # Prepare the nfa
    self.__compute_epsilon_closures(start)
    f = lambda node, state: self.__replace_catch_all(self.__encoding, node)
    Automaton.visit_states(set([start]), f)
    return Nfa(self.__encoding, start, global_end_to_return, nodes_created)

  @staticmethod
  def add_action(term, action):
    return Term('ACTION', term, action.to_term())

  @staticmethod
  def add_continue(tree, depth):
    return Term('CONTINUE', tree, depth)

  @staticmethod
  def unique_key(name):
    return Term('UNIQUE_KEY', name)

  @staticmethod
  def join_subtree(tree, subtree_name):
    return Term('JOIN', tree, subtree_name)

  @staticmethod
  def or_terms(terms):
    if len(terms) == 1: return terms[0]
    return Term('OR', *terms) if terms else Term.empty()

  @staticmethod
  def cat_terms(terms):
    if len(terms) == 1: return terms[0]
    return Term('CAT', *terms) if terms else Term.empty()

  __modifer_map = {
    '+': 'ONE_OR_MORE',
    '?': 'ZERO_OR_ONE',
    '*': 'ZERO_OR_MORE',
  }

  @staticmethod
  def apply_modifier(modifier, term):
    return Term(NfaBuilder.__modifer_map[modifier], term)
