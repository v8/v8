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

class TransitionKey:

  __single_char_type = 0
  __epsilon_type = 1
  __any_type = 2
  __class_type = 3

  @staticmethod
  def __create(key_type, value):
    key = TransitionKey()
    key.__type = key_type
    key.__value = value
    return key

  @staticmethod
  def epsilon():
    return TransitionKey.__create(TransitionKey.__epsilon_type, None)

  @staticmethod
  def any():
    return TransitionKey.__create(TransitionKey.__any_type, None)

  @staticmethod
  def single_char(char):
    return TransitionKey.__create(TransitionKey.__single_char_type, char)

  @staticmethod
  def character_class(invert, graph):
    # TODO
    return TransitionKey.__create(TransitionKey.__class_type, (invert, graph))

  @staticmethod
  def __class_match(class_graph, char):
    assert False

  __char_matchers = {
    __single_char_type: (lambda x, y : x == y),
    __epsilon_type: (lambda x, y : False),
    __any_type: (lambda x, y : True),
    __class_type: __class_match,
  }

  def matches_char(self, char):
    return TransitionKey.__char_matchers[self.__type](self.__value, char)

  def matches_key(self, key):
    assert key != TransitionKey.epsilon()
    assert self != TransitionKey.epsilon()
    if (key == self): return True
    # TODO need to test intersection/symmetric diff
    assert self != TransitionKey.any()
    return False

  def __hash__(self):
    if self.__value == None:
      return hash(self.__type)
    return hash(self.__type) ^ hash(self.__value)

  def __eq__(self, other):
    return (
      isinstance(other, self.__class__) and
      self.__type == other.__type and
      self.__value == other.__value)

  def __str__(self):
    if self.__type == self.__single_char_type:
      return "'%s'" % self.__value
    if self.__type == self.__epsilon_type:
      return "epsilon"
    if self.__type == self.__any_type:
      return "any"
    # TODO
    return "class"

  @staticmethod
  def merge_key_set(key_set):
    key_set -= set([TransitionKey.epsilon()])
    # TODO need intersections and symmetric differences
    return key_set
