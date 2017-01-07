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

def write_file(path, contents):
  with open(path, 'w') as f:
    f.write(contents)

def run_cmd(cmd):
  return subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True)

def run_cmd_expect_fail(cmd):
  return run_cmd(cmd + '; if [ $? -eq 0 ]; then exit 1; else exit 0; fi')

class IntegrationTest(unittest.TestCase):

  def test_printusage(self):
    output = run_cmd_expect_fail(shk + ' -h')
    self.assertRegexpMatches(output, r'usage: shk')

  def test_printtools(self):
    output = run_cmd(shk + ' -t list; exit 0')
    self.assertRegexpMatches(output, r'shk subtools:')
    self.assertRegexpMatches(output, r'clean')
    self.assertRegexpMatches(output, r'commands')
    self.assertRegexpMatches(output, r'deps')
    self.assertRegexpMatches(output, r'query')
    self.assertRegexpMatches(output, r'targets')
    self.assertRegexpMatches(output, r'recompact')

  @with_testdir('no_manifest')
  def test_no_manifest(self):
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'error: loading \'build\.ninja\'')

  @with_testdir('simple_build')
  def test_simple_build(self):
    run_cmd(shk)
    self.assertEqual(read_file('out'), 'data')

  @with_testdir('mkdir')
  def test_mkdir_build(self):
    run_cmd(shk)
    self.assertTrue(os.path.isdir('out'))

  @with_testdir('simple_build')
  def test_simple_rebuild(self):
    run_cmd(shk)
    output = run_cmd(shk)
    self.assertEqual(read_file('out'), 'data')
    self.assertRegexpMatches(output, 'no work to do')

  @with_testdir('target_in_subdir')
  def test_rebuild_with_moved_target(self):
    run_cmd(shk)
    self.assertEqual(read_file('dir/out'), 'hello')

    # Change the manifest
    manifest = read_file('build.ninja')
    write_file('build.ninja', manifest.replace('dir/out', 'dir2/out'))

    run_cmd(shk)
    # Since the target has moved, the old output should have been removed,
    # including its directory that was created by the build system.
    self.assertFalse(os.path.exists('dir'))
    self.assertEqual(read_file('dir2/out'), 'hello')

  @with_testdir('simple_build')
  def test_noop_rebuild_after_touching_input(self):
    run_cmd(shk)
    time.sleep(1)  # Wait, because the fs mtime only has second resolution
    os.utime('in', None)  # Touch the input file
    output = run_cmd(shk)
    self.assertRegexpMatches(output, 'no work to do')

  @with_testdir('simple_build')
  def test_simple_noop_build(self):
    run_cmd(shk + ' -n')
    self.assertFalse(os.path.exists('out'))

  @with_testdir('append_output')
  def test_delete_before_rebuilding(self):
    run_cmd(shk)
    self.assertEqual(read_file('out'), 'hello')
    write_file('in', 'changed')
    run_cmd(shk)
    self.assertEqual(read_file('out'), 'changed')

  @with_testdir('undeclared_dependency')
  def test_undeclared_dependency(self):
    run_cmd(shk)
    self.assertEqual(read_file('out'), 'input_file')
    write_file('in', 'changed')
    run_cmd(shk)
    self.assertEqual(read_file('out'), 'changed')

  @with_testdir('cyclic_dependency')
  def test_cyclic_dependency(self):
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'Cyclic dependency\?')

  @with_testdir('cyclic_dependency')
  def test_cyclic_dependency_specified_target(self):
    output = run_cmd_expect_fail(shk + ' out')
    self.assertRegexpMatches(output, r'Dependency cycle: in -> out -> in$')

  @with_testdir('duplicate_step_outputs')
  def test_duplicate_step_outputs(self):
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'Multiple rules generate out')

  @with_testdir('canonicalized_duplicate_step_outputs')
  def test_canonicalized_duplicate_step_outputs(self):
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'Multiple rules generate (a/\.\./)?out')

  @with_testdir('link_duplicate_step_outputs')
  def test_symlink_duplicate_step_outputs(self):
    os.symlink('a', 'b')
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'Multiple rules generate [ab]/out')

  @with_testdir('link_duplicate_step_outputs')
  def test_hardlink_duplicate_step_outputs(self):
    os.mkdir('b')
    write_file('a/out', '')
    os.link('a/out', 'b/out')
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'Multiple rules generate [ab]/out')

  @with_testdir('link_duplicate_step_outputs')
  def test_symlink_step_outputs(self):
    os.mkdir('b')
    write_file('a/out', '')
    run_cmd('ln -s ../a/out b/out')
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'Multiple rules generate [ab]/out')

  @with_testdir('canonicalize_symlink')
  def test_symlink_step_outputs(self):
    # Test that paths are not canonicalized without consulting the file system
    run_cmd('ln -s ../a/b one/symlink')
    output = run_cmd_expect_fail(shk)
    self.assertRegexpMatches(output, r'Multiple rules generate (one/symlink/\.\.|a)/out')

  @with_testdir('symlink')
  def test_symlink(self):
    run_cmd(shk)
    self.assertTrue(os.path.exists('a'))
    self.assertTrue(os.path.exists('b'))

  @with_testdir('symlink')
  def test_symlink_rebuild(self):
    run_cmd(shk)
    run_cmd(shk)
    self.assertTrue(os.path.exists('a'))
    self.assertTrue(os.path.exists('b'))

  @with_testdir('simple_build')
  def test_specify_manifest(self):
    os.rename('build.ninja', 'manifest.ninja')
    run_cmd(shk + ' -f manifest.ninja')
    self.assertEqual(read_file('out'), 'data')

  @with_testdir('target_in_subdir')
  def test_target_in_subdir(self):
    run_cmd(shk)
    self.assertEqual(read_file('dir/out'), 'hello')

  @with_testdir('simple_build')
  def test_full_clean(self):
    run_cmd(shk)
    self.assertTrue(os.path.exists('out'))
    self.assertTrue(os.path.exists('.shk_log'))
    output = run_cmd(shk + ' -t clean')
    self.assertFalse(os.path.exists('out'))
    self.assertFalse(os.path.exists('.shk_log'))
    self.assertRegexpMatches(output, r'cleaned 2 files\.')

  @with_testdir('simple_build')
  def test_full_clean_again(self):
    run_cmd(shk)
    run_cmd(shk + ' -t clean')
    output = run_cmd(shk + ' -t clean')
    self.assertRegexpMatches(output, r'cleaned 0 files\.')

  @with_testdir('simple_build')
  def test_single_target_clean(self):
    run_cmd(shk)
    self.assertTrue(os.path.exists('out'))
    self.assertTrue(os.path.exists('.shk_log'))
    output = run_cmd(shk + ' -t clean out')
    self.assertFalse(os.path.exists('out'))
    self.assertTrue(os.path.exists('.shk_log'))
    self.assertRegexpMatches(output, r'cleaned 1 file\.')

if __name__ == '__main__':
    unittest.main()
