#!/usr/bin/python
# -*- coding: utf8 -*-

import os
import pipes
import re
import shutil
import subprocess
import time
import unittest

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

shkTrace = os.environ['SHK_TRACE_PATH']

def read_file(path):
  with open(path) as f:
    return f.read()

def write_file(path, contents):
  with open(path, 'w') as f:
    f.write(contents)

def run_cmd(cmd):
  return subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)

def run_cmd_expect_fail(cmd):
  return run_cmd(cmd + '; if [ $? -eq 0 ]; then exit 1; else exit 0; fi')

def trace_cmd(cmd):
  run_cmd(shkTrace + " -f trace.txt -c " + pipes.quote(cmd))
  return read_file('trace.txt')

class IntegrationTest(unittest.TestCase):

  def test_printusage(self):
    output = run_cmd_expect_fail(shkTrace + ' -h')
    self.assertRegexpMatches(output, r'usage: shk')

  @with_testdir()
  def test_read_file(self):
    write_file('file', '')
    trace = trace_cmd("cat file")
    self.assertRegexpMatches(trace, 'read ' + os.getcwd() + '/file')

if __name__ == '__main__':
    unittest.main()
