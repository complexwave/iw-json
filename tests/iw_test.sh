#!/bin/sh
#
# iw_test.sh <dev> — exercise the -j/JSON output against a live device.
#
# Meant to be run on a real AP (or any associated interface) where
# `station dump` actually returns peers — the offline test suite can't
# cover the populated station path.
#
# First prints the pretty JSON of the main query commands for visual
# inspection, then validates each command's JSON: with python3+jsonschema
# when the matching schema exists, otherwise a jq/python well-formedness
# check.
#
#   tests/iw_test.sh wlan0
#
set -u

DEV="${1:-}"
if [ -z "$DEV" ]; then
	echo "usage: $0 <dev>" >&2
	exit 2
fi

# Resolve paths relative to this script so it runs from anywhere.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
IW="$ROOT/iw"
SCHEMAS="$ROOT/schemas"

if [ ! -x "$IW" ]; then
	echo "error: $IW not built — run 'make' first" >&2
	exit 2
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

pass=0
fail=0

# validate <schema-name>  (reads JSON from $TMP; schema "" = well-formed only)
validate() {
	schema="$1"

	if [ -n "$schema" ] && [ -f "$SCHEMAS/$schema.json" ] && \
	   python3 -c 'import jsonschema' 2>/dev/null; then
		python3 - "$SCHEMAS/$schema.json" "$TMP" <<'PY'
import json, sys
import jsonschema
schema = json.load(open(sys.argv[1]))
data = json.load(open(sys.argv[2]))
jsonschema.validate(data, schema)
PY
		return $?
	fi

	# no schema / no jsonschema: just check it parses
	if command -v jq >/dev/null 2>&1; then
		jq -e . "$TMP" >/dev/null 2>&1
	else
		python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$TMP"
	fi
}

# run <label> <schema> <iw-args...>
run() {
	label="$1"; schema="$2"; shift 2
	"$IW" -j "$@" >"$TMP" 2>/dev/null
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "  SKIP  $label (iw rc=$rc — command unsupported on this dev)"
		return
	fi
	if validate "$schema"; then
		echo "  PASS  $label"
		pass=$((pass + 1))
	else
		echo "  FAIL  $label"
		echo "------ output ------"
		head -40 "$TMP"
		echo "--------------------"
		fail=$((fail + 1))
	fi
}

# --- visual inspection: pretty-print the main query commands ---------------
echo "############################################################"
echo "# iw JSON output for dev=$DEV (visual inspection)"
echo "############################################################"
for desc in "dev" "dev $DEV info" "dev $DEV link" "dev $DEV station dump" "dev $DEV survey dump" "dev $DEV scan dump"; do
	echo
	echo "### iw -j -p $desc"
	# shellcheck disable=SC2086
	"$IW" -j -p $desc 2>&1
done
echo
echo "### iw -j -p reg get"
"$IW" -j -p reg get 2>&1
echo
echo "### iw -j -p phy (first 60 lines)"
"$IW" -j -p phy 2>&1 | head -60

# --- validation ------------------------------------------------------------
echo
echo "== iw JSON live test: dev=$DEV =="

run "dev"                 dev     dev
run "dev $DEV info"       dev     dev "$DEV" info
run "link"                link    dev "$DEV" link
run "station dump"        station dev "$DEV" station dump
run "survey dump"         survey  dev "$DEV" survey dump
run "scan dump"           scan    dev "$DEV" scan dump
run "reg get"             reg     reg get
run "phy"                 phy     phy
# 'commands' never sends netlink (handler returns 2), so it always exits
# non-zero despite producing valid output — validate it directly.
"$IW" -j commands >"$TMP" 2>/dev/null
if validate commands; then
	echo "  PASS  commands"; pass=$((pass + 1))
else
	echo "  FAIL  commands"; fail=$((fail + 1))
fi
run "features"            features features

# channels needs a phy name; derive it from the device.
PHY=$(cat "/sys/class/net/$DEV/phy80211/name" 2>/dev/null)
if [ -n "$PHY" ]; then
	run "channels"        channels phy "$PHY" channels
else
	echo "  NOTE  could not resolve phy for $DEV — 'channels' not exercised"
fi

# Mesh paths/params: SKIP automatically on non-mesh interfaces (iw rc!=0).
run "mpath dump"          mpath   dev "$DEV" mpath dump
run "mpp dump"            mpp     dev "$DEV" mpp dump
run "mesh_param dump"     mesh_param dev "$DEV" mesh_param dump

# Per-station 'station get' for every associated peer.
peers=$("$IW" -j dev "$DEV" station dump 2>/dev/null | \
	jq -r '.[].addr' 2>/dev/null)
if [ -z "$peers" ]; then
	echo "  NOTE  no associated stations — 'station get' path not exercised"
	echo "        (run this on an AP with clients to cover it)"
else
	for mac in $peers; do
		run "station get $mac" station dev "$DEV" station get "$mac"
	done
fi

echo
echo "== $pass passed, $fail failed =="
[ $fail -eq 0 ]
