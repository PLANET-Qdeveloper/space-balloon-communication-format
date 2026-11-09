/*
 * 送る側でフレームを作り，受ける側で受信したバイト列から 1 バイトずつ読んで値を取り出す．
 *
 * ペイロードには spec/downlink.yaml の気圧（0x01）と GPS（0x10）を TLV で入れる．
 *
 * 実行:
 *   cmake --preset default && cmake --build --preset default
 *   ./build/default/examples/example_frame
 */
#include "space_balloon_communication_format/space_balloon_communication_format.h"
#include "space_balloon_communication_format/tlv.h"
#include <stdio.h>
#include <string.h>

#define GROUND_STATION_ID 0x01
#define MAIN1_ID 0x10

#define TAG_PRESSURE 0x01
#define TAG_GPS_POSITION 0x10

static void print_bytes(const char *label, const uint8_t *bytes, size_t length) {
  printf("%s (%zu bytes):", label, length);
  for (size_t i = 0; i < length; i++) {
    printf(" %02X", bytes[i]);
  }
  printf("\n");
}

static const char *result_name(space_balloon_communication_format_decode_result_t result) {
  switch (result) {
  case SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID:
    return "valid";
  case SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED:
    return "completed";
  case SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_HEADER:
    return "invalid header";
  case SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER:
    return "invalid footer";
  case SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_CHECKSUM:
    return "invalid checksum";
  case SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_STUFFING:
    return "invalid stuffing";
  }
  return "unknown";
}

/** ペイロードの TLV を読んで，知っているタグの値を表示する（scale は downlink.yaml の値） */
static void print_payload(const space_balloon_communication_format_t *packet) {
  space_balloon_communication_format_tlv_reader_t reader;
  space_balloon_communication_format_tlv_t tlv;
  space_balloon_communication_format_tlv_result_t result;

  space_balloon_communication_format_tlv_reader_init(&reader, packet->payload, packet->payload_length);
  while ((result = space_balloon_communication_format_tlv_next(&reader, &tlv)) == SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_FOUND) {
    if (tlv.tag == TAG_PRESSURE) {
      uint32_t pressure;
      if (space_balloon_communication_format_tlv_read_u32(&tlv, &pressure)) {
        printf("    pressure  = %.3f Pa\n", pressure * 0.001);
      }
    } else if (tlv.tag == TAG_GPS_POSITION) {
      int32_t latitude, longitude, altitude;
      uint8_t fix_type;
      if (space_balloon_communication_format_tlv_read_i32(&tlv, &latitude) && space_balloon_communication_format_tlv_read_i32(&tlv, &longitude) &&
          space_balloon_communication_format_tlv_read_i32(&tlv, &altitude) && space_balloon_communication_format_tlv_read_u8(&tlv, &fix_type)) {
        printf("    latitude  = %.7f deg\n", latitude * 1e-7);
        printf("    longitude = %.7f deg\n", longitude * 1e-7);
        printf("    altitude  = %.3f m\n", altitude * 0.001);
        printf("    fix_type  = %u\n", fix_type);
      }
    } else {
      /* 知らないタグは何もしない（次の next で飛ばされる） */
      printf("    [0x%02X] unknown tag, %u bytes skipped\n", tlv.tag, tlv.length);
    }
  }
  if (result == SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_TRUNCATED) {
    printf("    payload is truncated\n");
  }
}

int main(void) {
  /* ---- 送る側（機体）---- */
  space_balloon_communication_format_t packet;
  space_balloon_communication_format_clear(&packet);
  packet.destination_id = GROUND_STATION_ID;
  packet.source_id = MAIN1_ID;

  space_balloon_communication_format_tlv_writer_t payload;
  space_balloon_communication_format_tlv_writer_init(&payload, packet.payload, sizeof(packet.payload));
  /* 気圧: u32, scale 0.001 */
  space_balloon_communication_format_tlv_put_u32(&payload, TAG_PRESSURE, 101325);
  /* 受ける側が知らないタグ（新しいセンサーを足した場合など） */
  space_balloon_communication_format_tlv_put_i16(&payload, 0x7A, -1);
  /* GPS: 緯度・経度（i32, scale 1e-7），高度（i32, scale 0.001），fix_type（u8） */
  space_balloon_communication_format_tlv_begin(&payload, TAG_GPS_POSITION);
  space_balloon_communication_format_tlv_add_i32(&payload, 335902000);
  space_balloon_communication_format_tlv_add_i32(&payload, 1304017000);
  space_balloon_communication_format_tlv_add_i32(&payload, 30500000);
  space_balloon_communication_format_tlv_add_u8(&payload, 3);
  space_balloon_communication_format_tlv_end(&payload);
  /* 途中で失敗していないかは最後に 1 回だけ確かめればよい */
  size_t payload_length = space_balloon_communication_format_tlv_writer_finish(&payload);
  if (payload_length == 0) {
    printf("payload is too long\n");
    return 1;
  }
  packet.payload_length = (uint8_t)payload_length;

  uint8_t frame[SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_FRAME_SIZE];
  size_t frame_length = space_balloon_communication_format_encode(&packet, frame, sizeof(frame));
  if (frame_length == 0) {
    printf("encode failed\n");
    return 1;
  }
  print_bytes("frame", frame, frame_length);

  /*
   * ---- 受ける側（地上局）----
   * 実際の受信を真似て，前にゴミ，途中で切れたフレーム，CRC の壊れたフレームを混ぜる．
   */
  uint8_t received[3 + 4 + 2 * SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_FRAME_SIZE];
  size_t n = 0;
  const uint8_t noise[] = {0x00, 0x55, 0xAA};
  memcpy(received + n, noise, sizeof(noise)); /* 受信を始めたときのゴミ */
  n += sizeof(noise);
  memcpy(received + n, frame, 4); /* 途中で切れたフレーム */
  n += 4;
  memcpy(received + n, frame, frame_length); /* CRC の壊れたフレーム */
  received[n + 5] ^= 0xFF;
  n += frame_length;
  memcpy(received + n, frame, frame_length); /* 正しいフレーム */
  n += frame_length;

  /* デコーダは受信するストリーム（UART など）ごとに 1 つ用意する */
  space_balloon_communication_format_decoder_t decoder;
  space_balloon_communication_format_decoder_init(&decoder);

  int completed = 0;
  for (size_t i = 0; i < n; i++) {
    space_balloon_communication_format_decode_result_t result = space_balloon_communication_format_decode(&decoder, received[i]);
    if (result == SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID) {
      continue;
    }
    if (result != SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED) {
      /* エラーが出ても，そのままバイトを渡し続ければ次のフレームから読み直す */
      printf("byte %2zu: %s\n", i, result_name(result));
      continue;
    }

    printf("byte %2zu: completed, from 0x%02X to 0x%02X\n", i, decoder.packet.source_id, decoder.packet.destination_id);
    print_payload(&decoder.packet);
    completed++;
  }

  return completed == 1 ? 0 : 1;
}
