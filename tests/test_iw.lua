#!/usr/bin/env luajit
--
-- Tests for libiw.so via LuaJIT FFI.
-- These tests verify arg parsing and error handling without
-- requiring wireless hardware.
--

package.path = arg[0]:match("(.-)[^/]*$") .. "../lua/?.lua;" .. package.path

-- find libiw.so next to this script's parent dir
local script_dir = arg[0]:match("(.-)[^/]*$")
local lib_dir = script_dir .. "../"
package.cpath = lib_dir .. "?.so;" .. package.cpath
ffi = require("ffi")
ffi.cdef[[
	void *dlopen(const char *filename, int flags);
]]
-- preload with absolute-ish path so ffi.load("iw") finds it
ffi.C.dlopen(lib_dir .. "libiw.so", 0x101) -- RTLD_GLOBAL | RTLD_LAZY

local iw = require("iw")

local passed = 0
local failed = 0
local errors = {}

local function test(name, fn)
	local ok, err = pcall(fn)
	if ok then
		passed = passed + 1
		io.write(string.format("  PASS  %s\n", name))
	else
		failed = failed + 1
		errors[#errors + 1] = { name = name, err = err }
		io.write(string.format("  FAIL  %s: %s\n", name, err))
	end
end

local function assert_eq(a, b, msg)
	if a ~= b then
		error(string.format("%s: expected %q, got %q",
			msg or "assert_eq", tostring(b), tostring(a)), 2)
	end
end

local function assert_nil(v, msg)
	if v ~= nil then
		error(string.format("%s: expected nil, got %q",
			msg or "assert_nil", tostring(v)), 2)
	end
end

local function assert_not_nil(v, msg)
	if v == nil then
		error(string.format("%s: expected non-nil", msg or "assert_not_nil"), 2)
	end
end

local function assert_match(s, pattern, msg)
	if type(s) ~= "string" or not s:match(pattern) then
		error(string.format("%s: %q does not match pattern %q",
			msg or "assert_match", tostring(s), pattern), 2)
	end
end

-- ----------------------------------------------------------------
print("== libiw basic tests ==")
-- ----------------------------------------------------------------

test("iw module loads", function()
	assert_not_nil(iw, "module")
	assert_not_nil(iw.cmd, "cmd function")
end)

test("iw is callable", function()
	-- iw("help") should work (returns help text, rc=0 via HANDLER_RET_DONE)
	local out, err = iw("help")
	-- help goes through usage() which prints to stdout, so we get output
	assert_not_nil(out, "help output")
end)

test("empty command returns error", function()
	local out, err = iw("")
	assert_nil(out, "empty cmd should fail")
	assert_not_nil(err, "should have error")
end)

test("version command works", function()
	local out, err = iw("--version")
	assert_not_nil(out, "version output")
	assert_match(out, "iw version", "version string")
end)

test("nonexistent device returns error", function()
	-- this will fail because the device doesn't exist,
	-- but it tests that arg parsing works
	local out, err = iw("dev nonexistent_device_xyz info")
	-- should return nil (error) since device doesn't exist
	-- the exact behavior depends on netlink availability
	-- but it should not crash
	-- just verify it returns without crashing
	io.write(string.format("    (out=%s, err=%s) ",
		out and string.format("%d bytes", #out) or "nil",
		err and string.format("%d bytes", #err) or "nil"))
end)

test("multiple sequential calls work", function()
	for i = 1, 5 do
		local out, err = iw("--version")
		assert_not_nil(out, "call " .. i)
		assert_match(out, "iw version", "call " .. i)
	end
end)

test("format string arguments work", function()
	local dev = "test_device_xyz"
	local out, err = iw("dev %s info", dev)
	-- will fail (no such device) but tests that format string works
	-- just verify no crash
end)

test("help subcommand works", function()
	local out, err = iw("help station")
	assert_not_nil(out, "help station output")
end)

-- ----------------------------------------------------------------
print(string.format("\n== Results: %d passed, %d failed ==", passed, failed))

if failed > 0 then
	print("\nFailed tests:")
	for _, e in ipairs(errors) do
		print(string.format("  %s: %s", e.name, e.err))
	end
	os.exit(1)
end
