#!/usr/bin/python
# -*- coding: utf8 -*-

import os
import re
import shutil
import subprocess
import unittest

def with_testdir(function):
  dir = re.sub(r'^test_', '', function.__name__)
  tempdir = os.path.join(os.path.dirname(__file__), 'tmpdir')
  def decorator(*args, **kwargs):
    shutil.copytree(dir, tempdir)
    cwd = os.getcwd()
    os.chdir(tempdir)
    try:
      result = function(*args, **kwargs)
    finally:
      os.chdir(cwd)
      shutil.rmtree(tempdir)
    return result
  return decorator

shk = os.environ['SHK_PATH']

def read_file(path):
  with open(path) as f:
    return f.read()

class IntegrationTest(unittest.TestCase):

  def test_printusage(self):
    output = subprocess.check_output(shk + ' -h; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'usage: shk')

  @with_testdir
  def test_no_manifest(self):
    output = subprocess.check_output(shk + '; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'error: loading \'build\.ninja\'')

  @with_testdir
  def test_simple_build(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'data')

if __name__ == '__main__':
    unittest.main()
