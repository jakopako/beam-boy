#include <unity.h>

#include <cstdio>

#include "core/store_index.h"

using beamboy::StoreIndex;

namespace {

const char kSha[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void assertRejects(const char* json) {
  StoreIndex index;
  TEST_ASSERT_FALSE(index.parse(json));
}

}  // namespace

void test_parses_a_valid_store_index() {
  char json[512];
  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"reflexfs\",\"title\":\"Reflex "
           "FS\",\"color\":\"ff00aa\",\"url\":\"https://example.test/"
           "reflexfs/game.be\",\"sha256\":\"%s\",\"size\":\"1234\"}]}",
           kSha);

  StoreIndex index;
  TEST_ASSERT_TRUE(index.parse(json));
  TEST_ASSERT_EQUAL_UINT8(1, index.count());
  TEST_ASSERT_EQUAL_STRING("reflexfs", index.at(0).id);
  TEST_ASSERT_EQUAL_STRING("Reflex FS", index.at(0).title);
  TEST_ASSERT_EQUAL_STRING("ff00aa", index.at(0).color);
  TEST_ASSERT_EQUAL_STRING("https://example.test/reflexfs/game.be",
                           index.at(0).url);
  TEST_ASSERT_EQUAL_STRING(kSha, index.at(0).sha256);
  TEST_ASSERT_EQUAL_UINT32(1234, index.at(0).size);
}

void test_accepts_an_empty_store_index() {
  StoreIndex index;
  TEST_ASSERT_TRUE(index.parse("{\"games\":[]}"));
  TEST_ASSERT_EQUAL_UINT8(0, index.count());
}

void test_rejects_missing_required_fields() {
  char json[512];
  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"reflexfs\",\"title\":\"Reflex "
           "FS\",\"color\":\"ff00aa\",\"url\":\"https://example.test/"
           "game.be\",\"sha256\":\"%s\"}]}",
           kSha);
  assertRejects(json);
}

void test_rejects_path_like_ids() {
  char json[512];
  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"../bad\",\"title\":\"Bad\",\"color\":"
           "\"ff00aa\",\"url\":\"https://example.test/game.be\","
           "\"sha256\":\"%s\",\"size\":\"123\"}]}",
           kSha);
  assertRejects(json);
}

void test_rejects_bad_hashes_and_sizes() {
  assertRejects(
      "{\"games\":[{\"id\":\"bad\",\"title\":\"Bad\",\"color\":\"ff00aa\","
      "\"url\":\"https://example.test/game.be\",\"sha256\":\"abc\","
      "\"size\":\"123\"}]}");

  char json[512];
  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"bad\",\"title\":\"Bad\",\"color\":"
           "\"ff00aa\",\"url\":\"https://example.test/game.be\","
           "\"sha256\":\"%s\",\"size\":\"999999\"}]}",
           kSha);
  assertRejects(json);
}

void test_rejects_nested_or_trailing_json() {
  assertRejects("{\"games\":[{}]}");
  assertRejects("{\"games\":[{\"id\":{\"nested\":\"no\"}}]}");

  char json[512];
  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"ok\",\"title\":\"Ok\",\"color\":"
           "\"ff00aa\",\"url\":\"https://example.test/game.be\","
           "\"sha256\":\"%s\",\"size\":\"123\"}]} true",
           kSha);
  assertRejects(json);
}

void test_rejects_duplicate_fields_and_ids() {
  char json[768];
  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"same\",\"id\":\"same2\",\"title\":\"A\","
           "\"color\":\"ff00aa\",\"url\":\"https://example.test/a.be\","
           "\"sha256\":\"%s\",\"size\":\"1\"}]}",
           kSha);
  assertRejects(json);

  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"same\",\"title\":\"A\",\"color\":"
           "\"ff00aa\",\"url\":\"https://example.test/a.be\","
           "\"sha256\":\"%s\",\"size\":\"1\"},{\"id\":\"same\","
           "\"title\":\"B\",\"color\":\"00ffaa\",\"url\":"
           "\"https://example.test/b.be\",\"sha256\":\"%s\","
           "\"size\":\"1\"}]}",
           kSha, kSha);
  assertRejects(json);
}

void test_rejects_control_characters_in_title_and_url() {
  char json[512];
  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"bad\",\"title\":\"Bad\\nTitle\","
           "\"color\":\"ff00aa\",\"url\":\"https://example.test/game.be\","
           "\"sha256\":\"%s\",\"size\":\"1\"}]}",
           kSha);
  assertRejects(json);

  snprintf(json, sizeof(json),
           "{\"games\":[{\"id\":\"bad\",\"title\":\"Bad\",\"color\":"
           "\"ff00aa\",\"url\":\"https://example.test/game be\","
           "\"sha256\":\"%s\",\"size\":\"1\"}]}",
           kSha);
  assertRejects(json);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_valid_store_index);
  RUN_TEST(test_accepts_an_empty_store_index);
  RUN_TEST(test_rejects_missing_required_fields);
  RUN_TEST(test_rejects_path_like_ids);
  RUN_TEST(test_rejects_bad_hashes_and_sizes);
  RUN_TEST(test_rejects_nested_or_trailing_json);
  RUN_TEST(test_rejects_duplicate_fields_and_ids);
  RUN_TEST(test_rejects_control_characters_in_title_and_url);
  return UNITY_END();
}
