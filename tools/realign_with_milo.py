import cfg_handlers

milo = cfg_handlers.MiloCfg()

result = []
for c in milo.cfg['consoles']:
  if c['id'][0].startswith('br.stable'):
    for b in c['builders']:
      if b['name'][0].startswith('buildbucket/luci.v8.ci.br.stable/'):
        result.append(b['name'][0][33:])

bb = cfg_handlers.BuildBucketCfg()

bk = bb.get_bucket_by_suffix("ci.br.stable")
for b in bb.builders(bk):
  if bb.builder_name(b) not in result:
    print bb.builder_name(b)