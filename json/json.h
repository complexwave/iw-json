/* SPDX-License-Identifier: ISC */
#ifndef __IW_JSON_H
#define __IW_JSON_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Streaming JSON output for iw.
 *
 * One global writer, live for one iw invocation (caller serializes --
 * see iw_lib_lock in iw.c for the library entry point). Two mutually
 * exclusive modes, chosen once by json_begin():
 *
 *  - JSON mode (on=true): json_begin() opens the top-level array.
 *    json_obj_begin()/json_arr_begin() push a container, scalar calls
 *    (json_i32 etc.) append a value to whatever container is open,
 *    json_end() closes the array, serializes to stdout, and resets.
 *
 *  - plain mode (on=false): container calls are no-ops. Scalar calls
 *    fall through to printf(fmt, value), only when their json_out mask
 *    allows it -- callers own their own newlines/spacing via fmt.
 *
 * json_pretty() must be called before json_begin() (or between
 * json_end()/json_begin() pairs) -- it is read once at json_begin()
 * time, not re-checked per call.
 */

void json_begin(bool on);
void json_end(void);
void json_pretty(bool on);

void json_obj_begin(const char *key);
void json_obj_end(void);
void json_arr_begin(const char *key);
void json_arr_end(void);

enum json_out {
	JSON_OUT_TEXT = 1 << 0,	/* plain-text only */
	JSON_OUT_JSON = 1 << 1,	/* JSON only */
	JSON_OUT_BOTH = 1 << 2,	/* JSON in JSON mode, printf in plain mode */
};

/*
 * key: property name when the current container is an object, NULL
 * when it's an array (unnamed element) or the lone top-level object.
 * fmt: printf format for plain mode; NULL for JSON_OUT_JSON-only call
 * sites (never dereferenced in JSON mode).
 */
void json_i32 (enum json_out t, const char *key, const char *fmt, int32_t v);
void json_i64 (enum json_out t, const char *key, const char *fmt, int64_t v);
void json_u32 (enum json_out t, const char *key, const char *fmt, uint32_t v);
void json_u64 (enum json_out t, const char *key, const char *fmt, uint64_t v);
void json_bool(enum json_out t, const char *key, const char *fmt, bool v);
void json_str (enum json_out t, const char *key, const char *fmt, const char *v);
void json_hex (enum json_out t, const char *key, const char *fmt, uint32_t v);
void json_f64 (enum json_out t, const char *key, const char *fmt, double v);

#endif /* __IW_JSON_H */
