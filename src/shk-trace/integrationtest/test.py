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
helper = os.path.join(os.path.dirname(shkTrace), 'shktrace_integrationtest_helper')

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
    self.assertIn('read ' + os.getcwd() + '/file', trace)

  @with_testdir()
  def test_write_file(self):
    trace = trace_cmd("touch file")
    self.assertIn('write ' + os.getcwd() + '/file', trace)

  @with_testdir()
  def test_create_file(self):
    trace = trace_cmd("echo > file")
    self.assertIn('create ' + os.getcwd() + '/file', trace)

  @with_testdir()
  def test_delete_file(self):
    write_file('file', '')
    trace = trace_cmd("rm file")
    self.assertIn('delete ' + os.getcwd() + '/file', trace)

  @with_testdir()
  def test_create_and_delete_file(self):
    trace = trace_cmd("echo > file && rm file")
    self.assertNotIn(os.getcwd() + '/file', trace)

  @with_testdir()
  def test_move_file(self):
    write_file('file1', '')
    trace = trace_cmd("mv file1 file2")
    self.assertIn('delete ' + os.getcwd() + '/file1', trace)
    self.assertIn('create ' + os.getcwd() + '/file2', trace)

  @with_testdir()
  def test_create_and_move_file(self):
    trace = trace_cmd("echo > file1 && mv file1 file2")
    self.assertNotIn(os.getcwd() + '/file1', trace)
    self.assertIn('create ' + os.getcwd() + '/file2', trace)

  @with_testdir()
  def test_append_to_file(self):
    trace = trace_cmd("echo >> file")
    self.assertIn('write ' + os.getcwd() + '/file', trace)

  @with_testdir()
  def test_read_then_append_to_file(self):
    write_file('file', '')
    trace = trace_cmd("cat file && echo >> file")
    self.assertIn('read ' + os.getcwd() + '/file', trace)
    self.assertIn('write ' + os.getcwd() + '/file', trace)

  @with_testdir()
  def test_executable_counts_as_input(self):
    trace = trace_cmd("ls")
    self.assertIn('read /bin/ls', trace)

  @with_testdir()
  def test_access(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' access')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_access_nonexisting(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' access')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fork_inherit_fd(self):
    trace = trace_cmd(helper + ' fork_inherit_fd')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_link(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' link')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_link_error(self):
    write_file('input', '')
    write_file('output', '')
    trace = trace_cmd(helper + ' link')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertNotIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_linkat(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    write_file('dir1/input', '')
    trace = trace_cmd(helper + ' linkat')
    self.assertIn('read ' + os.getcwd() + '/dir1/input', trace)
    self.assertIn('create ' + os.getcwd() + '/dir2/output', trace)

  @with_testdir()
  def test_linkat_error(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    write_file('dir1/input', '')
    write_file('dir2/output', '')
    trace = trace_cmd(helper + ' linkat')
    self.assertIn('read ' + os.getcwd() + '/dir1/input', trace)
    self.assertNotIn('create ' + os.getcwd() + '/dir2/output', trace)

  @with_testdir()
  def test_linkat_error(self):
    write_file('input', '')
    write_file('output', '')
    trace = trace_cmd(helper + ' link')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertNotIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_readlink(self):
    os.symlink('abc', 'input')
    trace = trace_cmd(helper + ' readlink')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_readlink_error(self):
    trace = trace_cmd(helper + ' readlink')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_readlinkat(self):
    os.mkdir('dir')
    os.symlink('abc', 'input')
    trace = trace_cmd(helper + ' readlinkat')
    self.assertIn('read ' + os.getcwd() + '/dir/../input', trace)

  @with_testdir()
  def test_readlinkat_error(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' readlinkat')
    self.assertIn('read ' + os.getcwd() + '/dir/../input', trace)

  @with_testdir()
  def test_unlink(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' unlink')
    self.assertIn('delete ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_unlinkat(self):
    os.mkdir('dir')
    write_file('input', '')
    trace = trace_cmd(helper + ' unlinkat')
    self.assertIn('delete ' + os.getcwd() + '/dir/../input', trace)

  @with_testdir()
  def test_unlinkat_dir(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' unlinkat_dir')
    self.assertIn('delete ' + os.getcwd() + '/dir', trace)

if __name__ == '__main__':
    unittest.main()
