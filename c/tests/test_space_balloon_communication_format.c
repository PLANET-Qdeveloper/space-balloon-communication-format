#include "space_balloon_communication_format/space_balloon_communication_format.h"
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

typedef space_balloon_communication_format_t packet_t;
typedef space_balloon_communication_format_decoder_t decoder_t;
typedef space_balloon_communication_format_decode_result_t result_t;

#define COMPLETED SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED
#define MAX_FRAME SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_FRAME_SIZE

/** Rust 版（rust/src/tests/format.rs）と同じフレーム：宛先 0x10，送信元 0x01，ペイロード 01 02 */
static const uint8_t KNOWN_FRAME[] = {0x7E, 0x10, 0x01, 0x02, 0x01, 0x02, 0x1E, 0xF1, 0x7F};

static void make_packet(packet_t *packet, uint8_t destination, uint8_t source,
                        const uint8_t *payload, uint8_t length) {
  space_balloon_communication_format_clear(packet);
  packet->destination_id = destination;
  packet->source_id = source;
  packet->payload_length = length;
  if (length > 0) {
    memcpy(packet->payload, payload, length);
  }
}

/** バイト列をデコードして，最後のバイトの結果を返す */
static result_t decode_all(decoder_t *decoder, const uint8_t *bytes, size_t length) {
  result_t result = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
  for (size_t i = 0; i < length; i++) {
    result = space_balloon_communication_format_decode(decoder, bytes[i]);
  }
  return result;
}

static void test_crc_matches_ccitt_false_check_value(void) {
  const uint8_t data[] = "123456789";
  CHECK(space_balloon_communication_format_calculate_crc_checksum(data, 9) == 0x29B1);
}

static void test_encode_matches_rust_implementation(void) {
  const uint8_t payload[] = {0x01, 0x02};
  packet_t packet;
  uint8_t out[MAX_FRAME];

  make_packet(&packet, 0x10, 0x01, payload, sizeof(payload));
  size_t n = space_balloon_communication_format_encode(&packet, out, sizeof(out));
  CHECK(n == sizeof(KNOWN_FRAME));
  CHECK(memcmp(out, KNOWN_FRAME, sizeof(KNOWN_FRAME)) == 0);
}

static void test_encode_returns_zero_when_output_is_too_small(void) {
  const uint8_t payload[] = {0x01, 0x02};
  packet_t packet;
  uint8_t out[sizeof(KNOWN_FRAME)];

  make_packet(&packet, 0x10, 0x01, payload, sizeof(payload));
  CHECK(space_balloon_communication_format_encode(&packet, out, sizeof(out) - 1) == 0);
  CHECK(space_balloon_communication_format_encode(&packet, out, sizeof(out)) == sizeof(out));
}

/** 区切りと同じバイトを含んでいても元に戻ること */
static void test_round_trip_with_stuffed_bytes(void) {
  const uint8_t payload[] = {0x7E, 0x7F, 0x7D, 0x00, 0xFF};
  packet_t packet;
  decoder_t decoder;
  uint8_t out[MAX_FRAME];

  make_packet(&packet, 0x7E, 0x7F, payload, sizeof(payload));
  size_t n = space_balloon_communication_format_encode(&packet, out, sizeof(out));
  /* 先頭と末尾以外に生の 0x7E / 0x7F が出てこない */
  for (size_t i = 1; i + 1 < n; i++) {
    CHECK(out[i] != 0x7E && out[i] != 0x7F);
  }

  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, out, n) == COMPLETED);
  CHECK(decoder.packet.destination_id == 0x7E);
  CHECK(decoder.packet.source_id == 0x7F);
  CHECK(decoder.packet.payload_length == sizeof(payload));
  CHECK(memcmp(decoder.packet.payload, payload, sizeof(payload)) == 0);
}

static void test_round_trip_empty_payload(void) {
  packet_t packet;
  decoder_t decoder;
  uint8_t out[MAX_FRAME];

  make_packet(&packet, 0x01, 0x10, NULL, 0);
  size_t n = space_balloon_communication_format_encode(&packet, out, sizeof(out));
  CHECK(n == 7); /* 7E + ヘッダ3 + CRC2 + 7F */

  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, out, n) == COMPLETED);
  CHECK(decoder.packet.payload_length == 0);
}

/** 全部のバイトがスタッフィングされる最悪の場合でも MAX_FRAME_SIZE に収まること */
static void test_worst_case_fits_max_frame_size(void) {
  packet_t packet;
  decoder_t decoder;
  uint8_t out[MAX_FRAME];

  space_balloon_communication_format_clear(&packet);
  packet.destination_id = 0x7E;
  packet.source_id = 0x7E;
  packet.payload_length = SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_PAYLOAD_SIZE;
  memset(packet.payload, 0x7D, sizeof(packet.payload));
  size_t n = space_balloon_communication_format_encode(&packet, out, sizeof(out));
  CHECK(n > 0);

  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, out, n) == COMPLETED);
  CHECK(memcmp(decoder.packet.payload, packet.payload, sizeof(packet.payload)) == 0);
}

/** 2 つのストリームを 1 バイトずつ交互に読んでも混ざらないこと */
static void test_two_decoders_do_not_interfere(void) {
  const uint8_t payload_a[] = {0xAA, 0xAB};
  const uint8_t payload_b[] = {0xBB, 0xBC, 0xBD};
  packet_t a, b;
  decoder_t decoder_a, decoder_b;
  uint8_t out_a[MAX_FRAME], out_b[MAX_FRAME];
  result_t result_a = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
  result_t result_b = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;

  make_packet(&a, 0x01, 0x10, payload_a, sizeof(payload_a));
  make_packet(&b, 0x01, 0x20, payload_b, sizeof(payload_b));
  size_t n_a = space_balloon_communication_format_encode(&a, out_a, sizeof(out_a));
  size_t n_b = space_balloon_communication_format_encode(&b, out_b, sizeof(out_b));

  space_balloon_communication_format_decoder_init(&decoder_a);
  space_balloon_communication_format_decoder_init(&decoder_b);
  for (size_t i = 0; i < n_a || i < n_b; i++) {
    if (i < n_a) {
      result_a = space_balloon_communication_format_decode(&decoder_a, out_a[i]);
    }
    if (i < n_b) {
      result_b = space_balloon_communication_format_decode(&decoder_b, out_b[i]);
    }
  }
  CHECK(result_a == COMPLETED);
  CHECK(result_b == COMPLETED);
  CHECK(decoder_a.packet.source_id == 0x10);
  CHECK(decoder_b.packet.source_id == 0x20);
  CHECK(memcmp(decoder_a.packet.payload, payload_a, sizeof(payload_a)) == 0);
  CHECK(memcmp(decoder_b.packet.payload, payload_b, sizeof(payload_b)) == 0);
}

/** ヘッダも CRC の対象なので，宛先が化けても検出できること */
static void test_detects_corrupted_header(void) {
  uint8_t frame[sizeof(KNOWN_FRAME)];
  decoder_t decoder;

  memcpy(frame, KNOWN_FRAME, sizeof(frame));
  frame[1] = 0x11;
  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, frame, sizeof(frame)) ==
        SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_CHECKSUM);
}

static void test_detects_corrupted_payload(void) {
  uint8_t frame[sizeof(KNOWN_FRAME)];
  decoder_t decoder;

  memcpy(frame, KNOWN_FRAME, sizeof(frame));
  frame[5] = 0x03;
  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, frame, sizeof(frame)) ==
        SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_CHECKSUM);
}

static void test_rejects_early_footer(void) {
  const uint8_t frame[] = {0x7E, 0x10, 0x01, 0x02, 0x01, 0x7F};
  decoder_t decoder;

  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, frame, sizeof(frame)) ==
        SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER);
}

static void test_rejects_missing_footer(void) {
  const uint8_t frame[] = {0x7E, 0x10, 0x01, 0x00, 0x00, 0x00, 0x55};
  decoder_t decoder;

  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, frame, sizeof(frame)) ==
        SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER);
}

static void test_rejects_invalid_stuffing(void) {
  const uint8_t frame[] = {0x7E, 0x10, 0x7D, 0x00};
  decoder_t decoder;

  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(decode_all(&decoder, frame, sizeof(frame)) ==
        SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_STUFFING);
}

static void test_rejects_byte_before_start_marker(void) {
  decoder_t decoder;

  space_balloon_communication_format_decoder_init(&decoder);
  CHECK(space_balloon_communication_format_decode(&decoder, 0x10) ==
        SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_HEADER);
}

/** 途中で切れたフレームのあとに来た正しいフレームを読めること */
static void test_resyncs_on_start_marker(void) {
  const uint8_t broken[] = {0x7E, 0x10, 0x01, 0x05, 0xAA};
  decoder_t decoder;

  space_balloon_communication_format_decoder_init(&decoder);
  decode_all(&decoder, broken, sizeof(broken));
  CHECK(decode_all(&decoder, KNOWN_FRAME, sizeof(KNOWN_FRAME)) == COMPLETED);
  CHECK(decoder.packet.payload_length == 2);
}

/** エラーのあとも，そのまま次のフレームを読めること */
static void test_recovers_after_error(void) {
  const uint8_t bad[] = {0x7E, 0x10, 0x7D, 0x00};
  decoder_t decoder;

  space_balloon_communication_format_decoder_init(&decoder);
  decode_all(&decoder, bad, sizeof(bad));
  CHECK(decode_all(&decoder, KNOWN_FRAME, sizeof(KNOWN_FRAME)) == COMPLETED);
}

#define RUN(test)                                                              \
  do {                                                                         \
    printf("%s\n", #test);                                                     \
    test();                                                                    \
  } while (0)

int main(void) {
  RUN(test_crc_matches_ccitt_false_check_value);
  RUN(test_encode_matches_rust_implementation);
  RUN(test_encode_returns_zero_when_output_is_too_small);
  RUN(test_round_trip_with_stuffed_bytes);
  RUN(test_round_trip_empty_payload);
  RUN(test_worst_case_fits_max_frame_size);
  RUN(test_two_decoders_do_not_interfere);
  RUN(test_detects_corrupted_header);
  RUN(test_detects_corrupted_payload);
  RUN(test_rejects_early_footer);
  RUN(test_rejects_missing_footer);
  RUN(test_rejects_invalid_stuffing);
  RUN(test_rejects_byte_before_start_marker);
  RUN(test_resyncs_on_start_marker);
  RUN(test_recovers_after_error);

  if (failures > 0) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
