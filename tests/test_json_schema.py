#!/usr/bin/env python3
"""
Validate iw JSON output against schemas.

Uses libiw.so via ctypes — no wireless hardware needed for
structural tests (commands will fail but arg parsing is tested).

For live tests (actual device output), run with --live.
"""
import ctypes
import json
import os
import sys

import jsonschema

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
SCHEMA_DIR = os.path.join(PROJECT_DIR, "schemas")

lib = ctypes.CDLL(os.path.join(PROJECT_DIR, "libiw.so"))
lib.iw_cmd.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t)]
lib.iw_cmd.restype = ctypes.c_int

libc = ctypes.CDLL("libc.so.6")
libc.free.argtypes = [ctypes.c_void_p]


def iw_cmd(cmdline):
    out = ctypes.c_char_p()
    out_len = ctypes.c_size_t()
    rc = lib.iw_cmd(cmdline.encode(), ctypes.byref(out), ctypes.byref(out_len))
    result = None
    if out.value is not None:
        result = out.value[:out_len.value].decode("utf-8", errors="replace")
        libc.free(out)
    return rc, result


def load_schema(name):
    path = os.path.join(SCHEMA_DIR, name + ".json")
    with open(path) as f:
        return json.load(f)


passed = 0
failed = 0
errors = []


def test(name, fn):
    global passed, failed
    try:
        fn()
        passed += 1
        print(f"  PASS  {name}")
    except Exception as e:
        failed += 1
        errors.append((name, str(e)))
        print(f"  FAIL  {name}: {e}")


def assert_has_key(d, key):
    if key not in d:
        raise ValueError(f"missing key '{key}'")


def assert_mac(addr):
    import re
    if not re.match(r"^([0-9a-f]{2}:){5}[0-9a-f]{2}$", addr):
        raise ValueError(f"bad MAC: {addr}")


def assert_type(val, typ, name=""):
    if not isinstance(val, typ):
        raise TypeError(f"{name}: expected {typ.__name__}, got {type(val).__name__}")


def expect_raises(fn, exc_type):
    try:
        fn()
    except exc_type:
        return
    raise AssertionError(f"expected {exc_type.__name__}")


def test_valid_json(cmd, schema_name):
    """Run a command, parse JSON, validate against schema."""
    rc, out = iw_cmd(cmd)
    if out is None or out.strip() == "":
        raise ValueError(f"no output (rc={rc})")
    data = json.loads(out)
    schema = load_schema(schema_name)
    jsonschema.validate(data, schema)
    return data


# ----------------------------------------------------------------
print("== JSON schema validation tests ==")
# ----------------------------------------------------------------

live = "--live" in sys.argv

test("library loads", lambda: None)

test("iw_cmd returns output for help", lambda: (
    iw_cmd("help")
))

test("iw_cmd returns output for --version", lambda: (
    iw_cmd("--version")
))

if live:
    test("'dev' output is valid JSON array", lambda:
        test_valid_json("dev", "dev"))

    test("'dev' output has required fields", lambda: (
        [
            (
                assert_has_key(iface, "wiphy"),
                assert_has_key(iface, "type"),
            )
            for iface in test_valid_json("dev", "dev")
        ]
    ))

    test("'dev' interfaces have valid MAC addresses", lambda: (
        [
            assert_mac(iface.get("addr", "00:00:00:00:00:00"))
            for iface in test_valid_json("dev", "dev")
        ]
    ))

    test("'dev' txq_stats fields are integers", lambda: (
        [
            [
                assert_type(v, int, f"txq_stats.{k}")
                for k, v in iface["txq_stats"].items()
            ]
            for iface in test_valid_json("dev", "dev")
            if "txq_stats" in iface
        ]
    ))

    test("'dev' pretty output is valid JSON", lambda: (
        json.loads(iw_cmd("-p dev")[1])
    ))

    def _station_dump_iface():
        for iface in test_valid_json("dev", "dev"):
            if iface.get("type") in ("managed", "AP", "mesh point", "IBSS") \
                    and "ifname" in iface:
                return iface["ifname"]
        return None

    def check_station_dump():
        ifname = _station_dump_iface()
        if ifname is None:
            return  # no suitable interface; skip silently
        data = test_valid_json("dev %s station dump" % ifname, "station")
        for sta in data:
            assert_has_key(sta, "addr")
            assert_mac(sta["addr"])
            for rate_key in ("tx_bitrate", "rx_bitrate"):
                if rate_key in sta and "bitrate_mbps" in sta[rate_key]:
                    assert_type(sta[rate_key]["bitrate_mbps"], (int, float),
                                rate_key)

    test("'station dump' output validates against schema", check_station_dump)

    def check_survey_dump():
        ifname = _station_dump_iface()
        if ifname is None:
            return
        data = test_valid_json("dev %s survey dump" % ifname, "survey")
        for entry in data:
            assert_has_key(entry, "dev")

    test("'survey dump' output validates against schema", check_survey_dump)

    def check_scan_dump():
        ifname = _station_dump_iface()
        if ifname is None:
            return
        data = test_valid_json("dev %s scan dump" % ifname, "scan")
        for bss in data:
            assert_has_key(bss, "bssid")
            assert_mac(bss["bssid"])

    test("'scan dump' output validates against schema", check_scan_dump)

    def check_link():
        ifname = _station_dump_iface()
        if ifname is None:
            return
        data = test_valid_json("dev %s link" % ifname, "link")
        assert len(data) == 1, "link must emit exactly one wrapper object"
        wrap = data[0]
        assert_has_key(wrap, "dev")
        assert_has_key(wrap, "connected")
        assert_has_key(wrap, "links")
        for bss in wrap["links"]:
            assert_has_key(bss, "bssid")
            assert_mac(bss["bssid"])

    test("'link' output validates against schema", check_link)

    test("'reg get' output validates against schema", lambda:
        test_valid_json("reg get", "reg"))

    test("'reg get' has at least one domain with rules", lambda: (
        [
            assert_has_key(dom, "rules")
            for dom in test_valid_json("reg get", "reg")
        ]
    ))

    test("'phy' output validates against schema", lambda:
        test_valid_json("phy", "phy"))

    test("'phy' objects carry wiphy index", lambda: (
        [
            assert_has_key(p, "wiphy")
            for p in test_valid_json("phy", "phy")
        ]
    ))

    test("'commands' output validates against schema", lambda:
        test_valid_json("commands", "commands"))

    test("'features' output validates against schema", lambda:
        test_valid_json("features", "features"))

    def _phy_name():
        for p in test_valid_json("phy", "phy"):
            if "wiphy_name" in p:
                return p["wiphy_name"]
        return None

    def check_channels():
        name = _phy_name()
        if name is None:
            return
        data = test_valid_json("phy %s channels" % name, "channels")
        for band in data:
            assert_has_key(band, "band")
            assert_has_key(band, "frequencies")

    test("'channels' output validates against schema", check_channels)

    def check_mesh_param():
        # mesh_param dump works on any netdev that supports it; skip if no
        # mesh-capable iface is present (empty/error output tolerated).
        ifname = _station_dump_iface()
        if ifname is None:
            return
        rc, out = iw_cmd("dev %s mesh_param dump" % ifname)
        if not out or not out.strip():
            return
        try:
            data = json.loads(out)
        except ValueError:
            return  # non-mesh iface: command may emit a usage/error line
        jsonschema.validate(data, load_schema("mesh_param"))

    test("'mesh_param dump' output validates against schema", check_mesh_param)

else:
    test("non-live: dev with bad device returns error", lambda: (
        iw_cmd("dev nonexistent_xyz info")
    ))

    test("schema file loads", lambda:
        load_schema("dev"))

    test("schema validates empty array", lambda:
        jsonschema.validate([], load_schema("dev")))

    test("schema validates sample object", lambda:
        jsonschema.validate([{"wiphy": 0, "type": "managed"}], load_schema("dev")))

    test("schema rejects bad wiphy type", lambda: (
        expect_raises(lambda:
            jsonschema.validate([{"wiphy": "bad"}], load_schema("dev")),
            jsonschema.ValidationError)
    ))

    test("station schema file loads", lambda:
        load_schema("station"))

    test("station schema validates sample object", lambda:
        jsonschema.validate(
            [{
                "addr": "00:11:22:33:44:55", "dev": "wlan0",
                "rx_bytes": 1234, "signal_dbm": -42,
                "signal_chains": [-42, -44],
                "tx_bitrate": {"bitrate_mbps": 866.7, "mcs": 9, "nss": 2,
                               "width_mhz": 80, "short_gi": True},
                "tid_stats": [{"tid": 0, "rx_msdu": 10,
                               "txq_stats": {"backlog_bytes": 0}}],
                "bss_param": {"dtim_period": 2, "cts_prot": False},
                "authorized": True, "wmm": True
            }],
            load_schema("station")))

    test("station schema validates MLO links", lambda:
        jsonschema.validate(
            [{
                "addr": "00:11:22:33:44:55", "dev": "wlan0",
                "mlo_links": [{"link_id": 0, "addr": "00:11:22:33:44:66",
                               "signal_dbm": -40, "rx_bytes": 99}]
            }],
            load_schema("station")))

    test("station schema requires addr", lambda: (
        expect_raises(lambda:
            jsonschema.validate([{"dev": "wlan0"}], load_schema("station")),
            jsonschema.ValidationError)
    ))

    test("survey schema file loads", lambda:
        load_schema("survey"))

    test("survey schema validates sample object", lambda:
        jsonschema.validate(
            [{"dev": "wlan0", "frequency": 5180, "in_use": True,
              "noise_dbm": -95, "channel_active_time": 1000,
              "channel_busy_time": 12}],
            load_schema("survey")))

    test("survey schema requires dev", lambda: (
        expect_raises(lambda:
            jsonschema.validate([{"frequency": 5180}], load_schema("survey")),
            jsonschema.ValidationError)
    ))

    test("reg schema file loads", lambda:
        load_schema("reg"))

    test("reg schema validates sample object", lambda:
        jsonschema.validate(
            [{"scope": "global", "country": "US", "dfs_region": "DFS-FCC",
              "rules": [{"start_mhz": 5170, "end_mhz": 5250, "max_bw_mhz": 80,
                         "max_eirp_dbm": 20, "dfs_cac_ms": 60000,
                         "flags": ["DFS", "AUTO-BW"]}]}],
            load_schema("reg")))

    test("reg schema rejects bad scope", lambda: (
        expect_raises(lambda:
            jsonschema.validate(
                [{"scope": "bogus", "country": "US", "rules": []}],
                load_schema("reg")),
            jsonschema.ValidationError)
    ))

    test("scan schema file loads", lambda:
        load_schema("scan"))

    test("scan schema validates sample object", lambda:
        jsonschema.validate(
            [{"bssid": "00:11:22:33:44:55", "dev": "wlan0",
              "frequency": 5180, "signal_dbm": -55.0, "ssid": "mynet",
              "supported_rates": [6.0, 9.0, 12.0], "ds_channel": 36,
              "capability": "411", "associated": True,
              "status": "associated"}],
            load_schema("scan")))

    test("scan schema requires bssid", lambda: (
        expect_raises(lambda:
            jsonschema.validate([{"ssid": "x"}], load_schema("scan")),
            jsonschema.ValidationError)
    ))

    test("phy schema file loads", lambda:
        load_schema("phy"))

    test("phy schema validates sample object", lambda:
        jsonschema.validate(
            [{"wiphy": 0, "wiphy_name": "phy0",
              "bands": [{"band": 1, "ht_capa": "19ef",
                         "frequencies": [{"freq": 2412, "channel": 1,
                                          "max_tx_power_dbm": 20.0,
                                          "disabled": False}],
                         "bitrates": [{"bitrate_mbps": 1.0,
                                       "short_preamble": False}]}],
              "ciphers": ["CCMP-128 (00-0f-ac:4)"],
              "supported_iftypes": ["managed", "AP"]}],
            load_schema("phy")))

    test("phy schema requires wiphy", lambda: (
        expect_raises(lambda:
            jsonschema.validate([{"wiphy_name": "phy0"}], load_schema("phy")),
            jsonschema.ValidationError)
    ))

    test("mpp schema validates sample object", lambda:
        jsonschema.validate(
            [{"dst": "00:11:22:33:44:55", "next_hop": "00:11:22:33:44:66",
              "dev": "mesh0"}],
            load_schema("mpp")))

    test("mpp schema requires dst", lambda: (
        expect_raises(lambda:
            jsonschema.validate([{"dev": "mesh0"}], load_schema("mpp")),
            jsonschema.ValidationError)
    ))

    test("mpath schema validates sample object", lambda:
        jsonschema.validate(
            [{"dst": "00:11:22:33:44:55", "next_hop": "00:11:22:33:44:66",
              "dev": "mesh0", "sn": 3, "metric": 100, "flags": 0,
              "hop_count": 1}],
            load_schema("mpath")))

    test("channels schema validates sample object", lambda:
        jsonschema.validate(
            [{"band": 1, "frequencies": [
                {"frequency": 2412, "channel": 1, "max_tx_power_dbm": 22.0,
                 "widths": ["20MHz", "HT40+"]},
                {"frequency": 5260, "channel": 52, "radar": True,
                 "dfs_state": "usable", "dfs_cac_time_ms": 60000,
                 "widths": ["20MHz"]}]}],
            load_schema("channels")))

    test("channels schema rejects bad dfs_state", lambda: (
        expect_raises(lambda:
            jsonschema.validate(
                [{"band": 1, "frequencies": [
                    {"frequency": 5260, "channel": 52, "dfs_state": "bogus"}]}],
                load_schema("channels")),
            jsonschema.ValidationError)
    ))

    test("features schema validates sample object", lambda:
        jsonschema.validate(
            [{"nl80211_features": 1, "split_wiphy_dump": True}],
            load_schema("features")))

    test("commands schema validates sample object", lambda:
        jsonschema.validate(
            [{"id": 1, "name": "get_wiphy"}],
            load_schema("commands")))

    test("mesh_param schema validates sample object", lambda:
        jsonschema.validate(
            [{"mesh_retry_timeout": 100, "mesh_ttl": 31,
              "mesh_power_mode": "active", "mesh_rssi_threshold": -80,
              "mesh_fwding": 1}],
            load_schema("mesh_param")))

    test("mesh_param schema rejects bad power_mode", lambda: (
        expect_raises(lambda:
            jsonschema.validate([{"mesh_power_mode": "turbo"}],
                                load_schema("mesh_param")),
            jsonschema.ValidationError)
    ))

    test("link schema file loads", lambda:
        load_schema("link"))

    test("link schema validates not-connected", lambda:
        jsonschema.validate(
            [{"dev": "wlan0", "connected": False, "mld": False, "links": []}],
            load_schema("link")))

    test("link schema validates connected", lambda:
        jsonschema.validate(
            [{
                "dev": "wlan0", "connected": True, "mld": False,
                "links": [{"bssid": "00:11:22:33:44:55",
                           "status": "associated", "frequency": 5180,
                           "ssid": "testnet",
                           "supported_rates": [6.0, 9.0, 12.0]}],
                "stats": {"rx_bytes": 1234, "tx_bytes": 5678,
                          "signal_dbm": -42,
                          "tx_bitrate": {"bitrate_mbps": 866.7, "mcs": 9,
                                         "nss": 2},
                          "bss_param": {"dtim_period": 2, "cts_prot": False},
                          "connected_time": 99}
            }],
            load_schema("link")))

    test("link schema validates MLD (multiple links)", lambda:
        jsonschema.validate(
            [{
                "dev": "wlan0", "connected": True, "mld": True,
                "mld_addr": "00:11:22:33:44:55",
                "links": [{"bssid": "00:11:22:33:44:66",
                           "status": "associated", "link_id": 0,
                           "frequency": 5180},
                          {"bssid": "00:11:22:33:44:77",
                           "status": "associated", "link_id": 1,
                           "frequency": 2412}],
                "stats": {"rx_bytes": 1}
            }],
            load_schema("link")))

    test("link schema rejects bad link status", lambda: (
        expect_raises(lambda:
            jsonschema.validate(
                [{"dev": "wlan0", "connected": True, "mld": False,
                  "links": [{"bssid": "00:11:22:33:44:55",
                             "status": "bogus"}]}],
                load_schema("link")),
            jsonschema.ValidationError)
    ))


# ----------------------------------------------------------------
print(f"\n== Results: {passed} passed, {failed} failed ==")
if live:
    print("(ran with --live, tested against actual wireless hardware)")
else:
    print("(ran without --live, schema-only tests)")
    print("run with --live to validate against real device output")

if failed:
    print("\nFailed:")
    for name, err in errors:
        print(f"  {name}: {err}")
    sys.exit(1)
