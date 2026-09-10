import json


def loads(data):
    def hook(pairs):
        out = {}
        for k, v in pairs:
            if k in out:
                raise ValueError("duplicate JSON key: " + k)
            out[k] = v
        return out

    return json.loads(data, object_pairs_hook=hook)
