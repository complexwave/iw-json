/* SPDX-License-Identifier: ISC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

#define JSON_BUF_INIT	(16 * 1024)	/* prealloc; covers ~99% of iw output */
#define JSON_MAX_DEPTH	32		/* deepest real handler nests ~6 */

static bool json_mode;
static bool pretty;

static char *buf;
static size_t len, cap;

static struct {
	bool is_array;
	bool first_child;
} stack[JSON_MAX_DEPTH];
static int depth;

static void grow(size_t extra)
{
	size_t want;

	if (len + extra <= cap)
		return;

	want = cap ? cap : JSON_BUF_INIT;
	while (want < len + extra)
		want *= 2;

	buf = realloc(buf, want);
	if (!buf)
		abort();
	cap = want;
}

static void raw(const char *s, size_t n)
{
	grow(n);
	memcpy(buf + len, s, n);
	len += n;
}

static void raws(const char *s)
{
	raw(s, strlen(s));
}

static void indent(void)
{
	int i;

	for (i = 0; i < depth; i++)
		raw("  ", 2);
}

static void emit_string(const char *s)
{
	unsigned char c;

	if (!s) {
		raws("null");
		return;
	}

	raw("\"", 1);
	for (; *s; s++) {
		c = *s;
		switch (c) {
		case '"':  raw("\\\"", 2); break;
		case '\\': raw("\\\\", 2); break;
		case '\n': raw("\\n", 2); break;
		case '\r': raw("\\r", 2); break;
		case '\t': raw("\\t", 2); break;
		default:
			if (c < 0x20) {
				char tmp[8];
				int n = snprintf(tmp, sizeof(tmp), "\\u%04x", c);
				raw(tmp, n);
			} else {
				raw((char *)&c, 1);
			}
		}
	}
	raw("\"", 1);
}

/* comma/newline/indent/key bookkeeping before any value (scalar or container) */
static void begin_value(const char *key)
{
	if (!stack[depth - 1].first_child)
		raw(",", 1);
	stack[depth - 1].first_child = false;

	if (pretty) {
		raw("\n", 1);
		indent();
	}

	if (!stack[depth - 1].is_array) {
		emit_string(key);
		raw(":", 1);
		if (pretty)
			raw(" ", 1);
	}
}

static void push_frame(bool is_array)
{
	if (depth >= JSON_MAX_DEPTH)
		abort();
	stack[depth].is_array = is_array;
	stack[depth].first_child = true;
	depth++;
}

static void end_container(char close)
{
	bool had_children = !stack[depth - 1].first_child;

	depth--;
	if (had_children) {
		if (pretty) {
			raw("\n", 1);
			indent();
		}
	}
	raw(&close, 1);
}

void json_begin(bool on)
{
	json_mode = on;
	if (!on)
		return;

	len = 0;
	cap = JSON_BUF_INIT;
	buf = malloc(cap);
	if (!buf)
		abort();

	depth = 0;
	push_frame(true);
	raw("[", 1);
}

void json_end(void)
{
	if (!json_mode)
		return;

	end_container(']');
	fwrite(buf, 1, len, stdout);

	free(buf);
	buf = NULL;
	len = cap = 0;
	depth = 0;
}

void json_pretty(bool on)
{
	pretty = on;
}

void json_obj_begin(const char *key)
{
	if (!json_mode)
		return;
	begin_value(key);
	raw("{", 1);
	push_frame(false);
}

void json_obj_end(void)
{
	if (!json_mode)
		return;
	end_container('}');
}

void json_arr_begin(const char *key)
{
	if (!json_mode)
		return;
	begin_value(key);
	raw("[", 1);
	push_frame(true);
}

void json_arr_end(void)
{
	if (!json_mode)
		return;
	end_container(']');
}

void json_i32(enum json_out t, const char *key, const char *fmt, int32_t v)
{
	char tmp[16];
	int n;

	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		n = snprintf(tmp, sizeof(tmp), "%d", v);
		raw(tmp, n);
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}

void json_i64(enum json_out t, const char *key, const char *fmt, int64_t v)
{
	char tmp[24];
	int n;

	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
		raw(tmp, n);
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}

void json_u32(enum json_out t, const char *key, const char *fmt, uint32_t v)
{
	char tmp[16];
	int n;

	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		n = snprintf(tmp, sizeof(tmp), "%u", v);
		raw(tmp, n);
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}

void json_u64(enum json_out t, const char *key, const char *fmt, uint64_t v)
{
	char tmp[24];
	int n;

	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		n = snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)v);
		raw(tmp, n);
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}

void json_bool(enum json_out t, const char *key, const char *fmt, bool v)
{
	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		raws(v ? "true" : "false");
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}

void json_str(enum json_out t, const char *key, const char *fmt, const char *v)
{
	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		emit_string(v);
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}

void json_hex(enum json_out t, const char *key, const char *fmt, uint32_t v)
{
	char tmp[16];
	int n;

	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		n = snprintf(tmp, sizeof(tmp), "%x", v);
		raw("\"", 1);
		raw(tmp, n);
		raw("\"", 1);
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}

void json_f64(enum json_out t, const char *key, const char *fmt, double v)
{
	char tmp[32];
	int n;

	if (json_mode) {
		if (!(t & (JSON_OUT_JSON | JSON_OUT_BOTH)))
			return;
		begin_value(key);
		n = snprintf(tmp, sizeof(tmp), "%.6g", v);
		raw(tmp, n);
	} else if (t & (JSON_OUT_TEXT | JSON_OUT_BOTH)) {
		printf(fmt, v);
	}
}
