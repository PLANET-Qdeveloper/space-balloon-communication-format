#include "space_balloon_communication_format/space_balloon_communication_format.h"
#include <string.h>

void space_balloon_communication_format_clear(space_balloon_communication_format_t *packet) {
  memset(packet, 0, sizeof(space_balloon_communication_format_t));
}

uint16_t space_balloon_communication_format_calculate_crc_checksum(uint8_t *data, uint16_t length) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

/**
 * @brief デコードの状態
 */
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

space_balloon_communication_format_decode_result_t space_balloon_communication_format_decode(
    space_balloon_communication_format_t *packet, const uint8_t input) {
  static decode_state_t state = DECODE_STATE_WAIT_START;
  static uint8_t is_stuffed = 0;
  uint8_t current_byte = input;

  if (state == DECODE_STATE_WAIT_START) {
    if (input == SPACE_BALLOON_COMMUNICATION_FORMAT_START_MARKER) {
      space_balloon_communication_format_clear(packet);
      state = DECODE_STATE_DESTINATION_ID;
      is_stuffed = 0;
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
    }
    return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_HEADER;
  }

  if (is_stuffed) {
    is_stuffed = 0;
    switch (current_byte) {
    case 0x81:
      current_byte = 0x7E;
      break;
    case 0x80:
      current_byte = 0x7F;
      break;
    case 0x7D:
      current_byte = 0x7D;
      break;
    default:
      state = DECODE_STATE_WAIT_START;
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_UNDEFINED;
    }
  } else {
    if (current_byte == 0x7D) {
      is_stuffed = 1;
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
    }
  }

  if (current_byte == SPACE_BALLOON_COMMUNICATION_FORMAT_END_MARKER) {
    if (state != DECODE_STATE_END) {
      state = DECODE_STATE_WAIT_START;
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER;
    }
    uint16_t calculated_crc =
        space_balloon_communication_format_calculate_crc_checksum(packet->payload, packet->payload_length);
    state = DECODE_STATE_WAIT_START;
    if (calculated_crc == packet->crc_checksum) {
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED;
    } else {
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_CHECKSUM;
    }
  }

  switch (state) {
  case DECODE_STATE_DESTINATION_ID:
    packet->destination_id = current_byte;
    state = DECODE_STATE_SOURCE_ID;
    break;
  case DECODE_STATE_SOURCE_ID:
    packet->source_id = current_byte;
    state = DECODE_STATE_PAYLOAD_LENGTH;
    break;
  case DECODE_STATE_PAYLOAD_LENGTH:
    packet->payload_length = current_byte;
    if (packet->payload_length > SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_PAYLOAD_SIZE) {
      state = DECODE_STATE_WAIT_START;
      return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_UNDEFINED;
    }
    packet->index = 0;
    state = (packet->payload_length == 0) ? DECODE_STATE_CRC_1
                                          : DECODE_STATE_PAYLOAD;
    break;
  case DECODE_STATE_PAYLOAD:
    packet->payload[packet->index++] = current_byte;
    if (packet->index >= packet->payload_length) {
      state = DECODE_STATE_CRC_1;
    }
    break;
  case DECODE_STATE_CRC_1:
    packet->crc_checksum = (uint16_t)current_byte << 8;
    state = DECODE_STATE_CRC_2;
    break;
  case DECODE_STATE_CRC_2:
    packet->crc_checksum |= current_byte;
    state = DECODE_STATE_END;
    break;
  case DECODE_STATE_WAIT_START:
  case DECODE_STATE_END:
    // error
    state = DECODE_STATE_WAIT_START;
    return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_UNDEFINED;
  }

  return SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID;
}

space_balloon_communication_format_encode_result_t space_balloon_communication_format_encode(
    space_balloon_communication_format_t *packet, uint8_t *output) {
  static uint8_t stuffed_byte = 0;
  static uint8_t stuffing_active = 0;

  if (stuffing_active) {
    *output = stuffed_byte;
    stuffing_active = 0;
    return SPACE_BALLOON_COMMUNICATION_FORMAT_ENCODE_SUCCESS;
  }

  uint8_t byte_to_encode;
  uint16_t payload_len = packet->payload_length;
  uint16_t frame_len_without_markers = 3 + payload_len + 2;

  if (packet->index == 0) {
    *output = SPACE_BALLOON_COMMUNICATION_FORMAT_START_MARKER;
    packet->crc_checksum =
        space_balloon_communication_format_calculate_crc_checksum(packet->payload, payload_len);
    packet->index++;
    return SPACE_BALLOON_COMMUNICATION_FORMAT_ENCODE_SUCCESS;
  }

  if (packet->index > frame_len_without_markers) {
    *output = SPACE_BALLOON_COMMUNICATION_FORMAT_END_MARKER;
    packet->index = 0;
    return SPACE_BALLOON_COMMUNICATION_FORMAT_ENCODE_COMPLETED;
  }

  uint16_t current_pos = packet->index - 1;
  if (current_pos == 0) {
    byte_to_encode = packet->destination_id;
  } else if (current_pos == 1) {
    byte_to_encode = packet->source_id;
  } else if (current_pos == 2) {
    byte_to_encode = packet->payload_length;
  } else if (current_pos < 3 + payload_len) {
    byte_to_encode = packet->payload[current_pos - 3];
  } else if (current_pos == 3 + payload_len) {
    byte_to_encode = (uint8_t)(packet->crc_checksum >> 8);
  } else { // current_pos == 4 + payload_len
    byte_to_encode = (uint8_t)(packet->crc_checksum & 0xFF);
  }

  if (byte_to_encode == 0x7E || byte_to_encode == 0x7F ||
      byte_to_encode == 0x7D) {
    *output = 0x7D;
    stuffing_active = 1;
    if (byte_to_encode == 0x7E)
      stuffed_byte = 0x81;
    else if (byte_to_encode == 0x7F)
      stuffed_byte = 0x80;
    else // 0x7D
      stuffed_byte = 0x7D;
  } else {
    *output = byte_to_encode;
  }

  packet->index++;
  return SPACE_BALLOON_COMMUNICATION_FORMAT_ENCODE_SUCCESS;
}