# httpapi

A DuckDB extension that exposes scalar functions for making HTTP requests
directly from SQL. Built on top of
[`cpp-httplib`](https://github.com/yhirose/cpp-httplib) with OpenSSL for HTTPS
support, `httpapi` lets you call REST endpoints without leaving DuckDB.

```sql
LOAD httpapi;

SELECT (http_get('https://api.github.com/zen')).body AS zen;
```

## Features

- HTTP and HTTPS (TLS via OpenSSL)
- Scalar functions for the common verbs: `GET`, `POST`, `PUT`, `PATCH`,
  `DELETE`, `HEAD`
- Vectorised — call any function over a column of URLs
- Uniform `STRUCT(status INTEGER, body VARCHAR, error VARCHAR)` return type
  so transport errors and HTTP responses are easy to handle in SQL
- Automatic redirect following
- 30 second connect / read / write timeout

## Installation

This extension is not (yet) part of the DuckDB core or community-extensions
repository. Until then, build from source or load a binary built by the
[CI pipeline](.github/workflows/MainDistributionPipeline.yml):

```sql
-- Start DuckDB with: duckdb -unsigned
LOAD '/path/to/httpapi.duckdb_extension';
```

If you publish to a custom repository, install with:

```sql
SET custom_extension_repository = 'your-bucket.example.com/httpapi/latest';
INSTALL httpapi;
LOAD httpapi;
```

## Function reference

Every function returns a `STRUCT` with three fields:

| Field    | Type      | Description                                                         |
| -------- | --------- | ------------------------------------------------------------------- |
| `status` | `INTEGER` | HTTP status code. `0` when the request never completed.             |
| `body`   | `VARCHAR` | Response body. Empty string when the request failed.                |
| `error`  | `VARCHAR` | Transport-level error message, or `NULL` if the request succeeded.  |

A request "succeeded" here means a response was received from the server. A
4xx or 5xx response is still considered successful at the transport level —
`error` is `NULL` and `status` carries the HTTP code.

If the URL argument is `NULL`, the result is `NULL`.

### Functions without a body

```sql
http_get(url VARCHAR)    -> STRUCT(status INTEGER, body VARCHAR, error VARCHAR)
http_delete(url VARCHAR) -> STRUCT(status INTEGER, body VARCHAR, error VARCHAR)
http_head(url VARCHAR)   -> STRUCT(status INTEGER, body VARCHAR, error VARCHAR)
```

### Functions with a body

The body is sent with `Content-Type: application/json`. A `NULL` body is
treated as an empty body.

```sql
http_post(url VARCHAR,  body VARCHAR) -> STRUCT(...)
http_put(url VARCHAR,   body VARCHAR) -> STRUCT(...)
http_patch(url VARCHAR, body VARCHAR) -> STRUCT(...)
```

## Examples

### Single request

```sql
SELECT (http_get('https://duckdb.org')).status;
-- 200
```

### Unpack the response

```sql
SELECT
    r.status,
    r.error,
    json_extract_string(r.body, '$.title') AS title
FROM (
    SELECT http_get('https://jsonplaceholder.typicode.com/posts/1') AS r
) t;
```

### Vectorised over a column

```sql
WITH urls(url) AS (
    VALUES ('https://duckdb.org'),
           ('https://example.com'),
           ('not-a-url')
)
SELECT
    url,
    (http_get(url)).status AS status,
    (http_get(url)).error  AS error
FROM urls;
```

### POST with a JSON payload

```sql
SELECT (http_post(
    'https://httpbin.org/post',
    '{"hello": "duckdb"}'
)).body;
```

### Error handling

Invalid URLs or unreachable hosts surface in the `error` field instead of
raising:

```sql
SELECT (http_get('not-a-url')).error;
-- invalid url: not-a-url
```

## Behaviour and limitations

- Only `http://` and `https://` URLs are accepted; ports must be valid
  integers in `[0, 65535]`.
- Custom request headers and query-string helpers are not yet exposed —
  encode query parameters into the URL yourself.
- Redirects are followed automatically.
- Connect, read and write timeouts are fixed at 30 seconds.
- `Content-Type` for body-bearing requests is always `application/json`.
- Response headers are not returned; only `status`, `body` and any transport
  `error` are exposed.
- Each function call opens its own connection — there is no connection
  pooling. Be considerate when fanning out across large tables.

## Building from source

The extension uses DuckDB's standard extension build system, with `cpp-httplib`
vendored as a single header under `third_party/httplib` and OpenSSL pulled in
via `vcpkg`.

```sh
git clone --recurse-submodules https://github.com/diegoglozano/duckdb-httpapi.git
cd duckdb-httpapi
make            # release build (use GEN=ninja for faster rebuilds)
```

The resulting artifacts:

```text
./build/release/duckdb                                       # shell with extension preloaded
./build/release/test/unittest                                # test runner
./build/release/extension/httpapi/httpapi.duckdb_extension   # loadable binary
```

For more on the build system and CI workflow, see
[docs/README.md](docs/README.md) and
[docs/UPDATING.md](docs/UPDATING.md).

## Testing

SQL-logic tests live under `test/sql`. Run them with:

```sh
make test
```

## License

MIT — see [LICENSE](LICENSE).
