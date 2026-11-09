#include "space_balloon_communication_format/tlv.h"
#include <stdio.h>
#include <string.h>

/* assert は Release（NDEBUG）で消えるので，自前の確認マクロを使う */
static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
      failures++;                                                              \
    }                                                                          \
  } while (0)

typedef space_balloon_communication_format_tlv_writer_t writer_t;
typedef space_balloon_communication_format_tlv_reader_t reader_t;
typedef space_balloon_communication_format_tlv_t tlv_t;

#define FOUND SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_FOUND
#define END SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_END
#define TRUNCATED SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_TRUNCATED
#define MAX_VALUE SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_MAX_VALUE_SIZE

/* Rust 版（rust/src/tests/mod.rs）と同じバイト列 */
static const uint8_t PRESSURE_TLV[] = {0x01, 0x04, 0xCD, 0x8B, 0x01, 0x00}; /* 気圧 101325 */
static const uint8_t GPS_TLV[] = {
    0x10, 0x0D,             /* Tag, Length */
    0x0A, 0x00, 0x00, 0x00, /* latitude 10 */
    0x14, 0x00, 0x00, 0x00, /* longitude 20 */
    0x1E, 0x00, 0x00, 0x00, /* altitude 30 */
    0x03,                   /* fix_type 3 */
};

static void test_put_matches_rust_implementation(void) {
  uint8_t buf[255];
  writer_t w;
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  CHECK(space_balloon_communication_format_tlv_put_u32(&w, 0x01, 101325));
  CHECK(space_balloon_communication_format_tlv_writer_finish(&w) == sizeof(PRESSURE_TLV));
  CHECK(memcmp(buf, PRESSURE_TLV, sizeof(PRESSURE_TLV)) == 0);
}

static void test_begin_add_end_matches_rust_implementation(void) {
  uint8_t buf[255];
  writer_t w;
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  space_balloon_communication_format_tlv_begin(&w, 0x10);
  space_balloon_communication_format_tlv_add_i32(&w, 10);
  space_balloon_communication_format_tlv_add_i32(&w, 20);
  space_balloon_communication_format_tlv_add_i32(&w, 30);
  space_balloon_communication_format_tlv_add_u8(&w, 3);
  space_balloon_communication_format_tlv_end(&w);
  CHECK(space_balloon_communication_format_tlv_writer_finish(&w) == sizeof(GPS_TLV));
  CHECK(memcmp(buf, GPS_TLV, sizeof(GPS_TLV)) == 0);
}

static void test_signed_values_are_little_endian_twos_complement(void) {
  uint8_t buf[16];
  writer_t w;
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  space_balloon_communication_format_tlv_put_i16(&w, 0x02, -2);
  space_balloon_communication_format_tlv_put_i32(&w, 0x03, -1);
  const uint8_t expected[] = {0x02, 0x02, 0xFE, 0xFF, 0x03, 0x04, 0xFF, 0xFF, 0xFF, 0xFF};
  CHECK(space_balloon_communication_format_tlv_writer_finish(&w) == sizeof(expected));
  CHECK(memcmp(buf, expected, sizeof(expected)) == 0);
}

/** 書いた値を読み戻すと同じになること（型ごとの端の値も） */
static void test_round_trip_all_types(void) {
  uint8_t buf[255];
  writer_t w;
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  space_balloon_communication_format_tlv_begin(&w, 0x20);
  space_balloon_communication_format_tlv_add_u8(&w, 0xFF);
  space_balloon_communication_format_tlv_add_i16(&w, INT16_MIN);
  space_balloon_communication_format_tlv_add_i16(&w, INT16_MAX);
  space_balloon_communication_format_tlv_add_u32(&w, UINT32_MAX);
  space_balloon_communication_format_tlv_add_i32(&w, INT32_MIN);
  space_balloon_communication_format_tlv_add_i32(&w, INT32_MAX);
  space_balloon_communication_format_tlv_end(&w);
  size_t n = space_balloon_communication_format_tlv_writer_finish(&w);
  CHECK(n == 2 + 1 + 2 + 2 + 4 + 4 + 4);

  reader_t r;
  tlv_t tlv;
  uint8_t u8 = 0;
  int16_t i16a = 0, i16b = 0;
  uint32_t u32 = 0;
  int32_t i32a = 0, i32b = 0;
  space_balloon_communication_format_tlv_reader_init(&r, buf, n);
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == FOUND);
  CHECK(tlv.tag == 0x20);
  CHECK((size_t)tlv.length == n - 2);
  CHECK(space_balloon_communication_format_tlv_read_u8(&tlv, &u8) && u8 == 0xFF);
  CHECK(space_balloon_communication_format_tlv_read_i16(&tlv, &i16a) && i16a == INT16_MIN);
  CHECK(space_balloon_communication_format_tlv_read_i16(&tlv, &i16b) && i16b == INT16_MAX);
  CHECK(space_balloon_communication_format_tlv_read_u32(&tlv, &u32) && u32 == UINT32_MAX);
  CHECK(space_balloon_communication_format_tlv_read_i32(&tlv, &i32a) && i32a == INT32_MIN);
  CHECK(space_balloon_communication_format_tlv_read_i32(&tlv, &i32b) && i32b == INT32_MAX);
  /* Value を読み切ったら，それ以上は読めない */
  CHECK(!space_balloon_communication_format_tlv_read_u8(&tlv, &u8));
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == END);
}

/** 知らないタグを読み飛ばして，その次のタグを読めること */
static void test_reader_skips_unknown_tags(void) {
  uint8_t payload[sizeof(PRESSURE_TLV) + 4 + sizeof(GPS_TLV)];
  const uint8_t unknown[] = {0x7A, 0x02, 0xFF, 0xFF};
  memcpy(payload, PRESSURE_TLV, sizeof(PRESSURE_TLV));
  memcpy(payload + sizeof(PRESSURE_TLV), unknown, sizeof(unknown));
  memcpy(payload + sizeof(PRESSURE_TLV) + sizeof(unknown), GPS_TLV, sizeof(GPS_TLV));

  reader_t r;
  tlv_t tlv;
  uint32_t pressure = 0;
  int32_t latitude = 0;
  int found_gps = 0;
  space_balloon_communication_format_tlv_reader_init(&r, payload, sizeof(payload));
  while (space_balloon_communication_format_tlv_next(&r, &tlv) == FOUND) {
    if (tlv.tag == 0x01) {
      space_balloon_communication_format_tlv_read_u32(&tlv, &pressure);
    } else if (tlv.tag == 0x10) {
      space_balloon_communication_format_tlv_read_i32(&tlv, &latitude);
      found_gps = 1;
    }
    /* 0x7A は何もしない（次の next で飛ばされる） */
  }
  CHECK(pressure == 101325);
  CHECK(found_gps && latitude == 10);
}

static void test_reader_reports_truncated(void) {
  const uint8_t short_value[] = {0x01, 0x04, 0xCD, 0x8B}; /* Length 4 なのに 2 バイトしかない */
  const uint8_t only_tag[] = {0x01};                      /* Length がない */
  reader_t r;
  tlv_t tlv;

  space_balloon_communication_format_tlv_reader_init(&r, short_value, sizeof(short_value));
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == TRUNCATED);
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == END); /* それ以上は読まない */

  space_balloon_communication_format_tlv_reader_init(&r, only_tag, sizeof(only_tag));
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == TRUNCATED);
}

static void test_reader_handles_empty_value_and_empty_payload(void) {
  const uint8_t empty_value[] = {0x05, 0x00};
  reader_t r;
  tlv_t tlv;
  uint8_t u8;

  space_balloon_communication_format_tlv_reader_init(&r, empty_value, sizeof(empty_value));
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == FOUND);
  CHECK(tlv.tag == 0x05 && tlv.length == 0);
  CHECK(!space_balloon_communication_format_tlv_read_u8(&tlv, &u8));
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == END);

  space_balloon_communication_format_tlv_reader_init(&r, NULL, 0);
  CHECK(space_balloon_communication_format_tlv_next(&r, &tlv) == END);
}

static void test_writer_fails_when_buffer_is_full(void) {
  uint8_t buf[5];
  writer_t w;
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  CHECK(!space_balloon_communication_format_tlv_put_u32(&w, 0x01, 1)); /* 6 バイト要る */
  CHECK(space_balloon_communication_format_tlv_writer_finish(&w) == 0);
  /* 一度失敗したら，入る大きさでも以降はすべて失敗する */
  CHECK(!space_balloon_communication_format_tlv_put_u8(&w, 0x02, 1));
}

static void test_writer_accepts_max_value_and_rejects_longer(void) {
  uint8_t data[MAX_VALUE + 1] = {0};
  uint8_t buf[300];
  writer_t w;

  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  space_balloon_communication_format_tlv_begin(&w, 0x01);
  CHECK(space_balloon_communication_format_tlv_add_bytes(&w, data, MAX_VALUE));
  CHECK(space_balloon_communication_format_tlv_end(&w));
  CHECK(buf[1] == MAX_VALUE);

  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  space_balloon_communication_format_tlv_begin(&w, 0x01);
  CHECK(!space_balloon_communication_format_tlv_add_bytes(&w, data, MAX_VALUE + 1));
  CHECK(space_balloon_communication_format_tlv_writer_finish(&w) == 0);
}

static void test_writer_rejects_wrong_order(void) {
  uint8_t buf[32];
  writer_t w;

  /* begin せずに add */
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  CHECK(!space_balloon_communication_format_tlv_add_u8(&w, 1));

  /* begin せずに end */
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  CHECK(!space_balloon_communication_format_tlv_end(&w));

  /* end せずに次の begin */
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  space_balloon_communication_format_tlv_begin(&w, 0x01);
  CHECK(!space_balloon_communication_format_tlv_begin(&w, 0x02));

  /* end せずに finish */
  space_balloon_communication_format_tlv_writer_init(&w, buf, sizeof(buf));
  space_balloon_communication_format_tlv_begin(&w, 0x01);
  space_balloon_communication_format_tlv_add_u8(&w, 1);
  CHECK(space_balloon_communication_format_tlv_writer_finish(&w) == 0);
}

#define RUN(test)                                                              \
  do {                                                                         \
    printf("%s\n", #test);                                                     \
    test();                                                                    \
  } while (0)

int main(void) {
  RUN(test_put_matches_rust_implementation);
  RUN(test_begin_add_end_matches_rust_implementation);
  RUN(test_signed_values_are_little_endian_twos_complement);
  RUN(test_round_trip_all_types);
  RUN(test_reader_skips_unknown_tags);
  RUN(test_reader_reports_truncated);
  RUN(test_reader_handles_empty_value_and_empty_payload);
  RUN(test_writer_fails_when_buffer_is_full);
  RUN(test_writer_accepts_max_value_and_rejects_longer);
  RUN(test_writer_rejects_wrong_order);

  if (failures > 0) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
