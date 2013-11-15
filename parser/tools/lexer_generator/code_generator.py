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

class CodeGenerator:

  @staticmethod
  def key_to_code(key):
    code = 'if ('
    first = True
    for (kind, r) in key.range_iter():
      if kind == 'CLASS': # FIXME: add class checks
        continue
      assert kind == 'LATIN_1'
      if not first:
        code += ' || '
      if r[0] == r[1]:
        code += 'yych == %s' % r[0]
      elif r[0] == 0:
        code += 'yych <= %s' % r[1]
      elif r[1] == 255: # FIXME: this should depend on the char type maybe??
        code += 'yych >= %s' % r[0]
      else:
        code += '(yych >= %s && yych <= %s)' % (r[0], r[1])
      first = False
    code += ')'
    return code

  @staticmethod
  def __terminate_code(value):
    assert value == None
    return 'PUSH_TOKEN(Token::EOS); return 0;'

  @staticmethod
  def __terminate_illegal_code(value):
    assert value == None
    return 'PUSH_TOKEN(Token::ILLEGAL); return 1;'

  @staticmethod
  def __skip_code(value):
    assert value == None
    return 'SKIP();'

  @staticmethod
  def __push_line_terminator_code(value):
    assert value == None
    return 'PUSH_LINE_TERMINATOR();'

  @staticmethod
  def __push_token_code(value):
    assert value != None
    return 'PUSH_TOKEN(Token::%s);' % value

  @staticmethod
  def __code_code(value):
    assert value != None
    return '%s\n' % value

  @staticmethod
  def __skip_and_terminate_code(value):
    return 'SKIP(); --start_; ' + CodeGenerator.__terminate_code(value)

  def __init__(self, dfa, default_action):
    self.__dfa = dfa
    self.__start_node_number = dfa.start_state().node_number()
    self.__default_action = default_action
    # make this better
    self.__action_code_map = {
      "terminate" : self.__terminate_code,
      "terminate_illegal" : self.__terminate_illegal_code,
      "push_token" : self.__push_token_code,
      "push_line_terminator" : self.__push_line_terminator_code,
      "skip" : self.__skip_code,
      "code" : self.__code_code,
      "skip_and_terminate" : self.__skip_and_terminate_code,
    }

  def __dfa_state_to_code(self, state):
    # FIXME: add different check types (if, switch, lookup table)
    # FIXME: add action + break / continue
    # FIXME: add default action
    code = ''
    if self.__start_node_number == state.node_number():
      code += '''
code_start:
'''
    code += '''
code_%s:
    //fprintf(stderr, "state %s\\n");
''' % (state.node_number(),
       state.node_number())

    entry_action = state.action().entry_action() if state.action() else None
    match_action = state.action().match_action() if state.action() else None

    if entry_action:
      code += self.__action_code_map[entry_action[0]](entry_action[1])

    code += '''
    //fprintf(stderr, "char at hand is %c (%d)\\n", yych, yych);\n'''

    for key, s in state.transitions().items():
      code += CodeGenerator.key_to_code(key)
      code += ''' {
        FORWARD();
        goto code_%s;
    }
''' % s.node_number()

    if match_action:
      code += self.__action_code_map[match_action[0]](match_action[1])
      code += 'goto code_%s;\n' % self.__start_node_number
    else:
      code += 'goto default_action;\n'
    return code

  def __process(self):
    dfa = self.__dfa
    default_action = self.__default_action
    code = '''
#include "lexer/even-more-experimental-scanner.h"

namespace v8 {
namespace internal {
uint32_t EvenMoreExperimentalScanner::DoLex() {
  YYCTYPE yych = *cursor_;
  goto code_%s;
''' % self.__start_node_number
    def f(state, code):
      code += self.__dfa_state_to_code(state)
      return code
    code = dfa.visit_all_states(f, code)

    default_action_code = ''
    assert(default_action and default_action.match_action())
    action = default_action.match_action()
    default_action_code = self.__action_code_map[action[0]](action[1])
    code += '''
  CHECK(false); goto code_start;
default_action:
  //fprintf(stderr, "default action\\n");
  %s
  FORWARD();
  goto code_%s;
  return 0;
}
}
}
''' % (default_action_code, self.__start_node_number)
    return code

  @staticmethod
  def rule_processor_to_code(rule_processor, use_mdfa):
    if use_mdfa:
      dfa = rule_processor.default_automata().minimal_dfa()
    else:
      dfa = rule_processor.default_automata().dfa()
    return CodeGenerator(dfa, rule_processor.default_action).__process()
