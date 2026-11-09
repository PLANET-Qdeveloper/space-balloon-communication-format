#include "space_balloon_communication_format/tlv.hpp"

#include <cstdio>
#include <cstring>

namespace sbcf = space_balloon_communication_format;

// assert は Release（NDEBUG）で消えるので，自前の確認マクロを使う
static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      failures++;                                                              \
    }                                                                          \
  } while (0)

// Rust 版（rust/src/tests/mod.rs）と同じバイト列
static const uint8_t PRESSURE_TLV[] = {0x01, 0x04, 0xCD, 0x8B, 0x01, 0x00};
static const uint8_t GPS_TLV[] = {0x10, 0x0D, 0x0A, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x03};

static void test_writer_matches_rust_implementation() {
  sbcf::TlvWriter w;
  CHECK(w.put_u32(0x01, 101325));
  CHECK(w.begin(0x10));
  CHECK(w.add_i32(10) && w.add_i32(20) && w.add_i32(30) && w.add_u8(3));
  CHECK(w.end());
  CHECK(w.is_ok());
  CHECK(w.get_length() == sizeof(PRESSURE_TLV) + sizeof(GPS_TLV));
  CHECK(std::memcmp(w.get_data(), PRESSURE_TLV, sizeof(PRESSURE_TLV)) == 0);
  CHECK(std::memcmp(w.get_data() + sizeof(PRESSURE_TLV), GPS_TLV, sizeof(GPS_TLV)) == 0);
}

static void test_writer_reports_error_and_clear_resets() {
  sbcf::TlvWriter w;
  CHECK(!w.add_u8(1)); // begin していない
  CHECK(!w.is_ok());
  CHECK(w.get_length() == 0);
  CHECK(!w.put_u8(0x01, 1)); // 一度失敗したら以降も失敗

  w.clear();
  CHECK(w.put_u8(0x01, 1));
  CHECK(w.is_ok());
  CHECK(w.get_length() == 3);
}

static void test_writer_fills_whole_payload() {
  // ペイロード 255 = Tag + Length + Value 253
  uint8_t data[sbcf::MAX_VALUE_LEN] = {};
  sbcf::TlvWriter w;
  CHECK(w.begin(0x01) && w.add_bytes(data, sizeof(data)) && w.end());
  CHECK(w.get_length() == sbcf::MAX_PAYLOAD_LEN);
  CHECK(!w.put_u8(0x02, 1)); // もう入らない
}

// フレームに入れて，エンコード・デコードを通して読み戻す
static void test_round_trip_through_frame() {
  sbcf::TlvWriter payload;
  payload.put_u32(0x01, 101325);
  payload.put_i16(0x02, -2500); // 温度
  payload.begin(0x7A);          // 受ける側が知らないタグ
  payload.add_i16(-1);
  payload.end();
  payload.begin(0x10);
  payload.add_i32(335902000);
  payload.add_i32(1304017000);
  payload.add_i32(-1000);
  payload.add_u8(3);
  payload.end();
  CHECK(payload.is_ok());

  sbcf::SpaceBalloonCommunicationFormat frame;
  frame.set_destination_id(0x01);
  frame.set_source_id(0x10);
  CHECK(frame.set_payload(payload.get_data(), payload.get_length()));
  uint8_t bytes[sbcf::MAX_FRAME_LEN];
  size_t n = frame.encode(bytes);

  sbcf::Decoder decoder;
  sbcf::DecodeResult result = sbcf::DecodeResult::Valid;
  for (size_t i = 0; i < n; i++) {
    result = decoder.push(bytes[i]);
  }
  CHECK(result == sbcf::DecodeResult::Completed);

  uint32_t pressure = 0;
  int16_t temperature = 0;
  int32_t latitude = 0, longitude = 0, altitude = 0;
  uint8_t fix_type = 0;
  int count = 0;
  sbcf::TlvReader reader(decoder.get_frame());
  sbcf::Tlv tlv;
  while (reader.next(tlv) == sbcf::TlvReadResult::Found) {
    count++;
    switch (tlv.get_tag()) {
    case 0x01:
      CHECK(tlv.read_u32(pressure));
      break;
    case 0x02:
      CHECK(tlv.read_i16(temperature));
      break;
    case 0x10:
      CHECK(tlv.get_length() == 13);
      CHECK(tlv.read_i32(latitude) && tlv.read_i32(longitude) && tlv.read_i32(altitude) && tlv.read_u8(fix_type));
      break;
    default:
      break; // 知らないタグは飛ばす
    }
  }
  CHECK(count == 4);
  CHECK(pressure == 101325);
  CHECK(temperature == -2500);
  CHECK(latitude == 335902000);
  CHECK(longitude == 1304017000);
  CHECK(altitude == -1000);
  CHECK(fix_type == 3);
}

static void test_reader_reports_truncated() {
  const uint8_t broken[] = {0x01, 0x04, 0xCD, 0x8B};
  sbcf::TlvReader reader(broken, sizeof(broken));
  sbcf::Tlv tlv;
  CHECK(reader.next(tlv) == sbcf::TlvReadResult::Truncated);
  CHECK(reader.next(tlv) == sbcf::TlvReadResult::End);
}

static void test_read_fails_when_value_is_short() {
  const uint8_t short_value[] = {0x01, 0x02, 0xAA, 0xBB};
  sbcf::TlvReader reader(short_value, sizeof(short_value));
  sbcf::Tlv tlv;
  uint32_t value = 7;
  CHECK(reader.next(tlv) == sbcf::TlvReadResult::Found);
  CHECK(!tlv.read_u32(value));
  CHECK(value == 7); // 失敗したら変えない
}

#define RUN(test)                                                              \
  do {                                                                         \
    std::printf("%s\n", #test);                                                \
    test();                                                                    \
  } while (0)

int main() {
  RUN(test_writer_matches_rust_implementation);
  RUN(test_writer_reports_error_and_clear_resets);
  RUN(test_writer_fills_whole_payload);
  RUN(test_round_trip_through_frame);
  RUN(test_reader_reports_truncated);
  RUN(test_read_fails_when_value_is_short);

  if (failures > 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
