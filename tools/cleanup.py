
import json
import re

def cleanup_cfg(proto_text):
  comment_matcher = re.compile(r"^(.*?)#.*$", re.M)
  proto_text = re.sub(comment_matcher, r"\1", proto_text)

  onelineblock_matcher = re.compile(r"(\n *)(\S* \{) (\S*: \S*) (\})", re.DOTALL)
  proto_text = re.sub(onelineblock_matcher, r"\1\2\1  \3\1\4", proto_text)

  multiline_value_matcher = re.compile(r"<<END\n(.+?)\n\s*END\n", re.DOTALL)
  proto_text = re.sub(multiline_value_matcher, reformat_multiline_value, proto_text)

  trailingwhitespace_matcher = re.compile(r"\s*\n", re.DOTALL)
  proto_text = re.sub(trailingwhitespace_matcher, "\n", proto_text)

  with open('out/clean.cfg', 'w') as f:
    f.write(proto_text)
  return proto_text

def reformat_multiline_value(match):
  splited = match.group(1).split(":", 1)
  json_text = '{ "%s": %s}' % (splited[0].strip(),splited[1])
  return json.dumps(json.loads(json_text))+"\n"