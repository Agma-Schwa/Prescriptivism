# Prescriptivism

This is the video game implementation of the Prescriptivism card game, designed by Agma Schwa.

## Building
Currently, building this project requires the following dependencies:
- CMake version 3.28 or later
- Clang 19 or later (GCC 15 should also work, but isn’t tested)
- Ninja (`make` ***does not work***)

The goal is that all other dependencies *should* be downloaded at build time, but this 
project is still fairly new, so it is possible that we missed some.

To build the project, run the following commands:
```bash
$ cmake -S . -B out -G Ninja
$ cmake --build out  
```

If you need to tell `cmake` what compiler to use, you can do so by replacing `cmake` in
the *configure* step (the first command) with the following. You do *not* need to do this
for the *build* step (the one w/ `--build`).
```bash
CC=/path/to/clang CXX=/path/to/clang++ cmake # rest of command goes here
```

Note that, at least for Clang, the paths should be absolute paths; otherwise, you might
get weird errors about it not finding `stddef.h`.
