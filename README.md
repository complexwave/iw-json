# iw-json

A fork of [`iw`](https://wireless.wiki.kernel.org/en/users/Documentation/iw) — the standard Linux
nl80211 configuration tool — with a proper `--json` output mode, and a
`libiw.so` you can call from other languages (Lua/LuaJIT included) without
shelling out.

Upstream `iw` prints human-readable text. Nothing wrong with that for a
terminal, but it's miserable to parse from a script or a daemon that wants
structured wifi state. This fork adds machine-readable JSON output next to
the existing text mode, and a small C library entry point so you can run
`iw` commands in-process.

This project was built with heavy AI assistance (Claude).

## What's different from upstream iw

- `-j` / `--json` flag: every command that prints something now emits JSON
  instead of (or alongside) plain text.
- [`schemas/`](schemas) — JSON Schema (draft 2020-12) for each command's
  output, so you know exactly what fields to expect and can validate
  against them.
- `libiw.so` — the same command logic built as a shared library, exposing
  one function (`iw_cmd`) that runs an `iw` command line and returns its
  JSON output as a buffer. No subprocess, no argv parsing in your app.
- [`lua/iw.lua`](lua/iw.lua) — a thin LuaJIT FFI binding over `libiw.so`.

The CLI itself is unchanged otherwise: same commands, same device/phy
semantics, same netlink backend.

## Building

```sh
make            # builds ./iw (CLI) and libiw.so
make test       # runs C, Lua and JSON-schema tests
```

Needs `libnl` dev headers, same as upstream (see `Makefile` for the
`pkg-config` version probing). No live wireless hardware required to build
or to run `make test`; `make test-live` additionally exercises real devices
if present.

## CLI usage

Same as `iw`, add `-j`/`--json`:

```sh
$ iw dev wlan0 link
Connected to aa:bb:cc:dd:ee:ff (on wlan0)
        SSID: my-network
        freq: 5180
        ...

$ iw -j dev wlan0 link
[
  {
    "dev": "wlan0",
    "connected": true,
    "mld": false,
    "links": [
      {
        "bssid": "aa:bb:cc:dd:ee:ff",
        "status": "associated",
        "frequency": 5180,
        "ssid": "my-network"
      }
    ],
    "stats": {
      "signal_dbm": -47,
      "rx_bitrate": { "bitrate_mbps": 866.7, "vht_mcs": 9, "nss": 2 },
      "tx_bitrate": { "bitrate_mbps": 433.3, "vht_mcs": 8, "nss": 1 }
    }
  }
]
```

Add `-p`/`--pretty` for indented output; default JSON is compact
single-line, one array per invocation. **Every command's output is wrapped in a top-level JSON array**,
even when there's conceptually one result — keeps the shape uniform so
callers don't need a special case for "single object vs list of objects"
(`iw dev wlan0 info` → one-element array, `iw dev wlan0 station dump` →
one element per station).

Pipe into `jq` as usual:

```sh
iw -j dev wlan0 station dump | jq '.[].signal_dbm'
iw -j dev wlan0 scan | jq '.[] | {ssid, signal_dbm, frequency}'
```

## Schemas

[`schemas/`](schemas) has one JSON Schema file per command family, matching
the output structure exactly:

| file | command |
|---|---|
| `link.json` | `iw dev <if> link` |
| `station.json` | `iw dev <if> station dump` / `station get` |
| `scan.json` | `iw dev <if> scan` |
| `dev.json` | `iw dev` / `iw dev <if> info` |
| `phy.json` | `iw phy` / `iw phy <phy> info` |
| `reg.json` | `iw reg get` |
| `survey.json` | `iw dev <if> survey dump` |
| `mesh_param.json` | `iw dev <if> mesh_param` |
| `mpath.json` / `mpp.json` | mesh path / proxy path tables |
| `channels.json` | channel listings |
| `features.json` | supported feature bitmaps |
| `commands.json` | supported nl80211 command list |

Every schema's top-level type is `array` for the reason above. Use them to
validate output in CI, or just read them as documentation — they carry
`description` fields explaining units (dBm, MHz, ms vs seconds, etc.) and
which fields are conditional on PHY mode (e.g. `vht_mcs` vs `he_mcs` in a
`bitrate` object — only the fields relevant to the negotiated mode are
present).

`tests/test_json_schema.py` validates live/fixture output against these
schemas — good reference for how to consume them from Python.

## libiw.so — calling iw from your own code

`iw_cmd()` is the entire library API:

```c
int iw_cmd(const char *cmdline, char **out, size_t *out_len);
```

- `cmdline`: an `iw` command line **without** the leading `iw` and without
  `-j` (JSON mode is forced on internally) — e.g. `"dev wlan0 link"`.
- On return, `*out` is a malloc'd buffer of the JSON output (`*out_len`
  bytes); caller `free()`s it.
- Returns `0` on success, negative `errno`-style value on failure. `*out`
  may still contain error text on failure — check the return code, not
  just NULL-ness of `*out`.
- Thread-safe: calls are serialized internally with a mutex (`iw` itself
  has global state, e.g. the `iw_json` flag, that isn't reentrant).

Implementation note: stdout capture is done via `memfd_create` + fd
redirection rather than swapping the `stdout` `FILE*`, because musl
declares `stdout` as `FILE *const` — this works on both glibc and musl.

### From C

```c
#include <stdio.h>
extern int iw_cmd(const char *cmdline, char **out, size_t *out_len);

int main(void)
{
    char *out;
    size_t len;

    if (iw_cmd("dev wlan0 link", &out, &len) == 0) {
        fwrite(out, 1, len, stdout);
        free(out);
    }
    return 0;
}
```

```sh
cc -o example example.c -L. -liw
LD_LIBRARY_PATH=. ./example
```

### From Lua / LuaJIT

[`lua/iw.lua`](lua/iw.lua) wraps `iw_cmd` via FFI:

```lua
package.cpath = "./?.so;" .. package.cpath   -- so ffi.load("iw") finds libiw.so
local iw = require("iw")

local out, err = iw("dev wlan0 link")
if out then
    print(out)               -- JSON string
else
    print("error: " .. err)
end

-- format-string style, like string.format
local out = iw("dev %s station dump", "wlan0")
```

`iw.cmd(fmt, ...)` is the same thing spelled out; `iw(...)` (calling the
module directly) is sugar for it. Returns `nil, err` on failure instead of
raising — check the first return value.

See [`tests/test_iw.lua`](tests/test_iw.lua) for more examples including
error handling around missing devices.

## Repo layout

```
iw.c, dev.c, link.c, ...   command implementations (mostly unchanged from upstream)
json/                      JSON print backend (json.c/json.h)
schemas/                   JSON Schema per command family
lua/iw.lua                 LuaJIT FFI binding for libiw.so
tests/                     C, Lua and Python (schema) tests
```

## License

ISC, see [`COPYING`](COPYING). The JSON backend (`json/`) is fresh code
written from scratch, replacing the old JSON writer entirely.

## Status

This is a personal fork, not upstream `iw`. It tracks a specific need
(structured wifi state for scripting/embedded use) rather than trying to
be a drop-in superset of every upstream feature. Patches/issues welcome,
but expect it to diverge from `git.sipsolutions.net/iw.git` over time.
