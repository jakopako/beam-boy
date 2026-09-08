// Beam Boy — host tests for the cartridge metadata parser.
//
// This parser exists to read `meta.json` out of a game folder, and after
// Phase 7 those folders arrive over the network. So the interesting tests here
// are not the happy path -- they are the malformed inputs, which is where a
// hand-rolled parser earns or loses its keep.
//
// The contract being pinned down is "reject, don't guess": anything that isn't
// a flat object of string values must fail outright, so a bad cartridge is
// absent from the launcher rather than present and subtly wrong.

#include <string.h>
#include <unity.h>

#include "core/json_lite.h"

using namespace beamboy;

namespace {

struct Captured {
  char keys[8][json::kMaxValueLength + 1];
  char values[8][json::kMaxValueLength + 1];
  int count;
};

Captured g_captured;

bool capture(void* user, const char* key, const char* value) {
  Captured* c = static_cast<Captured*>(user);
  if (c->count >= 8) return false;
  strcpy(c->keys[c->count], key);
  strcpy(c->values[c->count], value);
  c->count++;
  return true;
}

bool parse(const char* text) {
  g_captured.count = 0;
  return json::parseFlatObject(text, capture, &g_captured);
}

// A handler that refuses everything, to check that a handler's rejection
// really does abort the whole parse.
bool rejectAll(void*, const char*, const char*) { return false; }

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_parses_a_realistic_meta_json(void) {
  TEST_ASSERT_TRUE(parse(
      "{\"id\": \"snake\", \"title\": \"Snake\", \"color\": \"ff8000\"}"));
  TEST_ASSERT_EQUAL_INT(3, g_captured.count);
  TEST_ASSERT_EQUAL_STRING("id", g_captured.keys[0]);
  TEST_ASSERT_EQUAL_STRING("snake", g_captured.values[0]);
  TEST_ASSERT_EQUAL_STRING("title", g_captured.keys[1]);
  TEST_ASSERT_EQUAL_STRING("Snake", g_captured.values[1]);
  TEST_ASSERT_EQUAL_STRING("color", g_captured.keys[2]);
  TEST_ASSERT_EQUAL_STRING("ff8000", g_captured.values[2]);
}

void test_tolerates_whitespace_and_newlines(void) {
  TEST_ASSERT_TRUE(parse("  {\n\t\"id\"  :  \"a\" ,\n \"title\":\"B\"\n}  \n"));
  TEST_ASSERT_EQUAL_INT(2, g_captured.count);
  TEST_ASSERT_EQUAL_STRING("a", g_captured.values[0]);
  TEST_ASSERT_EQUAL_STRING("B", g_captured.values[1]);
}

void test_accepts_an_empty_object(void) {
  // Well-formed but carries no fields. Rejecting it for lacking an id is the
  // caller's job, not the parser's.
  TEST_ASSERT_TRUE(parse("{}"));
  TEST_ASSERT_EQUAL_INT(0, g_captured.count);
}

void test_decodes_the_supported_escapes(void) {
  TEST_ASSERT_TRUE(parse("{\"title\": \"a\\\"b\\\\c\\nd\"}"));
  TEST_ASSERT_EQUAL_STRING("a\"b\\c\nd", g_captured.values[0]);
}

void test_rejects_unicode_escapes(void) {
  // \u is refused rather than half-decoded. Correct handling needs UTF-8
  // encoding and surrogate pairs; a partial implementation is worse than none.
  TEST_ASSERT_FALSE(parse("{\"title\": \"\\u0041\"}"));
}

void test_rejects_non_string_values(void) {
  // meta.json has no numeric, boolean or null fields, so encountering one
  // means the file is not the format we think it is.
  TEST_ASSERT_FALSE(parse("{\"count\": 3}"));
  TEST_ASSERT_FALSE(parse("{\"ok\": true}"));
  TEST_ASSERT_FALSE(parse("{\"x\": null}"));
}

void test_rejects_nested_structures(void) {
  TEST_ASSERT_FALSE(parse("{\"meta\": {\"id\": \"x\"}}"));
  TEST_ASSERT_FALSE(parse("{\"tags\": [\"a\", \"b\"]}"));
}

void test_rejects_structural_errors(void) {
  TEST_ASSERT_FALSE(parse(""));
  TEST_ASSERT_FALSE(parse("{"));
  TEST_ASSERT_FALSE(parse("{\"id\"}"));
  TEST_ASSERT_FALSE(parse("{\"id\": }"));
  TEST_ASSERT_FALSE(parse("{\"id\" \"x\"}"));
  TEST_ASSERT_FALSE(parse("{\"id\": \"x\",}"));
  TEST_ASSERT_FALSE(parse("[\"id\"]"));
  TEST_ASSERT_FALSE(parse("\"bare string\""));
}

void test_rejects_an_unterminated_string(void) {
  // The scan must stop at the NUL rather than running off the buffer.
  TEST_ASSERT_FALSE(parse("{\"id\": \"unterminated"));
  TEST_ASSERT_FALSE(parse("{\"id\": \"trailing backslash\\"));
}

void test_rejects_trailing_content(void) {
  // Two objects back to back, or junk after the close, means the document is
  // not a single meta.json -- silently ignoring the remainder would be worse.
  TEST_ASSERT_FALSE(parse("{\"id\": \"a\"} {\"id\": \"b\"}"));
  TEST_ASSERT_FALSE(parse("{\"id\": \"a\"} garbage"));
}

void test_rejects_an_over_long_value_rather_than_truncating(void) {
  // A truncated title would show up in the launcher looking like a firmware
  // bug. Refusing the cartridge makes the fault visible where it belongs.
  char text[json::kMaxValueLength * 2];
  strcpy(text, "{\"title\": \"");
  const size_t start = strlen(text);
  for (size_t i = 0; i < json::kMaxValueLength + 4; i++) {
    text[start + i] = 'x';
  }
  strcpy(text + start + json::kMaxValueLength + 4, "\"}");

  TEST_ASSERT_FALSE(parse(text));
}

void test_a_handler_rejection_fails_the_parse(void) {
  // How the caller enforces its own limits (an id longer than a storage key,
  // say) using the same failure path as a syntax error.
  TEST_ASSERT_FALSE(
      json::parseFlatObject("{\"id\": \"x\"}", rejectAll, nullptr));
}

void test_rejects_null_input(void) {
  TEST_ASSERT_FALSE(json::parseFlatObject(nullptr, capture, &g_captured));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_realistic_meta_json);
  RUN_TEST(test_tolerates_whitespace_and_newlines);
  RUN_TEST(test_accepts_an_empty_object);
  RUN_TEST(test_decodes_the_supported_escapes);
  RUN_TEST(test_rejects_unicode_escapes);
  RUN_TEST(test_rejects_non_string_values);
  RUN_TEST(test_rejects_nested_structures);
  RUN_TEST(test_rejects_structural_errors);
  RUN_TEST(test_rejects_an_unterminated_string);
  RUN_TEST(test_rejects_trailing_content);
  RUN_TEST(test_rejects_an_over_long_value_rather_than_truncating);
  RUN_TEST(test_a_handler_rejection_fails_the_parse);
  RUN_TEST(test_rejects_null_input);
  return UNITY_END();
}
