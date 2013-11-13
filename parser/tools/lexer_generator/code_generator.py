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
  def dfa_state_to_code(state, start_node_number):
    # FIXME: add different check types (if, switch, lookup table)
    # FIXME: add action + break / continue
    # FIXME: add default action
    code = '''
code_%s:
    fprintf(stderr, "state %s\\n");''' % (state.node_number(),
                                          state.node_number())

    action = state.action()
    if action:
      if action.type() == 'terminate':
        code += 'return 0;'
        return code
      elif action.type() == 'terminate_illegal':
        code += 'return 1;'
        return code

    code += '''
    yych = *(++cursor_);
    fprintf(stderr, "char at hand is %c (%d)\\n", yych, yych);\n'''

    for key, s in state.transitions().items():
      code += CodeGenerator.key_to_code(key)
      code += ''' {
        goto code_%s;
    }
''' % s.node_number()

    if action:
      code += '%s\nyych = *(--cursor_);\ngoto code_%s;\n' % (action.data(),
                                                             start_node_number)
    return code

  @staticmethod
  def dfa_to_code(dfa):
    start_node_number = dfa.start_state().node_number()
    code = '''
YYCTYPE yych = *cursor_;
goto code_%s;
''' % start_node_number
    def f(state, code):
      code += CodeGenerator.dfa_state_to_code(state, start_node_number)
      return code
    return dfa.visit_all_states(f, code)
