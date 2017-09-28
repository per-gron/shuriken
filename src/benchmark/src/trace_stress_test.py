#!/usr/bin/python
# -*- coding: utf8 -*-

# Copyright 2017 Per Grön. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Program that stress tests shk-trace by tracing lots of commands in parallel
# and verifying the trace output.

import os
import time
import threading
import pipes
import re
import shutil
import subprocess
import sys
import sets

def with_testdir():
  tempdir = os.path.join(os.path.dirname(__file__), 'tmpdir')
  def wrap(function):
    def decorator(*args, **kwargs):
      if os.path.exists(tempdir):
        shutil.rmtree(tempdir)
      os.mkdir(tempdir)
      cwd = os.getcwd()
      os.chdir(tempdir)
      try:
        result = function(*args, **kwargs)
      finally:
        os.chdir(cwd)
        shutil.rmtree(tempdir)
      return result
    return decorator
  return wrap

def with_trace_server():
  def wrap(function):
    def decorator(*args, **kwargs):
      trace_server = subprocess.Popen(
          [shkTrace, '-s', '--suicide-when-orphaned'], stdout=subprocess.PIPE)
      # Wait for server to actually start (would be better to parse output)
      time.sleep(1)
      try:
        result = function(*args, **kwargs)
      finally:
        trace_server.terminate()
      return result
    return decorator
  return wrap

shkTraceFb = os.environ.get(
    'SHK_TRACE_FB_PATH',
    os.getcwd() + '/../../../build/src/shk-util/include/util')
sys.path.insert(0, shkTraceFb)
sys.path.insert(0, '../../../third_party/flatbuffers/python')
import ShkTrace.Trace
import ShkTrace.EventType

shkTrace = os.environ.get(
    'SHK_TRACE_PATH',
    os.getcwd() + '/../../../build/bin/shk-trace')

sys.path.insert(0, '/path/to/application/app/folder')

def read_file(path):
  with open(path) as f:
    return f.read()

def write_file(path, contents):
  with open(path, 'w') as f:
    f.write(contents)

def run_cmd(cmd):
  return subprocess.check_call(cmd, stderr=subprocess.STDOUT, shell=True)

def parse_trace(data):
  trace = ShkTrace.Trace.Trace.GetRootAsTrace(data, 0)
  inputs = sets.Set()
  for i in range(0, trace.InputsLength()):
    input = trace.Inputs(i)
    input = re.sub(r'T/hello\d?-......\.tbd', 'T/hello-XXXXXX.tbd', input)
    inputs.add(input)

  outputs = sets.Set()
  for i in range(0, trace.OutputsLength()):
    outputs.add(trace.Outputs(i))

  error = ''
  for i in range(0, trace.ErrorsLength()):
    error += trace.Errors(i) + '\n'

  if error:
    raise Exception(error)

  return [inputs, outputs]

def trace_cmd(cmd, num):
  file = "out." + str(num) + ".trace"
  run_cmd(shkTrace + " -f " + file + " -c " + pipes.quote(cmd))
  return parse_trace(bytearray(read_file(file)))

class CompileManyThread (threading.Thread):
  def __init__(self, num):
    threading.Thread.__init__(self)
    self.num = num

  def run(self):
    file = "hello" + str(self.num) + ".c"
    write_file(
        file,
        "#include <stdio.h>\nint main(){printf(\"hello\\n\");return 0;}")
    cmd = "clang " + file + " -o " + file + ".exe"
    original = trace_cmd(cmd, self.num)
    for i in range(50000):
      print i
      trace = trace_cmd(cmd, self.num)
      if original != trace:
        print "DIFFERENCE!"
        print "Only in original inputs: " + str(original[0] - trace[0])
        print "Only in now inputs: " + str(trace[0] - original[0])
        print "Only in original outputs: " + str(original[1] - trace[1])
        print "Only in now outputs: " + str(trace[1] - original[1])
        break

@with_testdir()
@with_trace_server()
def go():
  threads = []

  for i in range(8):
    threads.append(CompileManyThread(i))

  for t in threads:
    t.start()

  for t in threads:
    t.join()

go()
