import ast

with open('/home/liviurau/fetched/v8/v8/infra/mb/mb_config.pyl') as f:
  cfg = ast.literal_eval(f.read())
  all_builders = []
  for k,v in cfg['masters'].items()[1:]:
    all_builders.extend(v.items())
  all_builders = dict(all_builders)
  buildconf_dict = dict()
  for k,v in all_builders.items():
    old_val = buildconf_dict.get(v, None)
    if not old_val:
      old_val = [k]
    else:
      old_val.append(k)
    buildconf_dict[v] = old_val
  for v in buildconf_dict.values():
    if len(v) > 1:
      print(v)
