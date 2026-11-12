#include "space_balloon_communication_format/space_balloon_communication_format.h"
#include <string.h>

/** フレームの中で今どこを読んでいるか */
typedef enum {
  DECODE_STATE_WAIT_START,
  DECODE_STATE_DESTINATION_ID,
  DECODE_STATE_SOURCE_ID,
  DECODE_STATE_PAYLOAD_LENGTH,
  DECODE_STATE_PAYLOAD,
  DECODE_STATE_CRC_1,
  DECODE_STATE_CRC_2,
  DECODE_STATE_END,
} decode_state_t;

void space_balloon_communication_format_clear(space_balloon_communication_format_t *packet) {
  memset(packet, 0, sizeof(*packet));
}

// ---------------------------------------------------------------------------
// CRC
// ---------------------------------------------------------------------------

/**
 * @brief CRC16 の計算を途中から続ける（CRC-16/CCITT-FALSE）
 * @param crc ここまでの CRC（最初は 0xFFFF）
 */
static uint16_t crc_update(uint16_t crc, const uint8_t *data, uint16_t length) {
  for (uint16_t i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

uint16_t space_balloon_communication_format_calculate_crc_checksum(const uint8_t *data,
                                                                   uint16_t length) {
  return crc_update(0xFFFF, data, length);
}

/** フレームの CRC．ヘッダ（宛先ID・送信元ID・ペイロード長）とペイロードが対象． */
static uint16_t frame_crc(const space_balloon_communication_format_t *packet) {
  const uint8_t header[3] = {packet->destination_id, packet->source_id, packet->payload_length};
  uint16_t crc = crc_update(0xFFFF, header, sizeof(header));
  return crc_update(crc, packet->payload, packet->payload_length);
}

// ---------------------------------------------------------------------------
// エンコード
// ---------------------------------------------------------------------------

/** output に書いていく途中の状態 */
typedef struct {
  uint8_t *output;
  size_t capacity;
  size_t length;
  int overflow;
} writer_t;

static void put_raw(writer_t *writer, uint8_t byte) {
  if (writer->length >= writer->capacity) {
    writer->overflow = 1;
    return;
  }
  writer->output[writer->length++] = byte;
}

/** 区切りと同じバイトは 0x7D + 別のバイトに置き換えて書く */
static void put_stuffed(writer_t *writer, uint8_t byte) {
  switch (byte) {
  case SPACE_BALLOON_COMMUNICATION_FORMAT_START_MARKER:
    put_raw(writer, SPACE_BALLOON_COMMUNICATION_FORMAT_ESCAPE);
    put_raw(writer, 0x81);
    break;
  case SPACE_BALLOON_COMMUNICATION_FORMAT_END_MARKER:
    put_raw(writer, SPACE_BALLOON_COMMUNICATION_FORMAT_ESCAPE);
    put_raw(writer, 0x80);
    break;
  case SPACE_BALLOON_COMMUNICATION_FORMAT_ESCAPE:
    put_raw(writer, SPACE_BALLOON_COMMUNICATION_FORMAT_ESCAPE);
    put_raw(writer, 0x7D);
    break;
  default:
    put_raw(writer, byte);
    break;
  }
}

size_t space_balloon_communication_format_encode(const space_balloon_communication_format_t *packet,
                                                 uint8_t *output, size_t capacity) {
  writer_t writer = {output, capacity, 0, 0};
  uint16_t crc = frame_crc(packet);

  put_raw(&writer, SPACE_BALLOON_COMMUNICATION_FORMAT_START_MARKER);
  put_stuffed(&writer, packet->destination_id);
  put_stuffed(&writer, packet->source_id);
  put_stuffed(&writer, packet->payload_length);
  for (uint16_t i = 0; i < packet->payload_length; i++) {
    put_stuffed(&writer, packet->payload[i]);
  }
  put_stuffed(&writer, (uint8_t)(crc >> 8));
  put_stuffed(&writer, (uint8_t)(crc & 0xFF));
  put_raw(&writer, SPACE_BALLOON_COMMUNICATION_FORMAT_END_MARKER);

  return writer.overflow ? 0 : writer.length;
}

// ---------------------------------------------------------------------------
// デコード
// ---------------------------------------------------------------------------

void space_balloon_communication_format_decoder_init(
    space_balloon_communication_format_decoder_t *decoder) {
  memset(decoder, 0, sizeof(*decoder));
  decoder->state = DECODE_STATE_WAIT_START;
}

/** エラーを返して，次の開始マーカーを待つ */
static space_balloon_communication_format_decode_result_t
fail(space_balloon_communication_format_decoder_t *decoder,
     space_balloon_communication_format_decode_result_t error) {
  decoder->state = DECODE_STATE_WAIT_START;
  decoder->is_stuffed = 0;
  return error;
}

space_balloon_communication_format_decode_result_t space_balloon_communication_format_decode(
    space_balloon_communication_format_decoder_t *decoder, uint8_t input) {
  space_balloon_communication_format_t *packet = &decoder->packet;
  uint8_t byte = input;

  // スタッフィングしない 0x7E は必ず開始マーカーなので，フレームの途中でも最初から読み直す
  if (input == SPACE_BALLOON_COMMUNICATION_FORMAT_START_MARKER) {
    space_balloon_communication_format_clear(packet);
    decoder->state = DECODE_STATE_DESTINATION_ID;
    decoder->is_stuffed = 0;
    decoder->index = 0;
    return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
  }

  if (decoder->state == DECODE_STATE_WAIT_START) {
    return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_HEADER;
  }

  if (decoder->is_stuffed) {
    // 0x7D の次のバイトを元に戻す（戻した 0x7F はデータなので終了マーカーではない）
    decoder->is_stuffed = 0;
    switch (input) {
    case 0x81:
      byte = SPACE_BALLOON_COMMUNICATION_FORMAT_START_MARKER;
      break;
    case 0x80:
      byte = SPACE_BALLOON_COMMUNICATION_FORMAT_END_MARKER;
      break;
    case 0x7D:
      byte = SPACE_BALLOON_COMMUNICATION_FORMAT_ESCAPE;
      break;
    default:
      return fail(decoder, SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_STUFFING);
    }
  } else if (input == SPACE_BALLOON_COMMUNICATION_FORMAT_ESCAPE) {
    decoder->is_stuffed = 1;
    return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
  } else if (input == SPACE_BALLOON_COMMUNICATION_FORMAT_END_MARKER) {
    if (decoder->state != DECODE_STATE_END) {
      return fail(decoder, SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER);
    }
    decoder->state = DECODE_STATE_WAIT_START;
    if (frame_crc(packet) != decoder->crc_checksum) {
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_CHECKSUM;
    }
    return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED;
  }

  switch (decoder->state) {
  case DECODE_STATE_DESTINATION_ID:
    packet->destination_id = byte;
    decoder->state = DECODE_STATE_SOURCE_ID;
    break;
  case DECODE_STATE_SOURCE_ID:
    packet->source_id = byte;
    decoder->state = DECODE_STATE_PAYLOAD_LENGTH;
    break;
  case DECODE_STATE_PAYLOAD_LENGTH:
    packet->payload_length = byte;
    decoder->index = 0;
    decoder->state = (byte == 0) ? DECODE_STATE_CRC_1 : DECODE_STATE_PAYLOAD;
    break;
  case DECODE_STATE_PAYLOAD:
    packet->payload[decoder->index++] = byte;
    if (decoder->index >= packet->payload_length) {
      decoder->state = DECODE_STATE_CRC_1;
    }
    break;
  case DECODE_STATE_CRC_1:
    decoder->crc_checksum = (uint16_t)(byte << 8);
    decoder->state = DECODE_STATE_CRC_2;
    break;
  case DECODE_STATE_CRC_2:
    decoder->crc_checksum |= byte;
    decoder->state = DECODE_STATE_END;
    break;
  default:
    // CRC のあとに終了マーカー以外が来た
    return fail(decoder, SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER);
  }

  return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
}
