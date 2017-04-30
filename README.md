# Shuriken

Shuriken is a mostly Ninja compatible build system with a focus on correctness
over extreme smallness.

Shuriken currently only works on Mac OS X.

## Build instructions

```
mkdir build && cd build
cmake -GNinja ..
ninja all setuid
# shk and shk-trace binaries are now in bin/
```
