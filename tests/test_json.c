/* SPDX-License-Identifier: ISC */
/*
 * Standalone unit tests for json/json.c -- no netlink, no hardware.
 * Build/run: see `make test-json`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../json/json.h"

static int passed, failed;

static void report(const char *name, int ok, const char *detail)
{
	if (ok) {
		passed++;
		printf("  PASS  %s\n", name);
	} else {
		failed++;
		printf("  FAIL  %s\n", detail ? detail : name);
	}
}

/* redirect stdout to a temp file for the duration of fn(), return captured bytes */
static char *capture(void (*fn)(void), size_t *out_len)
{
	char path[] = "/tmp/iw_test_json_XXXXXX";
	int fd, saved;
	FILE *f;
	char *buf;
	long sz;

	fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp");
		exit(1);
	}

	fflush(stdout);
	saved = dup(STDOUT_FILENO);
	dup2(fd, STDOUT_FILENO);
	close(fd);

	fn();

	fflush(stdout);
	dup2(saved, STDOUT_FILENO);
	close(saved);

	f = fopen(path, "r");
	unlink(path);
	if (!f) {
		perror("fopen");
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	rewind(f);
	buf = malloc(sz + 1);
	if (!buf) {
		fclose(f);
		exit(1);
	}
	if (fread(buf, 1, sz, f) != (size_t)sz) {
		fclose(f);
		exit(1);
	}
	buf[sz] = '\0';
	fclose(f);

	if (out_len)
		*out_len = (size_t)sz;
	return buf;
}

static void check_str(const char *name, void (*fn)(void), const char *expect)
{
	char *got = capture(fn, NULL);
	char detail[512];

	if (strcmp(got, expect) != 0) {
		snprintf(detail, sizeof(detail),
			 "%s: got %s, want %s", name, got, expect);
		report(name, 0, detail);
	} else {
		report(name, 1, NULL);
	}
	free(got);
}

/* ---- cases ---- */

static void case_empty_array(void)
{
	json_begin(true);
	json_end();
}

static void case_empty_object(void)
{
	json_begin(true);
	json_obj_begin(NULL);
	json_obj_end();
	json_end();
}

static void case_scalars(void)
{
	json_begin(true);
	json_obj_begin(NULL);
	json_i32(JSON_OUT_JSON, "i32", NULL, -5);
	json_u32(JSON_OUT_JSON, "u32", NULL, 42);
	json_i64(JSON_OUT_JSON, "i64", NULL, -9000000000LL);
	json_u64(JSON_OUT_JSON, "u64", NULL, 18446744073709551615ULL);
	json_bool(JSON_OUT_JSON, "t", NULL, true);
	json_bool(JSON_OUT_JSON, "f", NULL, false);
	json_hex(JSON_OUT_JSON, "hex", NULL, 0xff);
	json_f64(JSON_OUT_JSON, "f64", NULL, 3.5);
	json_obj_end();
	json_end();
}

static void case_string_escaping(void)
{
	json_begin(true);
	json_obj_begin(NULL);
	/* a " b \ c <LF> d <TAB> e <0x01> f */
	json_str(JSON_OUT_JSON, "s", NULL, "a\"b\\c\nd\te\x01" "f");
	json_obj_end();
	json_end();
}

static void case_array_of_scalars(void)
{
	json_begin(true);
	json_arr_begin(NULL);
	json_i32(JSON_OUT_JSON, NULL, NULL, 6);
	json_i32(JSON_OUT_JSON, NULL, NULL, 9);
	json_i32(JSON_OUT_JSON, NULL, NULL, 12);
	json_arr_end();
	json_end();
}

static void nested_body(void)
{
	json_obj_begin(NULL);
	json_i32(JSON_OUT_JSON, "a", NULL, 1);
	json_arr_begin("arr");
	json_i32(JSON_OUT_JSON, NULL, NULL, 2);
	json_i32(JSON_OUT_JSON, NULL, NULL, 3);
	json_arr_end();
	json_obj_begin("empty");
	json_obj_end();
	json_obj_end();
}

static void case_nested_compact(void)
{
	json_begin(true);
	nested_body();
	json_end();
}

static void case_nested_pretty(void)
{
	json_pretty(true);
	json_begin(true);
	nested_body();
	json_end();
	json_pretty(false);
}

static void case_dual_plain_mode(void)
{
	json_begin(false);
	json_obj_begin("ignored");	/* no-op in plain mode */
	json_i32(JSON_OUT_BOTH, "y", "val=%d", 9);
	json_obj_end();
	json_end();
}

static void case_text_only_masked_in_json_mode(void)
{
	json_begin(true);
	json_i32(JSON_OUT_TEXT, NULL, NULL, 123);	/* must not appear */
	json_end();
}

static void case_json_only_masked_in_plain_mode(void)
{
	json_begin(false);
	json_i32(JSON_OUT_JSON, NULL, "%d", 123);	/* must not print */
	json_end();
}

#define GROW_N 3000

static void case_buffer_growth(void)
{
	int i;
	char key[16];

	json_begin(true);
	json_obj_begin(NULL);
	for (i = 0; i < GROW_N; i++) {
		snprintf(key, sizeof(key), "k%d", i);
		json_i32(JSON_OUT_JSON, key, NULL, i);
	}
	json_obj_end();
	json_end();
}

static void check_buffer_growth(void)
{
	char *got;
	size_t got_len;
	char *want;
	size_t want_cap = GROW_N * 24 + 32;
	size_t want_len = 0;
	int i;
	int ok;
	char detail[128];

	got = capture(case_buffer_growth, &got_len);

	want = malloc(want_cap);
	want_len += sprintf(want + want_len, "[{");
	for (i = 0; i < GROW_N; i++) {
		want_len += sprintf(want + want_len, "%s\"k%d\":%d",
				     i ? "," : "", i, i);
	}
	want_len += sprintf(want + want_len, "}]");

	ok = got_len == want_len && memcmp(got, want, want_len) == 0;
	if (!ok)
		snprintf(detail, sizeof(detail),
			 "buffer growth (>16KB): len got=%zu want=%zu",
			 got_len, want_len);
	report("buffer growth (>16KB) matches byte-for-byte", ok,
	       ok ? NULL : detail);

	free(got);
	free(want);
}

int main(void)
{
	printf("== json.c unit tests ==\n");

	check_str("empty array", case_empty_array, "[]");
	check_str("empty object", case_empty_object, "[{}]");
	check_str("scalars: all types encode correctly", case_scalars,
		  "[{\"i32\":-5,\"u32\":42,\"i64\":-9000000000,"
		  "\"u64\":18446744073709551615,\"t\":true,\"f\":false,"
		  "\"hex\":\"ff\",\"f64\":3.5}]");
	check_str("string escaping", case_string_escaping,
		  "[{\"s\":\"a\\\"b\\\\c\\nd\\te\\u0001f\"}]");
	check_str("array of unnamed scalars", case_array_of_scalars,
		  "[[6,9,12]]");
	check_str("nested object/array (compact)", case_nested_compact,
		  "[{\"a\":1,\"arr\":[2,3],\"empty\":{}}]");
	check_str("nested object/array (pretty)", case_nested_pretty,
		  "[\n  {\n    \"a\": 1,\n    \"arr\": [\n      2,\n      3"
		  "\n    ],\n    \"empty\": {}\n  }\n]");
	check_str("JSON_OUT_BOTH in plain mode: printf passthrough, "
		  "containers no-op", case_dual_plain_mode, "val=9");
	check_str("JSON_OUT_TEXT masked in JSON mode", case_text_only_masked_in_json_mode,
		  "[]");
	check_str("JSON_OUT_JSON masked in plain mode", case_json_only_masked_in_plain_mode,
		  "");
	check_buffer_growth();

	printf("\n== Results: %d passed, %d failed ==\n", passed, failed);
	return failed ? 1 : 0;
}
