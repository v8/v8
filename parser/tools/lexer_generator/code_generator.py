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

import os
import sys
import jinja2
from copy import deepcopy
from dfa import Dfa
from transition_keys import TransitionKey

class CodeGenerator:

  def __init__(self,
               rule_processor,
               minimize_default = True,
               inline = True,
               switching = True,
               debug_print = False,
               log = False):
    if minimize_default:
      dfa = rule_processor.default_automata().minimal_dfa()
    else:
      dfa = rule_processor.default_automata().dfa()
    self.__dfa = dfa
    self.__default_action = rule_processor.default_action
    self.__debug_print = debug_print
    self.__start_node_number = self.__dfa.start_state().node_number()
    self.__log = log
    self.__inline = inline
    self.__switching = switching
    self.__jump_table = []

  __jump_labels = ['state_entry', 'after_entry_code']

  @staticmethod
  def __transform_state(encoding, state):
    # action data
    action = state.action()
    entry_action = None if not action else action.entry_action()
    match_action = None if not action else action.match_action()
    # generate ordered transitions
    transitions = map(lambda (k, v) : (k, v.node_number()),
                      state.transitions().items())
    def cmp(left, right):
      return TransitionKey.canonical_compare(left[0], right[0])
    transitions = sorted(transitions, cmp)
    # map transition keys to disjoint ranges and collect stats
    disjoint_keys = []
    unique_transitions = {}
    old_transitions = transitions
    transitions = []
    (class_keys, distinct_keys, ranges) = (0, 0, 0)
    zero_transition = None
    for key, transition_id in old_transitions:
      keys = []
      for (t, r) in key.range_iter(encoding):
        if t == 'CLASS':
          class_keys += 1
          keys.append((t, r))
        elif t == 'PRIMARY_RANGE':
          distinct_keys += r[1] - r[0] + 1
          ranges += 1
          # split 0 out of range, don't bother updating stats
          assert r[0] >= 0
          if r[0] == 0:
            assert zero_transition == None
            zero_transition = transition_id
            if r[0] == r[1]:
              continue
            r = (r[0] + 1, r[1])
          keys.append((t, r))
        elif t == 'UNIQUE':
          assert r == 'no_match' or r == 'eos'
          assert r not in unique_transitions
          unique_transitions[r] = transition_id
        else:
          raise Exception()
      if keys:
        transitions.append((keys, transition_id))
    # delay zero transition until last
    if zero_transition != None:
      transitions.append(([('PRIMARY_RANGE', (0, 0))], transition_id))
    return {
      'node_number' : None,
      'original_node_number' : state.node_number(),
      'transitions' : transitions,
      # flags for code generator
      'can_elide_read' : len(transitions) == 0,
      'is_eos_handler' : False,
      'inline' : False,
      'must_not_inline' : False,
      # transitions for code generator
      'if_transitions' : [],
      'switch_transitions' : [],
      'deferred_transitions' : [],
      'unique_transitions' : unique_transitions,
      # state actions
      'entry_action' : entry_action,
      'match_action' : match_action,
      # statistics for states
      'class_keys' : class_keys,
      'distinct_keys' : distinct_keys,
      'ranges' : ranges,
      # record of which entry points will be needed
      'entry_points' : {k : False for k in CodeGenerator.__jump_labels}
    }

  def __register_jump(self, node_id, label):
    if label != 'inline':
      assert label in CodeGenerator.__jump_labels
      state = self.__dfa_states[node_id]['entry_points'][label] = True
    self.__jump_table.append((node_id, label))
    return len(self.__jump_table) - 1

  def __set_inline(self, count, state):
    inline = False
    if state['must_not_inline']:
      inline = False
    # inline terminal states
    elif not state['transitions']:
      inline = True
    # inline next to terminal states with 1 or 2 transitions
    elif state['distinct_keys'] < 3 and state['class_keys'] == 0:
      inline = True
      # ensure state terminates in 1 step
      for key, state_id in state['transitions']:
        if self.__dfa_states[state_id]['transitions']:
          inline = False
          break
    state['inline'] = inline
    return count + 1 if inline else count

  def __split_transitions(self, split_count, state):
    '''Goes through the transitions for 'state' and decides which of them should
    use the if statement and which should use the switch statement.'''
    assert not state['switch_transitions']
    (distinct_keys, ranges) = (state['distinct_keys'], state['ranges'])
    no_switch = distinct_keys <= 7 or float(distinct_keys)/float(ranges) >= 7.0
    if_transitions = []
    switch_transitions = []
    deferred_transitions = []
    for (ranges, node_id) in state['transitions']:
      i = []
      s = []
      d = []
      for r in ranges:
        # all class checks will be deferred to after all other checks
        if r[0] == 'CLASS':
          d.append(r)
        # zero must assigned to an if check because of eos check
        elif no_switch or (r[1][0] == 0):
          i.append(r)
        else:
          s.append(r[1])
      if i:
        if_transitions.append((i, node_id))
      if s:
        switch_transitions.append((s, node_id))
      if d:
        deferred_transitions.append((d, node_id))
    state['if_transitions'] = if_transitions
    state['switch_transitions'] = switch_transitions
    state['deferred_transitions'] = deferred_transitions
    return split_count + (0 if no_switch else 1)

  __call_map = {
    'non_primary_whitespace' : 'IsWhiteSpaceNotLineTerminator',
    'non_primary_letter' : 'IsLetter',
    'non_primary_identifier_part_not_letter' : 'IsIdentifierPartNotLetter',
    'non_primary_line_terminator' : 'IsLineTerminator',
  }

  def __rewrite_deferred_transitions(self, state):
    transitions = state['deferred_transitions']
    if not transitions:
      return
    encoding = self.__dfa.encoding()
    bom = 'byte_order_mark'
    catch_all = 'non_primary_everything_else'
    all_classes = set(encoding.class_name_iter())
    call_classes = all_classes - set([bom, catch_all])
    def remap_transition(class_name):
      if class_name in call_classes:
        return ('LONG_CHAR_CLASS', 'call', self.__call_map[class_name])
      if class_name == bom:
        return ('LONG_CHAR_CLASS', class_name)
      raise Exception(class_name)
    long_class_transitions = []
    long_class_map = {}
    catchall_transition = None
    # loop through and remove catch_all_transitions
    for (classes, transition_node_id) in transitions:
      lct = []
      has_catch_all = False
      for (class_type, class_name) in classes:
        assert not class_name in long_class_map
        long_class_map[class_name] = transition_node_id
        if class_name == catch_all:
          assert not has_catch_all
          assert catchall_transition == None
          has_catch_all = True
        else:
          lct.append(remap_transition(class_name))
      if has_catch_all:
        catchall_transition = (lct, transition_node_id)
      elif lct:
        long_class_transitions.append((lct, transition_node_id))
    if catchall_transition:
      catchall_transitions = all_classes
      for class_name in long_class_map.iterkeys():
        catchall_transitions.remove(class_name)
      assert not catchall_transitions, "class inversion not unimplemented"
    if catchall_transition:
      catchall_transition = [
        ([('LONG_CHAR_CLASS', 'catch_all')], catchall_transition[1])]
    else:
      catchall_transition = []
    state['deferred_transitions'] = (long_class_transitions +
                                     catchall_transition) # must be last

  @staticmethod
  def __reorder(current_node_number, id_map, dfa_states):
    current_node = id_map[current_node_number]
    if current_node['node_number'] != None:
      return
    current_node['node_number'] = len(dfa_states)
    dfa_states.append(current_node)
    for (key, node_number) in current_node['transitions']:
      CodeGenerator.__reorder(node_number, id_map, dfa_states)
    for node_number in current_node['unique_transitions'].values():
      CodeGenerator.__reorder(node_number, id_map, dfa_states)

  @staticmethod
  def __mark_eos_states(dfa_states, eos_states):
    for state_id in eos_states:
      state = dfa_states[state_id]
      state['is_eos_handler'] = True
      state['must_not_inline'] = True
      assert state['match_action']
      assert not state['entry_action']
      assert not state['transitions']
      assert not state['unique_transitions']

  def __build_dfa_states(self):
    dfa_states = []
    self.__dfa.visit_all_states(lambda state, acc: dfa_states.append(state))
    encoding = self.__dfa.encoding()
    f = lambda state : CodeGenerator.__transform_state(encoding, state)
    dfa_states = map(f, dfa_states)
    id_map = {x['original_node_number'] : x for x in dfa_states}
    dfa_states = []
    CodeGenerator.__reorder(self.__start_node_number, id_map, dfa_states)
    # store states
    eos_states = set([])
    def f((key, original_node_number)):
      return (key, id_map[original_node_number]['node_number'])
    for state in dfa_states:
      state['transitions'] = map(f, state['transitions'])
      state['unique_transitions'] = {k : id_map[v]['node_number']
          for k, v in state['unique_transitions'].items()}
      if 'eos' in state['unique_transitions']:
        eos_states.add(state['unique_transitions']['eos'])
    assert id_map[self.__start_node_number]['node_number'] == 0
    assert len(dfa_states) == self.__dfa.node_count()
    # mark eos states
    self.__mark_eos_states(dfa_states, eos_states)
    self.__dfa_states = dfa_states

  def __rewrite_gotos(self):
    goto_map = {}
    states = self.__dfa_states
    for state in states:
      if (state['match_action'] and
          state['match_action'][0] == 'do_stored_token'):
        assert not state['match_action'][1] in goto_map
        goto_map[state['match_action'][1]] = state['node_number']
    mapped_actions = set([
      'store_harmony_token_and_goto',
      'store_token_and_goto',
      'goto_start'])
    for state in states:
      if not state['match_action']:
        continue
      action = state['match_action'][0]
      if action in mapped_actions:
        value = state['match_action'][1]
        target_id = goto_map[value[-1]]
        states[target_id]['must_not_inline'] = True
        if action != 'goto_start':
          state['can_elide_read'] = False
          label = 'after_entry_code'
        else:
          states[target_id]['can_elide_read'] = False
          label = 'state_entry'
        jump_label = self.__register_jump(target_id, label)
        state['match_action'] = (action, tuple(list(value[:-1]) + [jump_label]))

  def __inlined_state(self, target_id):
    state = deepcopy(self.__dfa_states[target_id])
    state['node_number'] = len(self.__dfa_states)
    self.__dfa_states.append(state)
    # mark inline as none, to be correctly handled below
    state['inline'] = None
    # clear entry points
    state['entry_points'] = {k : False for k in CodeGenerator.__jump_labels}
    return state['node_number']

  def __rewrite_transitions_to_jumps(self, start_id, count, inline_mapping_in):
    # order here should match the order of code generation
    transition_names = [
      'switch_transitions',
      'if_transitions',
      'deferred_transitions']
    end_offset = start_id + count
    assert len(self.__dfa_states) == end_offset
    total_nodes_created = 0
    for state_id in range(start_id, end_offset):
      state = self.__dfa_states[state_id]
      # this will be ignored during code generation,
      # and it's needed as a template, so don't rewrite
      if state['inline'] == True:
        continue
      # these is a new inline state, now mark as inline and rewrite
      if state['inline'] == None:
        state['inline'] = True
      inline_mapping = inline_mapping_in.copy()
      def generate_jump((key, target_id)):
        jump_type = 'state_entry'
        if self.__dfa_states[target_id]['inline']:
          # generate at most one inline state for all transitions
          if not target_id in inline_mapping:
            inline_mapping[target_id] = self.__inlined_state(target_id)
            jump_type = 'inline'
          target_id = inline_mapping[target_id]
        else:
          assert not target_id in inline_mapping
        return (key, self.__register_jump(target_id, jump_type))
      for name in transition_names:
        state[name] = map(generate_jump, state[name])
      if 'eos' in state['unique_transitions']:
        # eos state is not inlined
        eos_state_id = state['unique_transitions']['eos']
        jump = self.__register_jump(eos_state_id, 'state_entry')
        state['unique_transitions']['eos'] = jump
      # now rewrite all nodes created
      nodes_created = len(inline_mapping) - len(inline_mapping_in)
      assert len(self.__dfa_states) == (
        end_offset + total_nodes_created + nodes_created)
      if nodes_created == 0:
        continue
      created = self.__rewrite_transitions_to_jumps(
        end_offset + total_nodes_created, nodes_created, inline_mapping)
      total_nodes_created += nodes_created + created
    return total_nodes_created

  def process(self):

    self.__build_dfa_states()
    dfa_states = self.__dfa_states
    # split transitions
    switched = reduce(self.__split_transitions, dfa_states, 0)
    if self.__log:
      print "%s states use switch (instead of if)" % switched
    # rewrite deferred transitions
    for state in dfa_states:
      self.__rewrite_deferred_transitions(state)
    # rewrite gotos
    self.__rewrite_gotos()
    # set nodes to inline
    if self.__inline:
      inlined = reduce(self.__set_inline, dfa_states, 0)
      if self.__log:
        print "%s states inlined" % inlined
    # rewrite transitions to use jumps
    inlined_nodes = self.__rewrite_transitions_to_jumps(0, len(dfa_states), {})
    if self.__log:
      print "%s inlined nodes created" % inlined_nodes
    # mark the entry point in case there are implicit jumps to it
    self.__dfa_states[0]['entry_points']['state_entry'] = True

    default_action = self.__default_action
    assert(default_action and default_action.match_action())
    default_action = default_action.match_action()

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    template_env = jinja2.Environment(
      loader = jinja2.PackageLoader('lexer_generator', '.'),
      undefined = jinja2.StrictUndefined)
    template = template_env.get_template('code_generator.jinja')

    encoding = self.__dfa.encoding()
    char_types = {'latin1': 'uint8_t', 'utf16': 'uint16_t', 'utf8': 'int8_t'}
    char_type = char_types[encoding.name()]

    return template.render(
      start_node_number = 0,
      debug_print = self.__debug_print,
      default_action = default_action,
      dfa_states = dfa_states,
      jump_table = self.__jump_table,
      encoding = encoding.name(),
      char_type = char_type,
      upper_bound = encoding.primary_range()[1])
