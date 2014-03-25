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

import logging
from transition_key import TransitionKey

class Path(object):

  def __init__(self, keys, states):
    self.__keys = keys
    self.__states = states
    assert self.__states
    self.__hash = None

  def sub_path(self):
    if len(self) == 1:
      return None
    return Path(self.__keys[:-1], self.__states[:-1])

  def key_iter(self):
    return iter(self.__keys)

  def state_at(self, index):
    return self.__states[index]

  @staticmethod
  def __hash_function((reps, l), state):
    n = state.node_number()
    h = (reps >> (l % 31) + 1) ^ reps ^ n ^ (n << (l % 31) + 1)
    return (h, l + 1)

  def __hash__(self):
    if self.__hash == None:
      self.__hash = reduce(self.__hash_function, self.__states, (101, -74))[0]
    return self.__hash

  def __eq__(self, other):
    return (isinstance(other, self.__class__) and
            self.__states == other.__states)

  def __len__(self):
    return len(self.__states)

class DfaPathWriter(object):

  def __init__(self, dfa):
    self.__dfa = dfa

  def write_lexer_shell_test_file(self, f):
    dfa = self.__dfa
    complete_paths = set()
    total_paths = 0
    encoding = dfa.encoding()
    start_state = dfa.start_state()
    for path, states_in_path, is_non_looping in dfa.path_iter():
      states = []
      keys = []
      for i, (key, state) in enumerate(path[1:]):
        states.append(state)
        keys.append(key)
        if key == TransitionKey.omega() and is_non_looping:
          assert i == len(path) - 2
      p = Path(keys, states)
      assert not p in complete_paths
      add_tail_transition = False
      total_paths += len(p)
      while p != None and (p not in complete_paths):
        complete_paths.add(p)
        self.__write_lines(p, start_state, add_tail_transition, encoding, f)
        p = p.sub_path()
        add_tail_transition = True
    logging.info("complete_paths %d" % len(complete_paths))
    logging.info("total_paths %d" % total_paths)

  __representative_map = {
    'non_primary_letter' : 256,
    'non_primary_whitespace' : 5760,
    'non_primary_line_terminator' : 8232,
    'non_primary_everything_else' : 706,
    'non_primary_identifier_part_not_letter' : 768,
  }

  __eos_rep = -1
  __omega_rep = -2

  @staticmethod
  def get_representatives(key, remaining = -1):
    reps = []
    contains_special = False
    for name, r in key.range_iter(None):
      if name == 'PRIMARY_RANGE':
        reps.append(r[0])
        if r[0] != r[1]:
          reps.append(r[1])
      elif name == 'CLASS':
        reps.append(DfaPathWriter.__representative_map[r])
      elif name == 'UNIQUE':
        if r == 'eos':
          assert remaining == 1
          reps.append(DfaPathWriter.__eos_rep)
          contains_special = True
        else:
          raise Exception('unimplemented %s %s' % (name, str(r)))
      elif name == 'OMEGA':
        assert remaining == 0
        reps.append(DfaPathWriter.__omega_rep)
        contains_special = True
      else:
        raise Exception('unimplemented %s %s' % (name, str(r)))
    if contains_special:
      assert len(reps) == 1
    return reps

  @staticmethod
  def no_transition_keys(state, encoding):
    all_keys = set(state.key_iter())
    eos_found = False
    all_keys.discard(TransitionKey.omega())
    eos_key = TransitionKey.unique('eos')
    if eos_key in all_keys:
      all_keys.remove(eos_key)
      eos_found = True
    # TODO(dcarney): have inverse key return uniques as a separate list
    inverse_key = TransitionKey.inverse_key(encoding, all_keys)
    if not inverse_key:
      return []
    reps = DfaPathWriter.get_representatives(inverse_key)
    if not eos_found:
      reps.append(DfaPathWriter.__eos_rep)
    return reps

  @staticmethod
  def __write_line(head, tail, f):
    length = len(head)
    if tail != DfaPathWriter.__eos_rep:
      length += 1
    if not length:
      return
    if tail == DfaPathWriter.__eos_rep:
      f.write('%s\n' % ' '.join(map(str, head)))
    elif head:
      f.write('%s %s\n' % (' '.join(map(str, head)), str(tail)))
    else:
      f.write("%s\n" % str(tail))

  def __write_lines(self, path, start_state, add_tail_transition, encoding, f):
    current_state = start_state
    reps = []
    last_index = len(path) - 1
    if add_tail_transition:
      last_index += 1
    for i, key in enumerate(path.key_iter()):
      # current_state = current_state.transition_state_for_key(key)
      # assert current_state == self.__states[i]
      reps.append(self.get_representatives(key, last_index - i))
    head = []
    for i, x in enumerate(reps):
      assert x
      if x[0] >= 0:
        # TODO(dcarney): produce a file with x[-1] here
        head.append(x[0])
      elif x[0] == DfaPathWriter.__eos_rep:
        # this is a trailing eos or a trailing omega following eos.
        assert i == len(reps) - 1 or (
          len(x) == 1 and i == len(reps) - 2 and
          len(reps[-1]) == 1 and reps[-1][0] == DfaPathWriter.__omega_rep)
        assert add_tail_transition == (i == len(reps) - 1)
        if i == len(reps) - 1:
          add_tail_transition = False
      elif x[0] == DfaPathWriter.__omega_rep:
        # this is a trailing omega
        # TODO(dcarney): produce an omega transition, not eos.
        assert not add_tail_transition
        assert len(x) == 1 and i == len(reps) - 1
      else:
        raise Exception('unreachable')
    if not add_tail_transition:
      self.__write_line(head, self.__eos_rep, f)
      return
    no_transition_keys = self.no_transition_keys(path.state_at(-1), encoding)
    # TODO(dcarney): don't test all edges here.
    for tail in no_transition_keys:
      self.__write_line(head, tail, f)
