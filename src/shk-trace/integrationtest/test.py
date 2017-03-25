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
  def test_chdir(self):
    trace = trace_cmd(helper + ' chdir')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_chdir_other_thread(self):
    trace = trace_cmd(helper + ' chdir_other_thread')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_chdir_fail(self):
    trace = trace_cmd(helper + ' chdir_fail')
    self.assertIn('read ' + os.getcwd() + '/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_chflags(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' chflags')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chflags_fail(self):
    trace = trace_cmd(helper + ' chflags')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chmod(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' chmod')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chmod_fail(self):
    trace = trace_cmd(helper + ' chmod')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chown(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' chown')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chown_error(self):
    trace = trace_cmd(helper + ' chown')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_dup(self):
    trace = trace_cmd(helper + ' dup')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_dup2(self):
    trace = trace_cmd(helper + ' dup2')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_faccessat(self):
    trace = trace_cmd(helper + ' faccessat')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_fchdir(self):
    trace = trace_cmd(helper + ' fchdir')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_fchflags(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fchflags')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fchflags_fail(self):
    trace = trace_cmd(helper + ' chflags')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fchmod(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fchmod')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fchmodat(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' fchmodat')
    self.assertIn('write ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fchmodat_error(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' fchmodat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fchown(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fchown')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fchownat(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' fchownat')
    self.assertIn('write ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fchownat_error(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' fchownat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fork_inherit_fd(self):
    trace = trace_cmd(helper + ' fork_inherit_fd')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_futimes(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' futimes')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lchown(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' lchown')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lchown_error(self):
    trace = trace_cmd(helper + ' lchown')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

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
  def test_mkdir(self):
    trace = trace_cmd(helper + ' mkdir')
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_mkdir_error(self):
    write_file('output', '')
    trace = trace_cmd(helper + ' mkdir')
    self.assertNotIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_mkdirat(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' mkdirat')
    self.assertIn('create ' + os.getcwd() + '/dir/output', trace)

  @with_testdir()
  def test_mkdirat_error(self):
    os.mkdir('dir')
    write_file('dir/output', '')
    trace = trace_cmd(helper + ' mkdirat')
    self.assertNotIn('create ' + os.getcwd() + '/dir/output', trace)

  @with_testdir()
  def test_mkfifo(self):
    trace = trace_cmd(helper + ' mkfifo')
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_pthread_chdir(self):
    trace = trace_cmd(helper + ' pthread_chdir')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_pthread_chdir_other_thread(self):
    trace = trace_cmd(helper + ' pthread_chdir_other_thread')
    self.assertIn('read ' + os.getcwd() + '/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_pthread_chdir_fail(self):
    trace = trace_cmd(helper + ' pthread_chdir_fail')
    self.assertIn('read ' + os.getcwd() + '/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_pthread_fchdir(self):
    trace = trace_cmd(helper + ' pthread_fchdir')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_pthread_fchdir_other_thread(self):
    trace = trace_cmd(helper + ' pthread_fchdir_other_thread')
    self.assertIn('read ' + os.getcwd() + '/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_mkfifo_error(self):
    write_file('output', '')
    trace = trace_cmd(helper + ' mkfifo')
    self.assertNotIn('create ' + os.getcwd() + '/output', trace)

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
  def test_rename(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' rename')
    self.assertIn('delete ' + os.getcwd() + '/input', trace)
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_rename_error(self):
    trace = trace_cmd(helper + ' rename')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    # Because input doesn't exist, the syscall fails before output is touched

  @with_testdir()
  def test_rename_error2(self):
    write_file('input', '')
    os.mkdir('output')
    trace = trace_cmd(helper + ' rename')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertIn('read ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_renameat(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    write_file('dir1/input', '')
    trace = trace_cmd(helper + ' renameat')
    self.assertIn('delete ' + os.getcwd() + '/dir1/input', trace)
    self.assertIn('create ' + os.getcwd() + '/dir2/output', trace)

  @with_testdir()
  def test_renameat_error(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    trace = trace_cmd(helper + ' renameat')
    self.assertIn('read ' + os.getcwd() + '/dir1/input', trace)
    # Because input doesn't exist, the syscall fails before output is touched

  @with_testdir()
  def test_renameat_error2(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    write_file('dir1/input', '')
    os.mkdir('dir2/output')
    trace = trace_cmd(helper + ' renameat')
    self.assertIn('read ' + os.getcwd() + '/dir1/input', trace)
    self.assertIn('read ' + os.getcwd() + '/dir2/output', trace)

  @with_testdir()
  def test_symlink(self):
    trace = trace_cmd(helper + ' symlink')
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_symlink_error(self):
    write_file('output', '')
    trace = trace_cmd(helper + ' symlink')
    self.assertNotIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_symlinkat(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' symlinkat')
    self.assertIn('create ' + os.getcwd() + '/dir/output', trace)

  @with_testdir()
  def test_symlinkat_error(self):
    os.mkdir('dir')
    write_file('dir/output', '')
    trace = trace_cmd(helper + ' symlinkat')
    self.assertNotIn('create ' + os.getcwd() + '/dir/output', trace)

  @with_testdir()
  def test_truncate(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' truncate')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_truncate_error(self):
    trace = trace_cmd(helper + ' truncate')
    self.assertNotIn('create ' + os.getcwd() + '/input', trace)

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

  @with_testdir()
  def test_utimes(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' utimes')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_utimes_error(self):
    trace = trace_cmd(helper + ' utimes')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_vfork_inherit_fd(self):
    trace = trace_cmd(helper + ' vfork_inherit_fd')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

if __name__ == '__main__':
    unittest.main()
