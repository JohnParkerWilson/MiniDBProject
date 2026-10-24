# localdb

Initial foundation for a local C++ database system.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Initial design

- C++17 and CMake.
- `DatabaseError` carries a stable error code and a readable message.
- `Schema` describes typed columns: `Int32`, `Int64`, `Boolean`, `String32`, and `Varchar`.
- `Tuple` stores values and serializes them using the supplied schema.
- `DiskManager` reads and writes fixed-size 4096-byte pages in a local binary file.

The tuple format is deliberately schema-aware and currently does not support SQL `NULL` values. The null flag is reserved in the encoding so null support can be added without changing the overall field framing. `String32` is a fixed-width field occupying 32 bytes; shorter values are zero-padded and longer values are rejected.
