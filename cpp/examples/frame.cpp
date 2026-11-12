// 送る側でフレームを作り，受ける側で受信したバイト列から 1 バイトずつ読んで値を取り出す（C++ 版）．
//
// 地上局に受信機が 2 台（UART1・UART2）つながっていて，バイトが交互に届く場面を真似る．
// Decoder を受信機ごとに 1 つ作れば，読みかけの状態が混ざらない．
//
// ペイロードには spec/downlink.yaml の気圧（0x01）と GPS（0x10）を TLV で入れる．
//
// 実行:
//   cmake -S cpp -B cpp/build && cmake --build cpp/build
//   ./cpp/build/examples/example_frame_cpp
#include "space_balloon_communication_format/space_balloon_communication_format.hpp"
#include "space_balloon_communication_format/tlv.hpp"

#include <cstdio>

namespace sbcf = space_balloon_communication_format;

static const uint8_t GROUND_STATION_ID = 0x01;
static const uint8_t MAIN1_ID = 0x10;

static const uint8_t TAG_PRESSURE = 0x01;
static const uint8_t TAG_GPS_POSITION = 0x10;

static void print_bytes(const char *label, const uint8_t *bytes, size_t length) {
  std::printf("%s (%zu bytes):", label, length);
  for (size_t i = 0; i < length; i++) {
    std::printf(" %02X", bytes[i]);
  }
  std::printf("\n");
}

/** ペイロードの TLV を読んで，知っているタグの値を表示する（scale は downlink.yaml の値） */
static void print_payload(const sbcf::SpaceBalloonCommunicationFormat &frame) {
  sbcf::TlvReader reader(frame);
  sbcf::Tlv tlv;
  while (reader.next(tlv) == sbcf::TlvReadResult::Found) {
    if (tlv.get_tag() == TAG_PRESSURE) {
      uint32_t pressure;
      if (tlv.read_u32(pressure)) {
        std::printf("    pressure  = %.3f Pa\n", pressure * 0.001);
      }
    } else if (tlv.get_tag() == TAG_GPS_POSITION) {
      int32_t latitude, longitude, altitude;
      uint8_t fix_type;
      if (tlv.read_i32(latitude) && tlv.read_i32(longitude) && tlv.read_i32(altitude) && tlv.read_u8(fix_type)) {
        std::printf("    latitude  = %.7f deg\n", latitude * 1e-7);
        std::printf("    longitude = %.7f deg\n", longitude * 1e-7);
        std::printf("    altitude  = %.3f m\n", altitude * 0.001);
        std::printf("    fix_type  = %u\n", fix_type);
      }
    }
    // 知らないタグは何もしない（次の next で飛ばされる）
  }
}

/** 受信機 1 台分．読み終えたフレームを表示する */
struct Receiver {
  const char *name;
  sbcf::Decoder decoder;
  int completed;

  void receive(uint8_t byte) {
    sbcf::DecodeResult result = decoder.push(byte);
    if (result == sbcf::DecodeResult::Completed) {
      const sbcf::SpaceBalloonCommunicationFormat &frame = decoder.get_frame();
      std::printf("%s: from 0x%02X to 0x%02X\n", name, frame.get_source_id(), frame.get_destination_id());
      print_payload(frame);
      completed++;
    } else if (result != sbcf::DecodeResult::Valid) {
      // エラーが出ても，そのまま渡し続ければ次のフレームから読み直す
      std::printf("%s: error %d\n", name, static_cast<int>(result));
    }
  }
};

/** ペイロードを入れてフレームにする．書いたバイト数を返す（失敗したら 0） */
static size_t encode(const sbcf::TlvWriter &payload, uint8_t (&out)[sbcf::MAX_FRAME_LEN]) {
  if (!payload.is_ok()) {
    return 0;
  }
  sbcf::SpaceBalloonCommunicationFormat frame;
  frame.set_destination_id(GROUND_STATION_ID);
  frame.set_source_id(MAIN1_ID);
  frame.set_payload(payload.get_data(), payload.get_length());
  return frame.encode(out);
}

int main() {
  // ---- 送る側（機体）：気圧のフレームと GPS のフレームを作る ----
  sbcf::TlvWriter pressure;
  pressure.put_u32(TAG_PRESSURE, 101325); // u32, scale 0.001

  sbcf::TlvWriter gps;
  gps.begin(TAG_GPS_POSITION);
  gps.add_i32(335902000);  // 緯度（scale 1e-7）
  gps.add_i32(1304017000); // 経度（scale 1e-7）
  gps.add_i32(30500000);   // 高度（scale 0.001）
  gps.add_u8(3);           // 3D fix
  gps.end();

  uint8_t frame1[sbcf::MAX_FRAME_LEN];
  uint8_t frame2[sbcf::MAX_FRAME_LEN];
  size_t length1 = encode(pressure, frame1);
  size_t length2 = encode(gps, frame2);
  if (length1 == 0 || length2 == 0) {
    std::printf("encode failed\n");
    return 1;
  }
  print_bytes("frame 1", frame1, length1);
  print_bytes("frame 2", frame2, length2);

  // ---- 受ける側（地上局）：2 台の受信機にバイトが交互に届く ----
  Receiver uart1 = {"uart1", sbcf::Decoder(), 0};
  Receiver uart2 = {"uart2", sbcf::Decoder(), 0};
  for (size_t i = 0; i < length1 || i < length2; i++) {
    if (i < length1) {
      uart1.receive(frame1[i]);
    }
    if (i < length2) {
      uart2.receive(frame2[i]);
    }
  }

  return (uart1.completed == 1 && uart2.completed == 1) ? 0 : 1;
}
