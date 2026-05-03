# Testing this extension

This directory contains all the tests for the `httpapi` extension. The `sql`
subdirectory holds tests written as
[SQLLogicTests](https://duckdb.org/dev/sqllogictest/intro.html), which is
DuckDB's preferred test format for both core and out-of-tree extensions.

The root `Makefile` builds and runs the suite:

```bash
make test          # release build + tests
make test_debug    # debug build + tests
```

Tests that depend on real network access are intentionally avoided so the
suite runs reliably in CI sandboxes. The in-tree tests cover URL parsing,
the response `STRUCT` shape and `NULL` propagation.
