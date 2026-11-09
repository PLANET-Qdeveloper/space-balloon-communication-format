#include "space_balloon_communication_format/space_balloon_communication_format.hpp"

#include <cstdio>
#include <cstring>

using space_balloon_communication_format::DecodeResult;
using space_balloon_communication_format::Decoder;
using space_balloon_communication_format::MAX_FRAME_LEN;
using space_balloon_communication_format::MAX_PAYLOAD_LEN;
using space_balloon_communication_format::SpaceBalloonCommunicationFormat;

// assert は Release（NDEBUG）で消えるので，自前の確認マクロを使う
static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      failures++;                                                              \
    }                                                                          \
  } while (0)

// Rust 版（rust/src/tests/format.rs）と同じフレーム：宛先 0x10，送信元 0x01，ペイロード 01 02
static const uint8_t KNOWN_FRAME[] = {0x7E, 0x10, 0x01, 0x02, 0x01, 0x02, 0x1E, 0xF1, 0x7F};

static SpaceBalloonCommunicationFormat make_frame(uint8_t destination, uint8_t source,
                                                  const uint8_t *payload, size_t length) {
  SpaceBalloonCommunicationFormat frame;
  frame.set_destination_id(destination);
  frame.set_source_id(source);
  frame.set_payload(payload, length);
  return frame;
}

static DecodeResult push_all(Decoder &decoder, const uint8_t *bytes, size_t length) {
  DecodeResult result = DecodeResult::Valid;
  for (size_t i = 0; i < length; i++) {
    result = decoder.push(bytes[i]);
  }
  return result;
}

static void test_getters_and_setters() {
  const uint8_t payload[] = {0x01, 0x02, 0x03};
  SpaceBalloonCommunicationFormat frame = make_frame(0x10, 0x01, payload, sizeof(payload));
  CHECK(frame.get_destination_id() == 0x10);
  CHECK(frame.get_source_id() == 0x01);
  CHECK(frame.get_payload_length() == 3);
  CHECK(std::memcmp(frame.get_payload(), payload, sizeof(payload)) == 0);
}

static void test_set_payload_rejects_too_long() {
  uint8_t payload[MAX_PAYLOAD_LEN + 1] = {};
  SpaceBalloonCommunicationFormat frame;
  CHECK(frame.set_payload(payload, MAX_PAYLOAD_LEN));
  CHECK(!frame.set_payload(payload, MAX_PAYLOAD_LEN + 1));
  CHECK(frame.get_payload_length() == MAX_PAYLOAD_LEN); // 失敗したら変わらない
}

static void test_encode_matches_rust_implementation() {
  const uint8_t payload[] = {0x01, 0x02};
  uint8_t out[MAX_FRAME_LEN];
  size_t n = make_frame(0x10, 0x01, payload, sizeof(payload)).encode(out);
  CHECK(n == sizeof(KNOWN_FRAME));
  CHECK(std::memcmp(out, KNOWN_FRAME, sizeof(KNOWN_FRAME)) == 0);
}

static void test_round_trip() {
  const uint8_t payload[] = {0x7E, 0x7F, 0x7D, 0x00, 0xFF};
  uint8_t out[MAX_FRAME_LEN];
  size_t n = make_frame(0x7E, 0x7F, payload, sizeof(payload)).encode(out);

  Decoder decoder;
  CHECK(push_all(decoder, out, n) == DecodeResult::Completed);
  const SpaceBalloonCommunicationFormat &frame = decoder.get_frame();
  CHECK(frame.get_destination_id() == 0x7E);
  CHECK(frame.get_source_id() == 0x7F);
  CHECK(frame.get_payload_length() == sizeof(payload));
  CHECK(std::memcmp(frame.get_payload(), payload, sizeof(payload)) == 0);
}

// Decoder を 2 つ作れば，2 つのストリームを交互に読んでも混ざらないこと
static void test_two_decoders_do_not_interfere() {
  const uint8_t payload_a[] = {0xAA, 0xAB};
  const uint8_t payload_b[] = {0xBB, 0xBC, 0xBD};
  uint8_t out_a[MAX_FRAME_LEN];
  uint8_t out_b[MAX_FRAME_LEN];
  size_t n_a = make_frame(0x01, 0x10, payload_a, sizeof(payload_a)).encode(out_a);
  size_t n_b = make_frame(0x01, 0x20, payload_b, sizeof(payload_b)).encode(out_b);

  Decoder uart1;
  Decoder uart2;
  DecodeResult result_a = DecodeResult::Valid;
  DecodeResult result_b = DecodeResult::Valid;
  for (size_t i = 0; i < n_a || i < n_b; i++) {
    if (i < n_a) {
      result_a = uart1.push(out_a[i]);
    }
    if (i < n_b) {
      result_b = uart2.push(out_b[i]);
    }
  }
  CHECK(result_a == DecodeResult::Completed);
  CHECK(result_b == DecodeResult::Completed);
  CHECK(uart1.get_frame().get_source_id() == 0x10);
  CHECK(uart2.get_frame().get_source_id() == 0x20);
  CHECK(std::memcmp(uart1.get_frame().get_payload(), payload_a, sizeof(payload_a)) == 0);
  CHECK(std::memcmp(uart2.get_frame().get_payload(), payload_b, sizeof(payload_b)) == 0);
}

static void test_reports_checksum_error() {
  uint8_t frame[sizeof(KNOWN_FRAME)];
  std::memcpy(frame, KNOWN_FRAME, sizeof(frame));
  frame[1] = 0x11;

  Decoder decoder;
  CHECK(push_all(decoder, frame, sizeof(frame)) == DecodeResult::InvalidChecksum);
}

// 読み終えたフレームは，次のフレームを読み終えるまで変わらないこと
static void test_frame_is_kept_until_next_completed() {
  Decoder decoder;
  push_all(decoder, KNOWN_FRAME, sizeof(KNOWN_FRAME));
  const uint8_t partial[] = {0x7E, 0x20, 0x02};
  push_all(decoder, partial, sizeof(partial));
  CHECK(decoder.get_frame().get_destination_id() == 0x10);
}

#define RUN(test)                                                              \
  do {                                                                         \
    std::printf("%s\n", #test);                                                \
    test();                                                                    \
  } while (0)

int main() {
  RUN(test_getters_and_setters);
  RUN(test_set_payload_rejects_too_long);
  RUN(test_encode_matches_rust_implementation);
  RUN(test_round_trip);
  RUN(test_two_decoders_do_not_interfere);
  RUN(test_reports_checksum_error);
  RUN(test_frame_is_kept_until_next_completed);

  if (failures > 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
