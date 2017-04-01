#!/usr/bin/python
# -*- coding: utf8 -*-

import os
import pipes
import re
import shutil
import subprocess
import sys
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

shkTraceFb = os.environ.get(
    'SHK_TRACE_FB_PATH',
    os.getcwd() + '/../../../build/src/shkutil/include/util')
sys.path.insert(0, shkTraceFb)
sys.path.insert(0, '../../../vendor/flatbuffers/python')
import ShkTrace.Trace
import ShkTrace.EventType

shkTrace = os.environ.get(
    'SHK_TRACE_PATH',
    os.getcwd() + '/../../../build/src/shk-trace/shk-trace')
helper = os.path.join(os.path.dirname(shkTrace), 'shktrace_integrationtest_helper')

sys.path.insert(0, '/path/to/application/app/folder')

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

def event_type_to_string(event_type):
  return {
    ShkTrace.EventType.EventType.Read: 'read',
    ShkTrace.EventType.EventType.ReadDirectory: 'read_directory',
    ShkTrace.EventType.EventType.Write: 'write',
    ShkTrace.EventType.EventType.Create: 'create',
    ShkTrace.EventType.EventType.Delete: 'delete',
    ShkTrace.EventType.EventType.Delete: 'delete',
    ShkTrace.EventType.EventType.FatalError: 'fatal_error',
  }.get(event_type)

def trace_to_string(trace):
  str = ''
  for i in range(0, trace.EventsLength()):
    event = trace.Events(i)
    str += event_type_to_string(event.Type()) + ' ' + event.Path() + '\n'
  return str

def trace_buffer_to_string(data):
  trace = ShkTrace.Trace.Trace.GetRootAsTrace(data, 0)
  return trace_to_string(trace)

def trace_cmd(cmd):
  run_cmd(shkTrace + " -f out.trace -c " + pipes.quote(cmd))
  return trace_buffer_to_string(bytearray(read_file('out.trace')))

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
  def test_read_file_in_nonexistent_dir(self):
    trace = trace_cmd("stat missing_dir/file; true")
    self.assertIn('read ' + os.getcwd() + '/missing_dir\n', trace)

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
  def test_tracing_in_parallel(self):
    run_cmd(
        shkTrace + " -f trace1.txt -c 'sleep 1 && /bin/echo hey' &" +
        shkTrace + " -f trace2.txt -c 'sleep 1 && /bin/echo hey' && wait")
    trace1 = trace_buffer_to_string(bytearray(read_file('trace1.txt')))
    trace2 = trace_buffer_to_string(bytearray(read_file('trace2.txt')))
    self.assertIn('read /bin/echo\n', trace1)
    self.assertIn('read /bin/echo\n', trace2)

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
  def test_ls_reads_directory(self):
    trace = trace_cmd("ls /usr")
    self.assertIn('read_directory /usr', trace)

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
  def test_accessx_np(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' accessx_np')
    self.assertIn('fatal_error accessx_np', trace)

  @with_testdir()
  def test_chdir(self):
    trace = trace_cmd(helper + ' chdir')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_chdir_other_thread(self):
    trace = trace_cmd(helper + ' chdir_other_thread')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_chdir_error(self):
    trace = trace_cmd(helper + ' chdir_fail')
    self.assertIn('read ' + os.getcwd() + '/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_chflags(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' chflags')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chflags_error(self):
    trace = trace_cmd(helper + ' chflags')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chmod(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' chmod')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chmod_error(self):
    trace = trace_cmd(helper + ' chmod')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chmod_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' chmod_extended')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_chmod_extended_error(self):
    trace = trace_cmd(helper + ' chmod_extended')
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
  def test_chroot(self):
    trace = trace_cmd(helper + ' chroot')
    self.assertIn('fatal_error chroot', trace)

  @with_testdir()
  def test_close(self):
    trace = trace_cmd(helper + ' close')
    self.assertNotIn('read /usr/local\n', trace)

  @with_testdir()
  def test_close_nocancel(self):
    trace = trace_cmd(helper + ' close_nocancel')
    self.assertNotIn('read /usr/local\n', trace)

  @with_testdir()
  def test_delete(self):
    trace = trace_cmd(helper + ' delete')
    self.assertIn('fatal_error delete', trace)

  @with_testdir()
  def test_dup(self):
    trace = trace_cmd(helper + ' dup')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_dup2(self):
    trace = trace_cmd(helper + ' dup2')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_exchangedata(self):
    write_file('input', '')
    write_file('output', '')
    trace = trace_cmd(helper + ' exchangedata')
    self.assertIn('write ' + os.getcwd() + '/input', trace)
    self.assertIn('write ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_execve(self):
    trace = trace_cmd(helper + ' execve')
    self.assertIn('read /usr/bin/true', trace)

  @with_testdir()
  def test_exchangedata_error(self):
    write_file('output', '')
    trace = trace_cmd(helper + ' exchangedata')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    # Because input doesn't exist, the syscall fails before output is touched

  @with_testdir()
  def test_exchangedata_error2(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' exchangedata')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertIn('read ' + os.getcwd() + '/output', trace)

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
  def test_fchmod_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fchmod_extended')
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
  def test_fcntl_disable_cloexec(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' fcntl_disable_cloexec')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fcntl_dupfd(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' fcntl_dupfd')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fcntl_dupfd_cloexec(self):
    trace = trace_cmd(helper + ' fcntl_dupfd_cloexec')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_fcntl_dupfd_cloexec_exec(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' fcntl_dupfd_cloexec_exec')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)
    self.assertNotIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fcntl_enable_cloexec(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' fcntl_enable_cloexec')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)
    self.assertNotIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fcntl_nocancel_dupfd(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' fcntl_nocancel_dupfd')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fgetattrlist(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fgetattrlist')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fgetxattr(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fgetxattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fhopen(self):
    trace = trace_cmd(helper + ' fhopen')
    self.assertIn('fatal_error fhopen', trace)

  @with_testdir()
  def test_flistxattr(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' flistxattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_flock(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' flock')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fork_inherit_fd(self):
    trace = trace_cmd(helper + ' fork_inherit_fd')
    self.assertIn('read /usr/nonexisting_path_just_for_testing', trace)

  @with_testdir()
  def test_fpathconf(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fpathconf')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fremovexattr(self):
    write_file('input', '')
    run_cmd(helper + ' setxattr')  # set an xattr so that there is one to remove
    trace = trace_cmd(helper + ' fremovexattr')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fsetattrlist(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fsetattrlist')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fsetxattr(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fsetxattr')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fsgetpath(self):
    # fsgetpath is a private syscall. GetFileInfo happens to call it.
    root_stat = os.stat('/')
    trace = trace_cmd('GetFileInfo -P /.vol/' + str(root_stat.st_dev) + '/' + str(root_stat.st_ino) + '; true')
    self.assertIn('fatal_error fsgetpath', trace)

  @with_testdir()
  def test_fstat(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fstat')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fstat_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fstat_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fstat64(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fstat64')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fstat64_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' fstat64_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_fstatat(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' fstatat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_fstatat_error(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' fstatat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_futimes(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' futimes')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_getattrlist(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' getattrlist')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_getattrlist_error(self):
    trace = trace_cmd(helper + ' getattrlist')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_getattrlistat(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' getattrlistat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_getattrlistat_error(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' getattrlistat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_getattrlistbulk(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' getattrlistbulk')
    self.assertIn('read_directory ' + os.getcwd() + '/dir', trace)

  @with_testdir()
  def test_getdirentries(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' getdirentries')
    self.assertIn('read_directory ' + os.getcwd() + '/dir', trace)

  @with_testdir()
  def test_getdirentriesattr(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' getdirentriesattr')
    self.assertIn('read_directory ' + os.getcwd() + '/dir', trace)

  @with_testdir()
  def test_getxattr(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' getxattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_getxattr_error(self):
    trace = trace_cmd(helper + ' getxattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_guarded_close_np(self):
    trace = trace_cmd(helper + ' guarded_close_np')
    self.assertNotIn('read /usr/local\n', trace)

  @with_testdir()
  def test_guarded_open_dprotected_np(self):
    trace = trace_cmd(helper + ' guarded_open_dprotected_np')
    self.assertIn('create ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_guarded_open_dprotected_np_error(self):
    os.mkdir('input')
    trace = trace_cmd(helper + ' guarded_open_dprotected_np')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_guarded_open_np(self):
    trace = trace_cmd(helper + ' guarded_open_np')
    self.assertIn('create ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_guarded_open_np_error(self):
    os.mkdir('input')
    trace = trace_cmd(helper + ' guarded_open_np')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

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
  def test_listxattr(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' listxattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_listxattr_error(self):
    trace = trace_cmd(helper + ' listxattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' lstat')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat_error(self):
    trace = trace_cmd(helper + ' lstat')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' lstat_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat_extended_error(self):
    trace = trace_cmd(helper + ' lstat_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat64(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' lstat64')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat64_error(self):
    trace = trace_cmd(helper + ' lstat64')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat64_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' lstat64_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_lstat64_extended_error(self):
    trace = trace_cmd(helper + ' lstat64_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

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
  def test_mkdir_extended(self):
    trace = trace_cmd(helper + ' mkdir_extended')
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_mkdir_extended_error(self):
    write_file('output', '')
    trace = trace_cmd(helper + ' mkdir_extended')
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
  def test_mkfifo_error(self):
    write_file('output', '')
    trace = trace_cmd(helper + ' mkfifo')
    self.assertNotIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_mkfifo_extended(self):
    trace = trace_cmd(helper + ' mkfifo_extended')
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_mkfifo_extended_error(self):
    write_file('output', '')
    trace = trace_cmd(helper + ' mkfifo_extended')
    self.assertNotIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_mknod(self):
    trace = trace_cmd(helper + ' mknod')
    self.assertIn('fatal_error mknod', trace)

  @with_testdir()
  def test_open_cloexec(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' open_cloexec')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)
    self.assertNotIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_open_cloexec_off(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' open_cloexec_off')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_open_create(self):
    trace = trace_cmd(helper + ' open_create')
    self.assertIn('create ' + os.getcwd() + '/input', trace)
    self.assertEqual(read_file('input'), 'yo')

  @with_testdir()
  def test_open_create_and_read(self):
    trace = trace_cmd(helper + ' open_create_and_read')
    self.assertIn('create ' + os.getcwd() + '/input', trace)
    self.assertNotIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_create_excl(self):
    trace = trace_cmd(helper + ' open_create_excl')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertIn('create ' + os.getcwd() + '/input', trace)
    self.assertEqual(read_file('input'), 'ye')

  @with_testdir()
  def test_open_create_excl_append(self):
    trace = trace_cmd(helper + ' open_create_excl_append')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertIn('write ' + os.getcwd() + '/input', trace)
    self.assertEqual(read_file('input'), 'ye')

  @with_testdir()
  def test_open_dprotected_np(self):
    trace = trace_cmd(helper + ' open_dprotected_np')
    self.assertIn('create ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_dprotected_np_error(self):
    os.mkdir('input')
    trace = trace_cmd(helper + ' open_dprotected_np')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' open_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_extended_error(self):
    trace = trace_cmd(helper + ' open_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_implicit_read(self):
    write_file('input', 'hi')
    trace = trace_cmd(helper + ' open_implicit_read')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_nocancel(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' open_nocancel')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_nocancel_error(self):
    trace = trace_cmd(helper + ' open_nocancel')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_open_partial_overwrite(self):
    write_file('input', '__!')
    trace = trace_cmd(helper + ' open_partial_overwrite')
    self.assertIn('write ' + os.getcwd() + '/input', trace)
    self.assertEqual(read_file('input'), 'hi!')

  @with_testdir()
  def test_open_read(self):
    trace = trace_cmd(helper + ' open_read')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_openat(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' openat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_openat_error(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' openat')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_openat_nocancel(self):
    os.mkdir('dir')
    write_file('dir/input', '')
    trace = trace_cmd(helper + ' openat_nocancel')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_openat_nocancel_error(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' openat_nocancel')
    self.assertIn('read ' + os.getcwd() + '/dir/input', trace)

  @with_testdir()
  def test_openat_with_openat_fd(self):
    trace = trace_cmd(helper + ' openat_with_openat_fd')
    self.assertIn('read /usr/shk_for_testing_only', trace)

  @with_testdir()
  def test_openbyid_np(self):
    trace = trace_cmd(helper + ' openbyid_np')
    self.assertIn('fatal_error openbyid_np', trace)

  @with_testdir()
  def test_opendir(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' opendir')
    self.assertIn('read_directory ' + os.getcwd() + '/dir', trace)

  @with_testdir()
  def test_pathconf(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' pathconf')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_pathconf_error(self):
    trace = trace_cmd(helper + ' pathconf')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_posix_spawn(self):
    trace = trace_cmd(helper + ' posix_spawn')
    self.assertIn('read /usr/bin/true', trace)

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
  def test_removexattr(self):
    write_file('input', '')
    run_cmd(helper + ' setxattr')  # set an xattr so that there is one to remove
    trace = trace_cmd(helper + ' removexattr')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_removexattr_error(self):
    trace = trace_cmd(helper + ' removexattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

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
  def test_rename_ext(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' rename_ext')
    self.assertIn('delete ' + os.getcwd() + '/input', trace)
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_rename_ext_error(self):
    trace = trace_cmd(helper + ' rename_ext')
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
  def test_renamex_np(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' renamex_np')
    self.assertIn('delete ' + os.getcwd() + '/input', trace)
    self.assertIn('create ' + os.getcwd() + '/output', trace)

  @with_testdir()
  def test_renamex_np_error(self):
    trace = trace_cmd(helper + ' renamex_np')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    # Because input doesn't exist, the syscall fails before output is touched

  @with_testdir()
  def test_renameatx_np(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    write_file('dir1/input', '')
    trace = trace_cmd(helper + ' renameatx_np')
    self.assertIn('delete ' + os.getcwd() + '/dir1/input', trace)
    self.assertIn('create ' + os.getcwd() + '/dir2/output', trace)

  @with_testdir()
  def test_renameatx_np_error(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    trace = trace_cmd(helper + ' renameatx_np')
    self.assertIn('read ' + os.getcwd() + '/dir1/input', trace)
    # Because input doesn't exist, the syscall fails before output is touched

  @with_testdir()
  def test_renameatx_np_error2(self):
    os.mkdir('dir1')
    os.mkdir('dir2')
    write_file('dir1/input', '')
    os.mkdir('dir2/output')
    trace = trace_cmd(helper + ' renameatx_np')
    self.assertIn('read ' + os.getcwd() + '/dir1/input', trace)
    self.assertIn('read ' + os.getcwd() + '/dir2/output', trace)

  @with_testdir()
  def test_rmdir(self):
    os.mkdir('dir')
    trace = trace_cmd(helper + ' rmdir')
    self.assertIn('delete ' + os.getcwd() + '/dir', trace)

  @with_testdir()
  def test_rmdir_error(self):
    trace = trace_cmd(helper + ' rmdir')
    self.assertIn('read ' + os.getcwd() + '/dir', trace)

  @with_testdir()
  def test_searchfs(self):
    trace = trace_cmd(helper + ' searchfs')
    self.assertIn('fatal_error searchfs', trace)

  @with_testdir()
  def test_setattrlist(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' setattrlist')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_setattrlist_error(self):
    trace = trace_cmd(helper + ' setattrlist')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_setxattr(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' setxattr')
    self.assertIn('write ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_setxattr_error(self):
    trace = trace_cmd(helper + ' setxattr')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' stat')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat_error(self):
    trace = trace_cmd(helper + ' stat')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' stat_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat_extended_error(self):
    trace = trace_cmd(helper + ' stat_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat64(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' stat64')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat64_error(self):
    trace = trace_cmd(helper + ' stat64')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat64_extended(self):
    write_file('input', '')
    trace = trace_cmd(helper + ' stat64_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

  @with_testdir()
  def test_stat64_extended_error(self):
    trace = trace_cmd(helper + ' stat64_extended')
    self.assertIn('read ' + os.getcwd() + '/input', trace)

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
  def test_undelete(self):
    trace = trace_cmd(helper + ' undelete')
    self.assertIn('fatal_error undelete', trace)

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


  # Symlink behavior tests

  @with_testdir()
  def test_symlink_chown(self):
    write_file('target', '')
    os.symlink('target', 'input')
    trace = trace_cmd(helper + ' chown')
    # input really should be among the read files here, but it's not because
    # OS X's kdebug simply does not provide this information :-(
    # self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertIn('write ' + os.getcwd() + '/target', trace)

  @with_testdir()
  def test_symlink_lchown(self):
    write_file('target', '')
    os.symlink('target', 'input')
    trace = trace_cmd(helper + ' lchown')
    self.assertIn('write ' + os.getcwd() + '/input', trace)
    self.assertNotIn(os.getcwd() + '/target', trace)

  @with_testdir()
  def test_symlink_lstat(self):
    write_file('target', '')
    os.symlink('target', 'input')
    trace = trace_cmd(helper + ' lstat')
    self.assertIn('read ' + os.getcwd() + '/input', trace)
    self.assertNotIn(os.getcwd() + '/target', trace)

if __name__ == '__main__':
    unittest.main()
