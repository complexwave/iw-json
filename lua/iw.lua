local ffi = require("ffi")

ffi.cdef[[
int iw_cmd(const char *cmdline, char **out, size_t *out_len);
void free(void *ptr);
]]

local C = ffi.load("iw")

local M = {}

function M.cmd(fmt, ...)
	local cmdline = fmt:format(...)
	local out = ffi.new("char*[1]")
	local out_len = ffi.new("size_t[1]")

	local rc = C.iw_cmd(cmdline, out, out_len)

	local result = nil
	if out[0] ~= nil then
		result = ffi.string(out[0], out_len[0])
		ffi.C.free(out[0])
	end

	if rc ~= 0 then
		return nil, result or ("iw_cmd failed: " .. tostring(rc))
	end

	return result
end

setmetatable(M, {
	__call = function(_, fmt, ...)
		return M.cmd(fmt, ...)
	end,
})

return M
