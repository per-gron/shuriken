#!/usr/bin/python
# -*- coding: utf8 -*-

import glob
import os
import sys
import time

def write_file(path, contents):
  with open(path, 'w') as f:
    f.write(contents)

lock_file = 'running_' + sys.argv[1]
write_file(lock_file, 'lock')
if len(glob.glob('./running_*')) > 2:
  raise 'Too many instances running in parallel!'
time.sleep(1)
os.remove(lock_file)
