#!/usr/bin/python
# -*- coding: utf8 -*-

import os
import re
import shutil
import subprocess
import unittest

def with_testdir(function):
  return with_specific_testdir(re.sub(r'^test_', '', function.__name__))

def with_specific_testdir(dir):
  tempdir = os.path.join(os.path.dirname(__file__), 'tmpdir')
  def wrap(function):
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
  return wrap

shk = os.environ['SHK_PATH']

def read_file(path):
  with open(path) as f:
    return f.read()

class IntegrationTest(unittest.TestCase):

  def test_printusage(self):
    output = subprocess.check_output(shk + ' -h; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'usage: shk')

  def test_printtools(self):
    output = subprocess.check_output(shk + ' -t list; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'shk subtools:')
    self.assertRegexpMatches(output, r'clean')
    self.assertRegexpMatches(output, r'commands')
    self.assertRegexpMatches(output, r'deps')
    self.assertRegexpMatches(output, r'query')
    self.assertRegexpMatches(output, r'targets')
    self.assertRegexpMatches(output, r'recompact')

  @with_testdir
  def test_no_manifest(self):
    output = subprocess.check_output(shk + '; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'error: loading \'build\.ninja\'')

  @with_testdir
  def test_simple_build(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'data')

  @with_specific_testdir('simple_build')
  def test_simple_rebuild(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    output = subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'data')
    self.assertRegexpMatches(output, 'no work to do')

  @with_specific_testdir('simple_build')
  def test_simple_noop_build(self):
    subprocess.check_output(shk + ' -n', stderr=subprocess.STDOUT, shell=True)
    self.assertFalse(os.path.exists('out'))

  @with_specific_testdir('append_output')
  def test_delete_before_rebuilding(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'hello\n')

if __name__ == '__main__':
    unittest.main()
