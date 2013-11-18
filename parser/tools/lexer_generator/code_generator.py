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

from dfa import Dfa
import jinja2
import os
import sys

class CodeGenerator:

  def __init__(self, rule_processor, use_mdfa, debug_print = False):
    if use_mdfa:
      dfa = rule_processor.default_automata().minimal_dfa()
    else:
      dfa = rule_processor.default_automata().dfa()
    self.__dfa = dfa
    self.__default_action = rule_processor.default_action
    self.__debug_print = debug_print
    self.__start_node_number = self.__dfa.start_state().node_number()

  def __state_cmp(self, left, right):
    if left == right:
      return 0
    if left.node_number() == self.__start_node_number:
      return -1
    if right.node_number() == self.__start_node_number:
      return 1
    return cmp(left.node_number(), right.node_number())

  def __canonicalize_traversal(self):
    dfa_states = []
    self.__dfa.visit_all_states(lambda state, acc: dfa_states.append(state))
    self.__dfa_states = sorted(dfa_states, cmp=self.__state_cmp)

  def process(self):

    self.__canonicalize_traversal()

    start_node_number = self.__start_node_number
    dfa_states = self.__dfa_states
    assert len(dfa_states) == self.__dfa.node_count()
    assert dfa_states[0].node_number() == start_node_number

    default_action = self.__default_action
    assert(default_action and default_action.match_action())
    default_action = default_action.match_action()

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    template_env = jinja2.Environment(
      loader = jinja2.PackageLoader('lexer_generator', '.'),
      undefined = jinja2.StrictUndefined)
    template = template_env.get_template('code_generator.jinja')

    return template.render(
      start_node_number = start_node_number,
      debug_print = self.__debug_print,
      default_action = default_action,
      dfa_states = dfa_states)
