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

from transition_key import TransitionKey
from automaton import Term, Action, Automaton
from dfa import Dfa

class BacktrackingGenerator(object):

  @staticmethod
  def generate(dfa, default_action):
    return BacktrackingGenerator(dfa, default_action).__generate()

  def __init__(self, dfa, default_action):
    self.__dfa = dfa
    self.__default_action = default_action

  @staticmethod
  def __print_ambiguous_paths(dfa, error_map, action_state_map):
    path_map = { k : [] for k in error_map.iterkeys()}
    error_states = set(error_map.iterkeys())
    for path, states_in_path, terminal in dfa.path_iter():
      matching = error_states & states_in_path
      if not matching:
        continue
      last_action = None
      path_length = 0
      for (key, state) in path:
        if state in error_states:
          assert last_action != None
          path_map[state].append([k for k, s in path[1:path_length]])
        elif state in action_state_map:
          last_action = state
        path_length += 1
    print reduce(lambda acc, x : acc + len(x), path_map.itervalues(), 0)
    for state, key_paths in path_map.iteritems():
      count = 0
      action_str = map(str, error_map[state][0])
      for key_path in key_paths:
        print "path %d %s %s" % (
          state.node_number(),
          action_str,
          " --> ".join(map(str, key_path)))
        count += 1
        if count > 10:
          break

  @staticmethod
  def __compute_action_states(dfa, default_action):
    terminal_set = dfa.terminal_set()
    # Collect states that terminate currently.
    action_states = {}
    omega_states = set()
    def f(state, acc):
      omega_state = state.omega_transition()
      if omega_state == None:
        if not state in terminal_set:
          state.key_iter().next()  # should have at least one transition
        return
      assert omega_state in terminal_set
      assert not state in terminal_set
      assert not list(omega_state.key_iter())
      omega_states.add(omega_state)
      match_action = omega_state.action()
      if not match_action:
        match_action = default_action
      action_states[state] = match_action
    dfa.visit_all_states(f)
    assert omega_states == terminal_set
    return action_states, omega_states

  @staticmethod
  def __build_backtracking_map(
      dfa, default_action, action_states, omega_states):
    incoming_transitions = dfa.build_incoming_transitions_map()
    backtracking_map = {}
    store_states = set()
    # Start node may be an edge case.
    start_state = dfa.start_state()
    if (not start_state in incoming_transitions and
        not start_state in action_states):
      action_states[start_state] = default_action
    error_map = {}
    for state in incoming_transitions.iterkeys():
      if state in omega_states or state in action_states:
        continue
      assert not state.omega_transition()
      seen = set([state])
      unchecked = set([state])
      match_edge = set()
      while unchecked:
        next = set()
        for unchecked_state in unchecked:
          if not unchecked_state in incoming_transitions:
            assert unchecked_state == start_state
            match_edge.add(unchecked_state)
            continue
          for (incoming_key, incoming_state) in incoming_transitions[unchecked_state]:
            assert not incoming_state in omega_states
            assert not incoming_key == TransitionKey.omega()
            if incoming_state in seen:
              continue
            if incoming_state in action_states:
              match_edge.add(incoming_state)
              seen.add(incoming_state)
            else:
              next.add(incoming_state)
        seen |= unchecked
        unchecked = next - seen
      # Accumulate unique actions.
      actions = set(map(lambda x : action_states[x].term(), match_edge))
      assert actions
      if not len(actions) == 1:
        error_map[state] = actions, match_edge
        continue
      action = iter(actions).next()
      # don't install default actions
      if action == default_action.term():
        continue
      store_states |= match_edge
      backtracking_map[state.node_number()] = (action, match_edge)
    return backtracking_map, store_states, error_map

  @staticmethod
  def __construct_dfa_with_backtracking(dfa, backtracking_map, store_states):
    def install_backtracking_action(new_states, node_number):
      omega_state_id = str(node_number) + '_bt'
      key = TransitionKey.omega()
      new_states[str(node_number)]['transitions'][key] = omega_state_id
      # install new state
      old_action = backtracking_map[node_number][0]
      new_states[omega_state_id] = {
        'transitions' : {},
        'action' : Action(Term('backtrack', old_action), 0),
        'terminal' : True,
      }
    def new_state_action(old_state):
      action = old_state.action()
      if not old_state in store_states:
        return action
      term = Term('store_lexing_state')
      if action:
        # TODO(dcarney): split target state instead
        term = Term('chain', term, action.term())
      return Action(term, 0)
    # Now generate the new dfa.
    terminal_set = dfa.terminal_set()
    def convert(old_state, new_states):
      node_number = old_state.node_number()
      # Clone existing state.
      new_states[str(node_number)] = {
        'transitions' : {
          k : str(v.node_number()) for (k, v) in old_state.key_state_iter() },
        'action' : new_state_action(old_state),
        'terminal' : old_state in terminal_set
      }
      # Install a backtracking action.
      if node_number in backtracking_map:
        install_backtracking_action(new_states, node_number)
      return new_states
    return dfa.visit_all_states(convert, {})

  def __generate(self):
    dfa = self.__dfa
    default_action = self.__default_action
    # Find nodes that have actions.
    action_states, omega_states= self.__compute_action_states(
      dfa, default_action)
    # Find nodes that need backtracking edges.
    backtracking_map, store_states, error_map  = self.__build_backtracking_map(
      dfa, default_action, action_states, omega_states)
    # Bail if error occurred.
    if error_map:
      self.__print_ambiguous_paths(dfa, error_map, action_states)
      raise Exception('ambiguous backtracking')
    # Now install backtracking everywhere.
    new_states = self.__construct_dfa_with_backtracking(
      dfa, backtracking_map, store_states)
    start_state = dfa.start_state()
    return Dfa(dfa.encoding(), str(start_state.node_number()), new_states)
