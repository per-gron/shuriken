#!/bin/sh

usage ()
{
  echo "usage: $0 [path to shk-trace-nosuid]"
  exit
}

if [ "$#" -ne 1 ]
then
  usage
fi

SHKTRACE_PATH="`dirname $1`/shk-trace"
sudo /bin/rm -f "$SHKTRACE_PATH"
cp "$1" "$SHKTRACE_PATH"
sudo /usr/sbin/chown root "$SHKTRACE_PATH"
sudo /bin/chmod u+s "$SHKTRACE_PATH"
