

UNRULY_BUILDERS =[
  'V8 Linux64 - builder',
  'V8 Linux64 - debug builder',
  'V8 Fuchsia - builder',
]

class BuildBucketCfg:
  def __init__(self, cfg):
    self.cfg = cfg

  def buckets(self):
    return self.cfg['buckets']

  def mixins(self):
    return self.to_map(self.cfg['builder_mixins'], 'name')

  def to_map(self, lst, key):
    return dict([(item[key][0], item) for item in lst])

  def builder_name(self, builder):
    return builder['name'][0]

  def builders(self, bucket):
    return bucket['swarming'][0]['builders']

  def stable_builders(self):
    stable_bucket = self.get_bucket_by_suffix('stable')
    result = []
    for builder in self.builders(stable_bucket):
      if self.builder_name(builder) not in UNRULY_BUILDERS:
        result.append(builder)
    return result

  def properties(self, builder):
    props = dict()
    recipe = self.consolidate_recipe(builder.get('recipe',[]))
    if 'properties' in recipe:
      self.add_props(props, recipe['properties'])
    if 'properties_j' in recipe:
      self.add_props(props, recipe['properties_j'])
    return props

  def dimensions(self, builder):
    dims = builder.get('dimensions', [])
    dim_dict = dict()
    for item in dims:
      splited = item.split(":", 1)
      dim_dict[splited[0]] = splited[1]
    return dim_dict

  def recipe_name(self, recipe):
    return recipe.get('name',[None])[-1]

  def cipd_package(self, recipe):
    return recipe.get('cipd_package',[None])[-1]

  def cipd_version(self, recipe):
    return recipe.get('cipd_version',[None])[-1]

  def get_bucket_by_suffix(self, suffix):
    for bucket in self.buckets():
      if bucket['name'][0].endswith(suffix):
        return bucket

  def trytriggered_builders_timeouts(self):
    result = dict()
    triggered_bucket = self.get_bucket_by_suffix('triggered')
    for builder in self.builders(triggered_bucket):
      if self.builder_name(builder).endswith('_ng_triggered'):
        if 'execution_timeout_secs' in builder:
          result[self.builder_name(builder)[:-13]] = builder['execution_timeout_secs'][0]
    return result

  def ng_builders(self):
    result = []
    triggered_bucket = self.get_bucket_by_suffix('try')
    for builder in self.builders(triggered_bucket):
      builder_name = self.builder_name(builder)
      if builder_name.endswith('_ng') and builder_name[:-3] not in UNRULY_BUILDERS:
        result.append(builder)
    return result

  def consolidate_builder(self, builder):
    builder_props = self.consolidate_mixins(builder) + builder.items()
    consolidated = self.duplicate_key_dict_builder(builder_props)
    return consolidated

  def consolidate_mixins(self, referer):
    mixins = self.mixins()
    consolidated_list = []
    for ref in referer.pop('mixins', []):
      mixin_copy = mixins[ref].copy()
      inner_list = self.consolidate_mixins(mixin_copy)
      del mixin_copy['name']
      consolidated_list = consolidated_list + inner_list + mixin_copy.items()
    return consolidated_list

  def duplicate_key_dict_builder(self, lst):
    res = dict()
    for k,v in lst:
      if k in res:
        res[k] = res[k] + v
      else:
        res[k] = v
    return res

  def consolidate_recipe(self, recipe):
    as_list = []
    for r in recipe:
      as_list.extend(r.items())
    return self.duplicate_key_dict_builder(as_list)

  def add_props(self, p_dict, p_list):
    for item in p_list:
      if type(item) is str:
        splited = item.split(":", 1)
        p_dict[splited[0]] = self.flatten_value(splited[1])
      elif type(item) is dict:
        for k,v in item.items():
          p_dict[k] = self.flatten_value(v)
      else:
        print("UNTREATED PROPERTY TYPE")

  def flatten_value(self, value):
    if type(value) is dict:
      res = dict()
      for k, v in value.items():
        res[k] = self.flatten_value(v)
      return res
    elif type(value) is list and len(value) == 1:
      return self.flatten_value(value[0])
    elif value == "true":
      return True
    elif value == "false":
      return False
    elif type(value) is str and value.isdigit():
      return int(value)
    else:
      return value

class SchedulerCfg:
  def __init__(self, cfg):
    self.cfg = cfg
    for t in self.cfg['job']:
      for k,v in t.items():
        pass#print k