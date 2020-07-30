#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
A collection of helper functions for transforming text proto files to
other formats.
"""

import cfg_handlers
import pprint
import json

import re
import sys

COMMON_BUILDER_BODY = """    executable=%s,
    swarming_tags=%s,
    dimensions=%s,
    service_account=%s,
    execution_timeout=%s,
    build_numbers=%s,
    properties=%s,
    caches=%s,
    priority=%s,
  )"""

MIGRATED_BUILDERS = [
  "Auto-tag",
  "V8 lkgr finder",
  "Auto-roll - push",
  "Auto-roll - deps",
  "Auto-roll - v8 deps",
  "Auto-roll - test262",
  "Auto-roll - wasm-spec",
  "Auto-roll - release process",
  "v8_verify_flakes",
]
class StarlarkGenerator:
  def __init__(self):
    self.bb_cfg = cfg_handlers.BuildBucketCfg()
    self.sc_cfg = cfg_handlers.SchedulerCfg()
    self.migrated = set(MIGRATED_BUILDERS)

  def generate(self):
    self.write_branch_coverage()
    self.write_tryng()
    self.write_builders4buckets()

  def write_branch_coverage(self):
    stable_builders = self.write_stable_builders(
      self.bb_cfg.stable_builders()
    )
    self.migrated.update([
      self.bb_cfg.builder_name(builder)
      for builder in self.bb_cfg.stable_builders()
    ])
    code = stable_builders.replace("STABLE ", "")
    code = self.minor_text_adjustments(code)
    self.write_file(
      'branch_coverage.star',
      "load('//lib.star','v8_branch_coverage_builder')",
      code)

  def write_file(self, name, header, content):
    with open(name, "w") as f:
      f.write(header)
      f.write("\n")
      f.write(content)

  def write_stable_builders(self, builders):
    return '\n'.join([
      self.write_stable_builder(
        self.bb_cfg.consolidate_builder(builder)
      ) for builder in builders if self.bb_cfg.builder_name(builder)
    ])

  def write_tryng(self):
    overrides = self.bb_cfg.trytriggered_builders_timeouts()
    builders = self.bb_cfg.ng_builders()
    builder_name_prefixes = [
      self.bb_cfg.builder_name(builder)[:-3]
      for builder in builders
    ]
    builder_names = []
    for name in builder_name_prefixes:
      builder_names.append(name + '_ng')
      builder_names.append(name + '_ng_triggered')
    try_ng = "\n".join([
      self.write_ng_pair(self.bb_cfg.consolidate_builder(builder), overrides)
      for builder in builders
    ])
    self.migrated.update(builder_names)
    code = self.minor_text_adjustments(try_ng)
    self.write_file(
      'try_ng.star',
      "load('//lib.star','v8_try_ng_pair')",
      code)

  def write_ng_pair(self, builder, overrides):
    props = self.bb_cfg.properties(builder)
    props.pop('triggers', None)
    header = """v8_try_ng_pair( name='%s',
      triggered_timeout=%s,""" % (
        self.bb_cfg.builder_name(builder)[:-3],
        overrides.get(self.bb_cfg.builder_name(builder)[:-3]),
      )
    return header + self.common_builder_body(builder, props)


  def write_stable_builder(self, builder):
    props = self.bb_cfg.properties(builder)
    builder_name = self.bb_cfg.builder_name(builder)
    header = """v8_branch_coverage_builder( name='%s',
      triggering_policy=%s,
      triggered_by_gitiles=%s,"""  %  (
      builder_name,
      self.write_scheduler_policy("ci.br.stable", self.bb_cfg.builder_name(builder)),
      self.sc_cfg.triggerd_by("ci.br.stable", builder_name) != None
    )
    return header + self.common_builder_body(builder, props)


  def write_builder(self, bk_name, builder):
    props = self.bb_cfg.properties(builder)
    builder_name = self.bb_cfg.builder_name(builder)
    header = """v8_builder( name='%s',
      bucket='%s',
      triggered_by=%s,
      triggering_policy=%s,""" % (
        self.bb_cfg.builder_name(builder),
        bk_name,
        self.sc_cfg.triggerd_by(bk_name, builder_name),
        self.write_scheduler_policy(bk_name, self.bb_cfg.builder_name(builder)),
      )
    return header + self.common_builder_body(builder, props)


  def common_builder_body(self, builder, props):
    dimensnsions = self.bb_cfg.dimensions(builder)
    caches = self.write_caches(builder)
    if dimensnsions.get('host_class','') == 'multibot':
      caches = None
    return COMMON_BUILDER_BODY % (
      self.write_executable(builder.get('recipe', None)),
      builder.get('swarming_tags'),
      dimensnsions,
      self.write_string_or_none(builder.get('service_account', [None])[-1]),
      builder.get('execution_timeout_secs', [None])[-1],
      self.build_numbers_val(builder),
      props,
      caches,
      builder.get('priority', [None])[-1],
    )


  def write_scheduler_policy(self, bucket_name, builder_name):
    job = self.sc_cfg.job(bucket_name, builder_name)
    if job:
      policy = job.get('triggering_policy', None)
      if policy:
        policy = policy[0]
        kind = policy['kind'][0]
        max_concurrent_invocations = policy.get('max_concurrent_invocations', [None])[0]
        max_batch_size = policy.get('max_batch_size', [None])[0]
        log_base = policy.get('log_base', [None])[0]
        return """scheduler.policy(
          kind=scheduler.%s_KIND,
          max_concurrent_invocations=%s,
          max_batch_size=%s,
          log_base=%s,
        )""" % (
          kind,
          max_concurrent_invocations,
          max_batch_size,
          log_base
        )
    return None


  def write_builders4buckets(self):
    code = '\n'.join([
      self.write_builders(
        bucket['name'][0][8:],
        self.bb_cfg.builders(bucket),
      ) for bucket in self.bb_cfg.buckets()
    ])
    code = code.replace("STABLE ", "")
    code = code.replace("BETA ", "")
    code = self.minor_text_adjustments(code)
    self.write_file(
      'others.star',
      "load('//lib.star','v8_builder')",
      code)


  def write_builders(self, bk_name, builders):
    return '\n'.join([
      self.write_builder(
        bk_name,
        self.bb_cfg.consolidate_builder(builder)
      ) for builder in builders if self.bb_cfg.builder_name(builder) not in self.migrated
    ])


  def write_executable(self, recipe):
    recipe = self.bb_cfg.consolidate_recipe(recipe)
    return "{'name':%s, 'cipd_package':%s, 'cipd_version':%s,}" % (
        self.write_string_or_none(self.bb_cfg.recipe_name(recipe)),
        self.write_string_or_none(self.bb_cfg.cipd_package(recipe)),
        self.write_string_or_none(self.bb_cfg.cipd_version(recipe)),
    )

  def write_string_or_none(self, val):
    if not val:
      return None
    return "'%s'" % val

  def build_numbers_val(self, builder):
    bn = builder.get('build_numbers', [None])[-1]
    if bn:
      return bn == 'YES'
    return None


  def write_caches(self, builder):
    if 'caches' in builder:
      caches = builder['caches']
      return "[\n      %s\n    ]" % (
        "\n".join([
          """swarming.cache(
            path='%s',
            name='%s',
            )""" % (
            cache['path'][-1],
            cache['name'][-1],
          ) for cache in caches
        ])
      )
    return None


  def minor_text_adjustments(self, starlark):
    jsonprop_matcher = re.compile(r"': '([\[\{].*?[\]\}])'")
    starlark = re.sub(jsonprop_matcher, r"': \1", starlark)

    spuriousparam_matcher = re.compile(r"\s*\S*=None,", re.MULTILINE)
    starlark = re.sub(spuriousparam_matcher, "", starlark)

    spuriousdicitem_matcher =  re.compile(r"'\S+?':None,", re.MULTILINE or re.DOTALL)
    starlark = re.sub(spuriousdicitem_matcher, "", starlark)

    spuriousemptydict_matcher =  re.compile(r"\s*\S*=\{\s*\},\n", re.MULTILINE or re.DOTALL)
    starlark = re.sub(spuriousemptydict_matcher, "\n", starlark)

    return starlark

def print_dict(d):
  pp = pprint.PrettyPrinter(indent=2, depth=4)
  pp.pprint(d)

def read_file(name):
  with open(name, "r") as f:
    return f.read()

if __name__ == '__main__':
  import os
  os.system('rm -rf out')
  os.system('mkdir -p out')
  StarlarkGenerator().generate()
  os.system('lucicfg fmt')
  os.system('lucicfg generate main.star')
  os.system('lucicfg semanticdiff --output-dir out main.star cr-buildbucket.cfg luci-scheduler.cfg> out/diff.txt')
  os.system('wc -l out/diff.txt')
