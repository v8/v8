import json

def loads(s):
  res = json.JSONDecoder(object_pairs_hook=parse_object_pairs).decode(s)
  with open('out/clean.py', 'w') as f:
    f.write(str(res))
  return res

def parse_object_pairs(pairs):
  from collections import defaultdict
  res = defaultdict(list)
  for k, v in pairs:
    k = to_str(k)
    v = to_str(v)
    res[k].append(v)
  return dict(res)

def to_str(s):
  if type(s) == unicode:
    return str(s)
  return s