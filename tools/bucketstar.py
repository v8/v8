#!/usr/bin/env python
# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
A collection of helper functions for transforming text proto files to
other formats.
"""

import cleanup
import dupkey_json
import jsonify
import collections

import pprint
import json

import re
import sys


def read_proto_file(file_name):
  with open(file_name , 'r') as f:
    proto_text = f.read()
    proto_text = cleanup.cleanup_cfg(proto_text)
    json_text = jsonify.prototext2json(proto_text)
    proto_dict = dupkey_json.loads(json_text)
    return proto_dict


def write_starlark(cfg):
  buckets = cfg['buckets']
  mixins = to_map(cfg['builder_mixins'], 'name')
  return write_buckets(buckets) + '\n' + write_builders_bk(buckets, mixins)


def to_map(lst, key):
  return dict([(item[key][0], item) for item in lst])


def write_buckets(buckets):
  return '\n'.join([
    "luci.bucket(name='%s', acls=%s_acls)" % (
      remove_prefix(bucket['name'][0], 'luci.v8.'),
      bucket['acl_sets'][0]
    ) for bucket in buckets
  ])


def remove_prefix(text, prefix):
  if text.startswith(prefix):
    return text[len(prefix):]
  return text


def write_builders_bk(buckets, mixins):
  return '\n'.join([
    write_builders(
      remove_prefix(bucket['name'][0], 'luci.v8.'),
      bucket['swarming'][0]['builders'],
      bucket['swarming'][0]['builder_defaults'][0],
      mixins
    ) for bucket in buckets
  ])


def write_builders(bk_name, builders, defaults, mixins):
  return '\n'.join([
    write_builder(
      bk_name,
      consolidate_builder(builder, defaults, mixins)
    ) for builder in builders
  ])


def consolidate_builder(builder, defaults, mixins):
  builder_props = defaults.items() + consolidate_mixins(defaults.copy(), mixins) + consolidate_mixins(builder, mixins) + builder.items()
  consolidated = duplicate_key_dict_builder(builder_props)
  return consolidated


def consolidate_mixins(referer, mixins):
  consolidated_list = []
  for ref in referer.pop('mixins', []):
    mixin_copy = mixins[ref].copy()
    inner_list = consolidate_mixins(mixin_copy, mixins)
    del mixin_copy['name']
    consolidated_list = consolidated_list + inner_list + mixin_copy.items()
  return consolidated_list


def duplicate_key_dict_builder(lst):
  res = dict()
  for k,v in lst:
    if k in res:
      res[k] = res[k] + v
    else:
      res[k] = v
  return res


def write_builder(bk_name, builder):
  builder_def = """luci.builder( name='%s',
    bucket='%s',
    executable=%s,
    swarming_tags=%s,
    dimensions=%s,
    service_account='%s',
    execution_timeout=time.second * %s,
    build_numbers=%s,
    properties=%s,
    caches=%s,
    priority=%s,
  )"""
  return builder_def % (
      builder['name'][0],
      bk_name,
      write_executable(builder['recipe']),
      builder['swarming_tags'],
      consolidate_dimensions(builder['dimensions']),
      builder['service_account'][-1],
      builder['execution_timeout_secs'][-1],
      build_numbers_val(builder),
      write_properties(builder),
      write_caches(builder),
      builder.get('priority', [None])[-1],
    )

def consolidate_dimensions(dims):
  dim_dict = dict()
  for item in dims:
    splited = item.split(":", 1)
    dim_dict[splited[0]] = splited[1]
  return dim_dict



def write_executable(recipe):
  recipe = consolidate_recipe(recipe)
  return "luci.recipe(name='%s',\n    cipd_package='%s',\n    cipd_version='%s')" % (
      recipe['name'][-1],
      recipe['cipd_package'][-1],
      recipe['cipd_version'][-1],
  )

def consolidate_recipe(recipe):
  as_list = []
  for r in recipe:
    as_list.extend(r.items())
  return duplicate_key_dict_builder(as_list)

def build_numbers_val(builder):
  bn = builder.get('build_numbers', [None])[-1]
  if bn:
    return bn == 'YES'
  return None

def write_properties(builder):
  props = dict()
  recipe = consolidate_recipe(builder['recipe'])
  if 'properties' in recipe:
    add_props(props, recipe['properties'])
  if 'properties_j' in recipe:
    add_props(props, recipe['properties_j'])
  return props

def add_props(p_dict, p_list):
  for item in p_list:
    if type(item) is str:
      splited = item.split(":", 1)
      p_dict[splited[0]] = flatten_value(splited[1])
    elif type(item) is dict:
      for k,v in item.items():
        p_dict[k] = flatten_value(v)
    else:
      print("UNTREATED PROPERTY TYPE")

def flatten_value(value):
  if type(value) is dict:
    res = dict()
    for k, v in value.items():
      res[k] = flatten_value(v)
    return res
  elif type(value) is list and len(value) == 1:
    return flatten_value(value[0])
  elif value == "true":
    return True
  elif value == "false":
    return False
  elif type(value) is str and value.isdigit():
    return int(value)
  else:
    return value

def write_caches(builder):
  if 'caches' in builder:
    caches = builder['caches']
    return "[\n      %s\n    ]" % (
      "\n".join([
        "swarming.cache(path='%s', name='%s', wait_for_warm_cache=%s)" % (
          cache['path'][-1],
          cache['name'][-1],
          None,#cache.get('wait_for_warm_cache_secs', [None])[-1]
        ) for cache in caches
      ])
    )
  return None


def minor_text_adjustments(starlark):
  jsonprop_matcher = re.compile(r"': '([\[\{].*?[\]\}])'")
  return re.sub(jsonprop_matcher, r"': \1", starlark)

def print_dict(d):
  pp = pprint.PrettyPrinter(indent=2, depth=4)
  pp.pprint(d)

if __name__ == '__main__':
  bucket_file = 'cr-buildbucket.cfg'
  cfg = read_proto_file(bucket_file)
  starlark = write_starlark(cfg)
  starlark = minor_text_adjustments(starlark)

  with open('buckets.star', "w") as f:
    f.write("load('//lib.star', 'waterfall_acls', 'tryserver_acls')\n")
    f.write(starlark)
  import os
  os.system('lucicfg generate main.star')
  os.system('mkdir -p out')
  os.system('lucicfg semanticdiff --output-dir out main.star %s > out/diff.txt' % bucket_file)
  os.system('wc -l out/diff.txt')
