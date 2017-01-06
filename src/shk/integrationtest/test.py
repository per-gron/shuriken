#!/usr/bin/python
# -*- coding: utf8 -*-

import os
import re
import shutil
import subprocess
import time
import unittest

def with_testdir(dir):
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

  @with_testdir('no_manifest')
  def test_no_manifest(self):
    output = subprocess.check_output(shk + '; exit 0', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'error: loading \'build\.ninja\'')

  @with_testdir('simple_build')
  def test_simple_build(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'data')

  @with_testdir('mkdir')
  def test_mkdir_build(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertTrue(os.path.isdir('out'))

  @with_testdir('simple_build')
  def test_simple_rebuild(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    output = subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'data')
    self.assertRegexpMatches(output, 'no work to do')

  @with_testdir('target_in_subdir')
  def test_rebuild_with_moved_target(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('dir/out'), 'hello')

    # Change the manifest
    manifest = read_file('build.ninja')
    with open('build.ninja', 'w') as f:
      f.write(manifest.replace('dir/out', 'dir2/out'))

    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    # Since the target has moved, the old output should have been removed,
    # including its directory that was created by the build system.
    self.assertFalse(os.path.exists('dir'))
    self.assertEqual(read_file('dir2/out'), 'hello')

  @with_testdir('simple_build')
  def test_noop_rebuild_after_touching_input(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    time.sleep(1)  # Wait, because the fs mtime only has second resolution
    os.utime('in', None)  # Touch the input file
    output = subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, 'no work to do')

  @with_testdir('simple_build')
  def test_simple_noop_build(self):
    subprocess.check_output(shk + ' -n', stderr=subprocess.STDOUT, shell=True)
    self.assertFalse(os.path.exists('out'))

  @with_testdir('append_output')
  def test_delete_before_rebuilding(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'hello')
    with open('in', 'w') as f:
      f.write('changed')
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'changed')

  @with_testdir('simple_build')
  def test_specify_manifest(self):
    os.rename('build.ninja', 'manifest.ninja')
    subprocess.check_output(shk + ' -f manifest.ninja', stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('out'), 'data')

  @with_testdir('target_in_subdir')
  def test_target_in_subdir(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertEqual(read_file('dir/out'), 'hello')

  @with_testdir('simple_build')
  def test_full_clean(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertTrue(os.path.exists('out'))
    self.assertTrue(os.path.exists('.shk_log'))
    output = subprocess.check_output(shk + ' -t clean', stderr=subprocess.STDOUT, shell=True)
    self.assertFalse(os.path.exists('out'))
    self.assertFalse(os.path.exists('.shk_log'))
    self.assertRegexpMatches(output, r'cleaned 2 files\.')

  @with_testdir('simple_build')
  def test_full_clean_again(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    subprocess.check_output(shk + ' -t clean', stderr=subprocess.STDOUT, shell=True)
    output = subprocess.check_output(shk + ' -t clean', stderr=subprocess.STDOUT, shell=True)
    self.assertRegexpMatches(output, r'cleaned 0 files\.')

  @with_testdir('simple_build')
  def test_single_target_clean(self):
    subprocess.check_output(shk, stderr=subprocess.STDOUT, shell=True)
    self.assertTrue(os.path.exists('out'))
    self.assertTrue(os.path.exists('.shk_log'))
    output = subprocess.check_output(shk + ' -t clean out', stderr=subprocess.STDOUT, shell=True)
    self.assertFalse(os.path.exists('out'))
    self.assertTrue(os.path.exists('.shk_log'))
    self.assertRegexpMatches(output, r'cleaned 1 file\.')

if __name__ == '__main__':
    unittest.main()
