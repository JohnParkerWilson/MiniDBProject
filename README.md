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
- `PageId` identifies a page using both its table ID and page number, while `RID` identifies a slot within a page.
- `DiskManager` reads and writes fixed-size 4096-byte pages in a local binary file.
- `Catalog` persists table IDs, table names, schemas, and the next table ID in a companion catalog file.
- `DiskManager` creates and opens tables, allocates logical pages, maps them to initialized physical pages, reads and writes pages, synchronizes, and closes the database.

The tuple format is deliberately schema-aware and currently does not support SQL `NULL` values. The null flag is reserved in the encoding so null support can be added without changing the overall field framing. `String32` is a fixed-width field occupying 32 bytes; shorter values are zero-padded and longer values are rejected.

## Catalog persistence

The catalog is stored in a separate binary file. Its format is little-endian and currently contains:

```text
8-byte magic: LDBCAT01
uint32 format version
uint32 next table ID
uint32 table count
repeated table entries:
    uint32 table ID
    uint32 table-name length + table-name bytes
    uint32 column count
    repeated columns:
        uint32 column-name length + column-name bytes
        uint8 type ID
        uint8 nullable flag
```

`Catalog::create_table()` writes the catalog immediately through a temporary file and replacement, so table IDs and schemas survive process shutdown and can be recovered by constructing `Catalog` with the same path.

The disk layer uses a companion catalog file by default. For a database path `data/localdb.db`, the catalog path is `data/localdb.db.catalog`. Each table entry also stores the physical page IDs belonging to that table. Logical page numbers are assigned from zero per table and remain stable after reopening.

The disk-layer workflow is:

```cpp
DiskManager disk("data/localdb.db");
TableId table_id = disk.create_table("users", schema);
PageId page_id = disk.allocate_page(table_id);
disk.write_page(page_id, page_bytes);
disk.sync();
disk.close();
```

`open_table()` reopens a table by its stable ID or name. `allocate_page()` zero-initializes the physical page before returning its logical `PageId`. `sync()` persists both page contents and catalog metadata; `flush()` remains as a compatibility alias.
