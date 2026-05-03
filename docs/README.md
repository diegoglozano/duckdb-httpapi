# Developer guide

This document is for people **building, testing or releasing** the `httpapi`
extension. For end-user documentation (functions, examples), see the
[top-level README](../README.md).

The repository is based on the
[DuckDB extension template](https://github.com/duckdb/extension-template), so
much of the workflow below mirrors any other DuckDB out-of-tree extension.

## Cloning

The repo uses two submodules — `duckdb` itself and `extension-ci-tools` — so
clone with `--recurse-submodules`:

```sh
git clone --recurse-submodules https://github.com/diegoglozano/duckdb-httpapi.git
cd duckdb-httpapi
```

If you forgot:

```sh
git submodule update --init --recursive
```

| Submodule          | Repository                                        | Purpose                                                                  |
| ------------------ | ------------------------------------------------- | ------------------------------------------------------------------------ |
| `duckdb`           | https://github.com/duckdb/duckdb                  | Core DuckDB sources required to build the extension.                     |
| `extension-ci-tools` | https://github.com/duckdb/extension-ci-tools    | Reusable build / test / release infrastructure for DuckDB extensions.    |

It's a good idea to bump these at least once per major DuckDB release so the
CI pipeline keeps working — see [UPDATING.md](UPDATING.md) for the procedure.

## Dependencies

The extension links against OpenSSL (for HTTPS support in `cpp-httplib`).
OpenSSL is pulled in via [vcpkg](https://vcpkg.io/en/getting-started); the
`vcpkg.json` manifest at the repo root declares it.

To enable vcpkg, install it once and export `VCPKG_TOOLCHAIN_PATH`:

```shell
cd <a-dir-outside-this-repo>
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg && git checkout ce613c41372b23b1f51333815feb3edd87ef8a8b
sh ./scripts/bootstrap.sh -disableMetrics
export VCPKG_TOOLCHAIN_PATH=`pwd`/scripts/buildsystems/vcpkg.cmake
```

`cpp-httplib` itself is vendored as a single header under
`third_party/httplib`, so the build configures successfully even without
vcpkg (e.g. in the clang-tidy CI job, where only OpenSSL needs to be
satisfied by the system).

## Building

```sh
make             # release build
make debug       # debug build
GEN=ninja make   # faster rebuilds, recommended with ccache installed
```

Artifacts:

```text
./build/release/duckdb                                       # shell with the extension preloaded
./build/release/test/unittest                                # DuckDB test runner with the extension linked in
./build/release/extension/httpapi/httpapi.duckdb_extension   # loadable binary as it would be distributed
```

`ccache` + `ninja` is the recommended local setup — DuckDB itself is a large
dependency and you only want to compile it once.

## Running

Start the bundled shell — the extension is already loaded:

```sh
./build/release/duckdb
```

```sql
D SELECT (http_get('https://duckdb.org')).status;
```

## Testing

Tests are written as
[SQL logic tests](https://duckdb.org/dev/sqllogictest/intro.html) and live in
`test/sql/`. Run them with:

```sh
make test
make test_debug
```

Tests that hit the network are kept off the main suite; the in-tree tests
exercise URL parsing, the response struct shape and `NULL` handling so they
can run in environments without internet access.

## Code quality

The CI workflow runs `clang-format` and `clang-tidy` against the extension
sources via `extension-ci-tools`'s
`_extension_code_quality.yml` reusable workflow. To match locally, configure
your editor with the `.clang-format` and `.clang-tidy` files at the repo root
(both are symlinks into the `duckdb` submodule).

The vendored `third_party/httplib` directory is marked as a `SYSTEM` include
in `CMakeLists.txt` so clang-tidy doesn't lint it.

## Distribution

### Community extensions

The recommended distribution channel for DuckDB extensions is the
[community extensions repository](https://github.com/duckdb/community-extensions).
Submitting `httpapi` there is a matter of opening a PR with a descriptor file;
the rest happens in their CI. Once published:

```sql
INSTALL httpapi FROM community;
LOAD httpapi;
```

### GitHub Actions artifacts

`.github/workflows/MainDistributionPipeline.yml` calls the reusable build
pipeline from `extension-ci-tools` and uploads the binary for every push.
Download the artifact and load it directly:

```sql
LOAD '/path/to/httpapi.duckdb_extension';
```

This requires starting DuckDB with unsigned extensions allowed:

```shell
duckdb -unsigned
```

### Custom repository

For a fully custom hosting setup (e.g. an S3 bucket), point DuckDB at it and
install:

```sql
SET custom_extension_repository = 'bucket.s3.eu-west-1.amazonaws.com/httpapi/latest';
INSTALL httpapi;
LOAD httpapi;
```

`scripts/extension-upload.sh` is a starting point for uploading built
extensions to such a bucket.

## CLion setup

### Opening the project

Open `./duckdb/CMakeLists.txt` (the submodule, not the top-level one) as the
CLion project. Then go to
`Tools → CMake → Change Project Root`
([docs](https://www.jetbrains.com/help/clion/change-project-root-directory.html))
and set the project root back to the root of this repo.

### Debugging

In `CLion → Settings → Build, Execution, Deploy → CMake`, add the build
profiles you need. The simplest setup is to leave everything at default
except:

- **Build path**: `../build/{build type}`
- **CMake options**:
  ```
  -DDUCKDB_EXTENSION_CONFIGS=<absolute_path_to>/extension_config.cmake
  ```

Then `Run → Edit Configurations → + → CMake Application` with target and
executable both `unittest`. To run only the extension's SQL tests, set the
program arguments to:

```
--test-dir ../../.. [sql]
```

The `unittest` binary is more reliable as a CLion run target than the DuckDB
CLI itself.
