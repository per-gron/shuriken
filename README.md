# Shuriken

Shuriken is a mostly Ninja compatible build system with a focus on correctness
over extreme smallness.

Shuriken currently only works on Mac OS X.

## Build instructions

```
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -GNinja ..
ninja shk shk-trace
# shk and shk-trace binaries are now in bin/
```

To test:

```
ninja all shk-trace && ctest
```
