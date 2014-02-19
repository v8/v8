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

from transition_key import TransitionKey
from automaton import Term, Action, Automaton
from dfa import Dfa

# --- Optimization: Replacing tokens with gotos ---
#
# This optimization reduces the code size for grammars which have constructs
# similar to keywords and identifiers. Consider the following grammar:
#
# "baz" <|token(BAZ)|>
# "bazz" <|token(BAZZ)|>
#
# /[a-z]/ <|token(IDENTIFIER)|Identifier>
#
# <<Identifier>>
# /[a-z]/ <|token(IDENTIFIER)|continue>
#
# In the corresponding dfa, we have a set of nodes for recognizing the keywords,
# and one node for the "Identifier" state. From every node in the keyword part
# of the dfa, there is an edge to the Identifier state with the letters which
# cannot be used for advancing in the keyword part (e.g., we have seen "baz" and
# the next character is "s", so that's an identifier and not the keyword
# "baz" or "bazz").
#
# [ ] ---b---> [ ] ---a---> [ ] ---z---> [BAZ] ---z---> [BAZZ]
#               |            |             |            |
#             [b-z]        [a-y]         [a-y]        [a-z]
#               |            |             |            |
#               \---------------------------------------/
#                                  |
#                                  v
#                                 [ID] ----------\
#                                  ^            [a-z]
#                                  --------------/
#
# If we generate code from the dfa, these edges contribute a lot to the code
# size. To reduce the code size, we do the following transformation:
#
# For each state which is an end of a keyword, we, add a match action "store
# token and goto". The match action will be executed only if we cannot advance
# in the graph with the next character. For example, if we have seen "baz", we
# first check if the next character is "z" to get "bazz", and if the next
# character is not "z", we execute the match action.
#
# When we execute the "store token and goto" action, we record that we have seen
# the corresponding keyword (the token might still be an identifier, depending
# on what the next character is). Then we jump to the Identifier state of the
# graph. The Identifier state has an entry action "store_token(IDENTIFIER)" for
# [a-z]. When it is executed, it overwrites the stored token with IDENTIFIER.
#
# Example: if we have seen "baz" and the next character is not "z", we store the
# token BAZ and jump to the Identifier state. If the next character is "a", we
# are sure the token is not the keyword "baz", and overwrite the stored token
# with IDENTIFIER.
#
# The Identifier state has a match action "do stored token", which returns the
# stored token to the upper layer.
#
# Example: if we have seen "baz" and the next character is not "z", we store the
# token BAZ and jump to the Identifier state. If the next character is a space,
# we cannot advance in the dfa, and the match action is executed. The match
# action returns the stored token BAZ to the upper layer.
#
# For each state which is an intermediate state in a keyword, we add the same
# "goto", but we don't need to store a token.
#
# [ ] ---b---> [ ] ---a---> [ ] ---z---> [BAZ] ---z---> [BAZZ]
#               |            |             |            |
#            goto         goto     store BAZ, goto  store BAZZ, goto
#               |            |             |            |
#               \---------------------------------------/
#                                  |
#                                  v
#                                 [ID] ----------\
#                                  ^           [a-z], store IDENTIFIER
#                                  --------------/
#
# (Note: this graph illustrates the logic, but is not accurate wrt the entry
# actions and match actions of the states.)
#
# The code size decreases, because we remove the checks which correspond to
# transitions from keyword states to the identifier state ([b-z], [a-y] etc.),
# and replace them with a more general check ([a-z]) in one place. This works
# because the more specialized checks (e.g., checking for "z" when we have seen
# "baz") are done before we "goto" to the state which has the generic check.
#
# There is one complication though: When we consider adding a goto from a state
# s1 to state s2, we need to check all possible transitions from s1 and from s2,
# and see if they match. We cannot jump to a state which has different
# transitions than the state where we're jumping from.
#
# For example, the following partial dfa allows distinguishing identifiers which
# have only lower case letters from identifiers which have at least one upper
# case letter.
#
# [s1] ---a---> [ ]
#  |
#  |            /---[a-z]--\
#  |            v          |
#  ---[b-z]--> [s2] ------/
#  |             |
#  |           [A-Z]
#  |             |
#  |             v
#  \--[A-Z]---> [s3]
#
# We can add a goto from s1 to s2 (after checking for "a"), because the
# transitions match: with [b-z], both states transition to s2, and with [A-Z],
# both states transition to s3. Note that [a-z] is a superkey of [b-z], and the
# transition from s2 is defined for the superkey, but that is ok.

class DfaOptimizer(object):

  def __init__(self, dfa, log):
    self.__dfa = dfa
    self.__log = log

  @staticmethod
  def __transistions_match(encoding, incoming_key, incoming_state, state):
    keys = set(state.key_iter())
    keys.add(incoming_key)
    disjoint_keys = TransitionKey.disjoint_keys(encoding, keys)
    for key in disjoint_keys:
      if not incoming_key.is_superset_of_key(key):
        continue
      transition_state = state.transition_state_for_key(key)
      if incoming_state.transition_state_for_key(key) != transition_state:
        return False
    return True

  @staticmethod
  def __build_incoming_transitions(dfa):
    incoming_transitions = {}
    def f(state, visitor_state):
      for key, transition_state in state.key_state_iter():
        if not transition_state in incoming_transitions:
          incoming_transitions[transition_state] = []
        incoming_transitions[transition_state].append((key, state))
    dfa.visit_all_states(f)
    return incoming_transitions

  @staticmethod
  def __build_replacement_map(dfa):
    replacements = {}
    encoding = dfa.encoding()
    incoming_transitions = DfaOptimizer.__build_incoming_transitions(dfa)
    replacement_targets = set([])
    # TODO(dcarney): this should be an ordered walk
    for state, incoming in incoming_transitions.items():
      if len(incoming) < 10:
        continue
      # the states action will be replaced, so we just don't optimize if there
      # is one
      if state.action():  # TODO(dcarney): allow this via action chaining
        continue
      # We only perform this optimization on 'token' actions
      match_action = state.match_action()
      if not match_action.name() == 'token':
        continue
      assert state.omega_transition() in dfa.terminal_set()
      # we can drop incoming edges for states with a match action
      # of either 'token' or 'harmony_token'
      def is_replacement_candidate(state):
        action = state.match_action()
        return action.name() == 'token' or action.name() == 'harmony_token'
      for (incoming_key, incoming_state) in incoming:
        # check to see if incoming edges are matched by an outgoing edge
        if (incoming_state == state or  # drop self edges
            incoming_key == TransitionKey.omega() or
            not is_replacement_candidate(incoming_state) or
            not DfaOptimizer.__transistions_match(
              encoding, incoming_key, incoming_state, state)):
          continue
        assert (not incoming_state in replacements or
                replacements[incoming_state][0] == state)
        # set this transition as to be removed
        if not incoming_state in replacements:
          replacements[incoming_state] = (state, set())
        replacements[incoming_state][1].add(incoming_key)
        replacement_targets.add(state)
        # now see if we can gather other transistions into the goto
        for key in incoming_state.key_iter():
          if (key != TransitionKey.omega() and
              key not in replacements[incoming_state][1] and
              DfaOptimizer.__transistions_match(
                encoding, key, incoming_state, state)):
            # this transition can be removed
            replacements[incoming_state][1].add(key)
    return (replacement_targets, replacements)

  @staticmethod
  def __new_state(is_terminal, action):
    return {
        'transitions' : {},
        'terminal' : is_terminal,
        'action' : action,
      }

  @staticmethod
  def __new_state_name(state):
    return str(state.node_number())

  @staticmethod
  def __split_target_state(state):
    old_name = DfaOptimizer.__new_state_name(state)
    old_match_action = state.match_action()
    assert old_match_action.name() == 'token', 'unimplemented'
    precedence = old_match_action.precedence()
    match_action = Action(Term('do_stored_token'), precedence)
    stored_token = old_match_action.term().args()[0]
    head_action = Action(Term('store_token', stored_token), precedence)
    tail_action = Action(Term('no_op', stored_token), precedence)
    head = DfaOptimizer.__new_state(False, head_action)
    tail = DfaOptimizer.__new_state(False, tail_action)
    match = DfaOptimizer.__new_state(True, match_action)
    head['transitions'][TransitionKey.omega()] = old_name + '_tail'
    tail['transitions'][TransitionKey.omega()] = old_name + '_match'
    return (head, tail, match)

  # generate a store action to replace an existing action
  @staticmethod
  def __replacement_action(state, transition_state):
    old_action = state.match_action()
    assert old_action
    transition_action = transition_state.match_action().term()
    if old_action.term() == transition_action:
      # no need to store token
      return Action.empty_action()
    new_name = 'store_' + old_action.name()
    return Action(
      Term(new_name, *old_action.term().args()), old_action.precedence())

  @staticmethod
  def __apply_jump(counters, state, new_state, target_state):
    # generate a new action for the new omega transition
    new_action = DfaOptimizer.__replacement_action(state, target_state)
    # determine new jump target
    jump_target = DfaOptimizer.__new_state_name(target_state)
    if not new_action:
      # just jump to entry of target_state, which will be split
      new_state['transitions'][TransitionKey.omega()] = jump_target
      counters['goto_start'] += 1
      return None
    counters[new_action.name()] += 1
    # install new match state
    match_state_id = DfaOptimizer.__new_state_name(state) + '_match'
    new_state['transitions'][TransitionKey.omega()] = match_state_id
    match_state = DfaOptimizer.__new_state(False, new_action)
    match_state['transitions'][TransitionKey.omega()] = jump_target + '_tail'
    return match_state

  @staticmethod
  def __remove_orphaned_states(states, orphanable, start_name):
    seen = set([])
    def visit(state_id, acc):
      seen.add(state_id)
    def state_iter(state_id):
      return states[state_id]['transitions'].itervalues()
    Automaton.visit_states(set([start_name]), visit, state_iter=state_iter)
    def f(name, state):
      assert name in seen or name in orphanable
      return name in seen
    return {k: v for k, v in states.iteritems() if f(k, v)}

  def __replace_tokens_with_gotos(self):
    '''replace states with no entry action and match action of type 'token(X)'
    with new states with entry action store_token(X) and match action
    goto(state_id) which has (far) fewer transitions'''

    dfa = self.__dfa
    (replacement_targets, replacements) = self.__build_replacement_map(dfa)
    if not replacement_targets:
      return dfa
    # perform replacement
    states = {}
    name = DfaOptimizer.__new_state_name
    counters = {
      'removals' : 0,
      'goto_start' : 0,
      'store_token' : 0,
      'store_harmony_token' : 0,
      'split_state' : 0
      }
    # map the old state to the new state, with fewer transitions and
    # goto actions
    orphanable = set([])
    def replace_state(state, acc):
      if state in replacements:
        target_state = replacements[state][0]
        deletions = replacements[state][1]
      else:
        deletions = {}
      # this is a replacement target, so we split the state
      if state in replacement_targets:
        assert not deletions
        (head, tail, match) = DfaOptimizer.__split_target_state(state)
        new_state = tail
        states[name(state)] = head
        states[name(state) + '_tail'] = tail
        states[name(state) + '_match'] = match
        counters['split_state'] += 1
      else:
        new_state = self.__new_state(state in self.__dfa.terminal_set(),
                                 state.action())
        states[name(state)] = new_state
      assert not TransitionKey.omega() in deletions
      for (transition_key, transition_state) in state.key_state_iter():
        # just copy these transitions
        if transition_key != TransitionKey.omega():
          if not transition_key in deletions:
            new_state['transitions'][transition_key] = name(transition_state)
            continue
          else:
            # drop these transitions
            deletions.remove(transition_key)
            counters['removals'] += 1
            continue
        if transition_key in new_state['transitions']:
          # this is a split state, omega has already been assigned
          # mark old match state as orphanable
          orphanable.add(name(transition_state))
        elif not state in replacements:
          # no replacement
          new_state['transitions'][transition_key] = name(transition_state)
        else:
          # mark old omega state as orphanable
          orphanable.add(name(transition_state))
          match_state = DfaOptimizer.__apply_jump(
            counters, state, new_state, target_state)
          if match_state:
            states[name(state) + '_match'] = match_state
      assert not deletions
    self.__dfa.visit_all_states(replace_state)
    start_name = name(self.__dfa.start_state())
    states = self.__remove_orphaned_states(states, orphanable, start_name)
    # dump stats
    if self.__log:
      print 'goto_start inserted %s' % counters['goto_start']
      print 'store_token inserted %s' % (
        counters['store_token'])
      print 'store_harmony_token %s' % (
        counters['store_harmony_token'])
      print 'transitions removed %s' % counters['removals']
      print 'states split %s' % counters['split_state']
    return Dfa(self.__dfa.encoding(), start_name, states)

  @staticmethod
  def optimize(dfa, log):
    optimizer = DfaOptimizer(dfa, log)
    return optimizer.__replace_tokens_with_gotos()
