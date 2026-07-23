#!/usr/bin/env bash
# Smoke test for ADBC driver-manifest discovery (issue #50), run by
# test-with-snowflake.yml and runnable locally on macOS.
#
# Proves the driver resolves through a snowflake.toml manifest ALONE: the
# duckdb binary is copied to a directory with no driver beside it (the
# extension-directory probe resolves to the binary's dir for the statically
# linked shell) and no SNOWFLAKE_ADBC_DRIVER_PATH is set. A negative control
# runs first: with no manifest either, resolution must FAIL - otherwise the
# test bed is leaking a driver source and the positive result means nothing.
# Every other CI job finds the driver by adjacency, so without this leg a
# manifest-discovery regression would ship silently.
set -euo pipefail

DUCKDB_BIN="${DUCKDB_BIN:-build/release/duckdb}"
DRIVER="${DRIVER:-$PWD/adbc_drivers/libadbc_driver_snowflake.so}"

for v in SNOWFLAKE_ACCOUNT SNOWFLAKE_KEYPAIR_USER SNOWFLAKE_PRIVATE_KEY_FILE SNOWFLAKE_DATABASE; do
    if [ -z "${!v:-}" ]; then
        echo "$v not set - skipping manifest smoke test."
        exit 0
    fi
done
[ -f "$DRIVER" ] || { echo "FAIL: driver not found at $DRIVER"; exit 1; }

MDIR="$HOME/Library/Application Support/ADBC/Drivers"
MANIFEST="$MDIR/snowflake.toml"
if [ -f "$MANIFEST" ]; then
    echo "FAIL: pre-existing manifest at $MANIFEST - refusing to clobber it"
    exit 1
fi

WORK=$(mktemp -d)
CREATED_MANIFEST=0
cleanup() {
    [ "$CREATED_MANIFEST" = 1 ] && rm -f "$MANIFEST"
    rm -rf "$WORK"
}
trap cleanup EXIT

cp "$DUCKDB_BIN" "$WORK/duckdb"
# No escape hatches: resolution may only come from the manifest under test.
unset SNOWFLAKE_ADBC_DRIVER_PATH CONDA_PREFIX ADBC_DRIVER_PATH

SQL="CREATE SECRET sf_smoke (TYPE snowflake, ACCOUNT '$SNOWFLAKE_ACCOUNT',
    USER '$SNOWFLAKE_KEYPAIR_USER', AUTH_TYPE 'key_pair',
    PRIVATE_KEY_FILE '$SNOWFLAKE_PRIVATE_KEY_FILE',
    PRIVATE_KEY_PASSWORD '${SNOWFLAKE_PRIVATE_KEY_PASSWORD_FILE:-}',
    DATABASE '$SNOWFLAKE_DATABASE', WAREHOUSE 'COMPUTE_WH');
SELECT * FROM snowflake_query('SELECT 42 AS answer', 'sf_smoke');"

echo "== negative control: no manifest -> driver must NOT be found"
if OUT=$("$WORK/duckdb" -unsigned -noheader -list -c "$SQL" 2>&1); then
    echo "FAIL: query succeeded with no driver source at all"
    echo "$OUT" | tail -5
    exit 1
fi
if ! echo "$OUT" | grep -q "not found"; then
    echo "FAIL: expected a driver-not-found error, got:"
    echo "$OUT" | tail -5
    exit 1
fi
echo "ok: driver correctly not found without a manifest"

echo "== manifest present -> driver must resolve through it and answer a query"
mkdir -p "$MDIR"
printf "[Driver]\n[Driver.shared]\nmacos_arm64 = '%s'\nmacos_amd64 = '%s'\n" \
    "$DRIVER" "$DRIVER" > "$MANIFEST"
CREATED_MANIFEST=1
if ! OUT=$("$WORK/duckdb" -unsigned -noheader -list -c "$SQL" 2>&1); then
    echo "FAIL: query failed with manifest present:"
    echo "$OUT" | tail -5
    exit 1
fi
if ! echo "$OUT" | grep -q '^42$'; then
    echo "FAIL: expected query answer 42, got:"
    echo "$OUT" | tail -5
    exit 1
fi
echo "ok: query answered through the manifest-resolved driver"
