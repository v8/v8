
import re

def prototext2json(prototext):
  json_text = json_format(prototext)

  commacurly_matcher1 = re.compile(r",\s*\},\s*", re.DOTALL)
  json_text = re.sub(commacurly_matcher1, "}", json_text)
  
  with open('out/clean.json', 'w') as f:
    f.write(json_text)
  return json_text

def json_format(proto_text):
  proto_text = fix_properties(proto_text)
  proto_text = fix_blocks(proto_text)
  return "{\n%s\n},\n" % (proto_text)

def fix_properties(proto_text):
  pps_matcher1 = re.compile(r"([\w]+): (\{.*?\})\n", re.DOTALL)
  proto_text = re.sub(pps_matcher1, prop_fix1, proto_text)

  pps_matcher2 = re.compile(r"([\w]+): (\".*?\")\n", re.DOTALL)
  proto_text = re.sub(pps_matcher2, prop_fix1, proto_text)

  pps_matcher3 = re.compile(r"([\w]+): (.*?)\n", re.DOTALL)
  return re.sub(pps_matcher3, prop_fix2, proto_text)

def prop_fix1(match):
  prop_name = match.group(1)
  prop_value = match.group(2)
  return '"%s": %s,\n' % (prop_name.strip(), prop_value.strip())


def prop_fix2(match):
  prop_name = match.group(1)
  prop_value = match.group(2)
  return '"%s": "%s",\n' % (prop_name.strip(), prop_value.strip())


def fix_blocks(proto_text):
  fullblock_matcher = re.compile(r"([\w]+) \{\n(.*?)\n\}\n", re.DOTALL | re.MULTILINE)
  proto_text = re.sub(fullblock_matcher, block_fix, proto_text)
  return proto_text

def block_fix(match):
  prop_name = match.group(1)
  prop_value = match.group(2)
  prop_value = allign_to_left(prop_value) + "\n"
  prop_value = json_format(prop_value)
  prop_value = re.sub(r",[\s\n]*\}", "}", prop_value, flags=re.DOTALL|re.MULTILINE)
  return '"%s": %s' % (prop_name, prop_value)

def allign_to_left(text):
  return re.sub(r"^  ", "", text, flags=re.MULTILINE)