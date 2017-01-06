#!/usr/bin/python
# -*- coding: utf8 -*-

import os
import subprocess
import unittest

def with_cwd(dir):
  def wrap(function):
    def decorator(*args, **kwargs):
      cwd = os.getcwd()
      os.chdir(os.path.join(os.path.dirname(__file__), dir))
      result = function(*args, **kwargs)
      os.chdir(cwd)
      return result
    return decorator
  return wrap

shk = os.environ['SHK_PATH']

class IntegrationTest(unittest.TestCase):

  def test_printusage(self):
    output = subprocess.check_output(shk + ' -h; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'usage: shk')

  @with_cwd('no_manifest')
  def test_no_manifest(self):
    output = subprocess.check_output(shk + '; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'error: loading \'build\.ninja\'')

if __name__ == '__main__':
    unittest.main()
