

import cleanup
import dupkey_json
import jsonify
import collections

def prototext2dict(file_name):
  with open(file_name , 'r') as f:
    proto_text = f.read()
    proto_text = cleanup.cleanup_cfg(proto_text)
    json_text = jsonify.prototext2json(proto_text)
    proto_dict = dupkey_json.loads(json_text)
    return proto_dict