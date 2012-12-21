effiziente_programme
====================

Instructions:

`make <filebasename>` compiles the <filebasename>.c file. After that you can use one of the following targets:

- `make prof`: Does function-level profiling on the file and stores the output in `prof.out`.
- `make coverage`: Does coverage profiling and stores the output in several `*.gcov` files.
- `make perf`: Collects several performance statistics.
- `make test`: Tests the program for correctness by comparing its output to a reference value.
- `make all`: All of the above.
- `make clean`: Deletes any auto-generated files.
