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
import ply.lex as lex
import ply.yacc as yacc

class ParserBuilder:

  class Logger(object):
    def debug(self,msg,*args,**kwargs):
      logging.debug(msg % args)

    def info(self,msg,*args,**kwargs):
      logging.debug(msg % args)

    def warning(self,msg,*args,**kwargs):
      raise Exception("warning: "+ (msg % args) + "\n")

    def error(self,msg,*args,**kwargs):
      raise Exception("error: "+ (msg % args) + "\n")

  __static_instances = {}
  @staticmethod
  def parse(
      string, name, new_lexer, new_parser, preparse = None, postparse = None):
    if not name in ParserBuilder.__static_instances:
      logger = ParserBuilder.Logger()
      lexer_instance = new_lexer()
      lexer_instance.lex = lex.lex(module=lexer_instance)
      instance = new_parser()
      instance.yacc = yacc.yacc(
        module=instance, debug=True, write_tables=0,
        debuglog=logger, errorlog=logger)
      ParserBuilder.__static_instances[name] = (lexer_instance, instance)
    (lexer_instance, instance) = ParserBuilder.__static_instances[name]
    if preparse:
      preparse(instance)
    try:
      return_value = instance.yacc.parse(string, lexer=lexer_instance.lex)
    except Exception:
      del ParserBuilder.__static_instances[name]
      raise
    if postparse:
      postparse(instance)
    return return_value
