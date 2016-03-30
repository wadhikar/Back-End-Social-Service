#!/usr/bin/python2

import json

line = raw_input()
obj = json.loads(line)
print obj["token"]
