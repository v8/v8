import cfg_handlers

s = cfg_handlers.SchedulerCfg()

to_rename = set()
for job_name in s.jobs.keys():
  if job_name.startswith("STABLE "):
    to_rename.add(job_name[7:])
  if job_name.startswith("BETA "):
    to_rename.add(job_name[5:])

with open('luci-scheduler.cfg', 'r') as f:
  content = f.read()

for name in list(to_rename):
  print name
  content = content.replace("id: \"%s\"" % name, "id: \"ci-%s\"" % name)
  content = content.replace("triggers: \"%s\"" % name, "triggers: \"ci-%s\"" % name)

  content = content.replace("id: \"BETA %s\"" % name, "id: \"ci.br.beta-%s\"" % name)
  content = content.replace("triggers: \"BETA %s\"" % name, "triggers: \"ci.br.beta-%s\"" % name)

  content = content.replace("id: \"STABLE %s\"" % name, "id: \"ci.br.stable-%s\"" % name)
  content = content.replace("triggers: \"STABLE %s\"" % name, "triggers: \"ci.br.stable-%s\"" % name)

with open('luci-scheduler.cfg', 'w') as f:
  f.write(content)
