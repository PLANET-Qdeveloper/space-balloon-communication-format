#include "space_balloon_communication_format/tlv.h"
#include <string.h>

typedef space_balloon_communication_format_tlv_writer_t writer_t;
typedef space_balloon_communication_format_tlv_t tlv_t;

// ---------------------------------------------------------------------------
// 書く
// ---------------------------------------------------------------------------

/** 失敗にして，以降の書き込みもすべて失敗させる */
static int fail(writer_t *writer) {
  writer->has_error = 1;
  return 0;
}

/** buffer の後ろにそのまま書く */
static int append(writer_t *writer, const uint8_t *data, size_t length) {
  if (writer->has_error) {
    return 0;
  }
  if (length > writer->capacity - writer->length) {
    return fail(writer);
  }
  if (length > 0) {
    memcpy(writer->buffer + writer->length, data, length);
  }
  writer->length += length;
  return 1;
}

/** begin したタグに値を足す */
static int add(writer_t *writer, const uint8_t *data, size_t length) {
  if (writer->has_error) {
    return 0;
  }
  if (!writer->is_open) {
    return fail(writer); /* begin していない */
  }
  size_t value_length = writer->length - (writer->length_position + 1);
  if (length > SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_MAX_VALUE_SIZE - value_length) {
    return fail(writer); /* Length（1 バイト）に入らない */
  }
  return append(writer, data, length);
}

void space_balloon_communication_format_tlv_writer_init(writer_t *writer, uint8_t *buffer, size_t capacity) {
  memset(writer, 0, sizeof(*writer));
  writer->buffer = buffer;
  writer->capacity = capacity;
}

int space_balloon_communication_format_tlv_begin(writer_t *writer, uint8_t tag) {
  if (writer->has_error) {
    return 0;
  }
  if (writer->is_open) {
    return fail(writer); /* 前のタグを end していない */
  }
  const uint8_t header[2] = {tag, 0}; /* Length は end で入れる */
  if (!append(writer, header, sizeof(header))) {
    return 0;
  }
  writer->length_position = writer->length - 1;
  writer->is_open = 1;
  return 1;
}

int space_balloon_communication_format_tlv_add_u8(writer_t *writer, uint8_t value) {
  return add(writer, &value, 1);
}

int space_balloon_communication_format_tlv_add_i16(writer_t *writer, int16_t value) {
  uint16_t bits = (uint16_t)value;
  const uint8_t bytes[2] = {(uint8_t)bits, (uint8_t)(bits >> 8)};
  return add(writer, bytes, sizeof(bytes));
}

int space_balloon_communication_format_tlv_add_u32(writer_t *writer, uint32_t value) {
  const uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
  return add(writer, bytes, sizeof(bytes));
}

int space_balloon_communication_format_tlv_add_i32(writer_t *writer, int32_t value) {
  return space_balloon_communication_format_tlv_add_u32(writer, (uint32_t)value);
}

int space_balloon_communication_format_tlv_add_bytes(writer_t *writer, const uint8_t *data, size_t length) {
  return add(writer, data, length);
}

int space_balloon_communication_format_tlv_end(writer_t *writer) {
  if (writer->has_error) {
    return 0;
  }
  if (!writer->is_open) {
    return fail(writer); /* begin していない */
  }
  size_t value_length = writer->length - (writer->length_position + 1);
  writer->buffer[writer->length_position] = (uint8_t)value_length;
  writer->is_open = 0;
  return 1;
}

int space_balloon_communication_format_tlv_put_u8(writer_t *writer, uint8_t tag, uint8_t value) {
  return space_balloon_communication_format_tlv_begin(writer, tag) && space_balloon_communication_format_tlv_add_u8(writer, value) && space_balloon_communication_format_tlv_end(writer);
}

int space_balloon_communication_format_tlv_put_i16(writer_t *writer, uint8_t tag, int16_t value) {
  return space_balloon_communication_format_tlv_begin(writer, tag) && space_balloon_communication_format_tlv_add_i16(writer, value) && space_balloon_communication_format_tlv_end(writer);
}

int space_balloon_communication_format_tlv_put_u32(writer_t *writer, uint8_t tag, uint32_t value) {
  return space_balloon_communication_format_tlv_begin(writer, tag) && space_balloon_communication_format_tlv_add_u32(writer, value) && space_balloon_communication_format_tlv_end(writer);
}

int space_balloon_communication_format_tlv_put_i32(writer_t *writer, uint8_t tag, int32_t value) {
  return space_balloon_communication_format_tlv_begin(writer, tag) && space_balloon_communication_format_tlv_add_i32(writer, value) && space_balloon_communication_format_tlv_end(writer);
}

size_t space_balloon_communication_format_tlv_writer_finish(const writer_t *writer) {
  if (writer->has_error || writer->is_open) {
    return 0;
  }
  return writer->length;
}

// ---------------------------------------------------------------------------
// 読む
// ---------------------------------------------------------------------------

void space_balloon_communication_format_tlv_reader_init(space_balloon_communication_format_tlv_reader_t *reader, const uint8_t *data, size_t length) {
  reader->data = data;
  reader->length = length;
  reader->offset = 0;
}

space_balloon_communication_format_tlv_result_t space_balloon_communication_format_tlv_next(space_balloon_communication_format_tlv_reader_t *reader, tlv_t *tlv) {
  size_t rest = reader->length - reader->offset;
  if (rest == 0) {
    return SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_END;
  }
  const uint8_t *p = reader->data + reader->offset;
  if (rest < 2 || rest - 2 < p[1]) {
    reader->offset = reader->length; /* 壊れているので，これ以上は読まない */
    return SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_TRUNCATED;
  }
  tlv->tag = p[0];
  tlv->length = p[1];
  tlv->value = p + 2;
  tlv->read_offset = 0;
  reader->offset += 2 + (size_t)p[1];
  return SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_FOUND;
}

/** Value から size バイト取り出す．足りなければ NULL */
static const uint8_t *take(tlv_t *tlv, uint8_t size) {
  if (tlv->length - tlv->read_offset < size) {
    return NULL;
  }
  const uint8_t *p = tlv->value + tlv->read_offset;
  tlv->read_offset = (uint8_t)(tlv->read_offset + size);
  return p;
}

static uint32_t u32_from_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int space_balloon_communication_format_tlv_read_u8(tlv_t *tlv, uint8_t *out) {
  const uint8_t *p = take(tlv, 1);
  if (p == NULL) {
    return 0;
  }
  *out = p[0];
  return 1;
}

int space_balloon_communication_format_tlv_read_i16(tlv_t *tlv, int16_t *out) {
  const uint8_t *p = take(tlv, 2);
  if (p == NULL) {
    return 0;
  }
  uint16_t bits = (uint16_t)(p[0] | (p[1] << 8));
  /* 符号付きへの変換を処理系に任せない（2 の補数として読む） */
  *out = (bits < 0x8000) ? (int16_t)bits : (int16_t)((int32_t)bits - 0x10000);
  return 1;
}

int space_balloon_communication_format_tlv_read_u32(tlv_t *tlv, uint32_t *out) {
  const uint8_t *p = take(tlv, 4);
  if (p == NULL) {
    return 0;
  }
  *out = u32_from_le(p);
  return 1;
}

int space_balloon_communication_format_tlv_read_i32(tlv_t *tlv, int32_t *out) {
  const uint8_t *p = take(tlv, 4);
  if (p == NULL) {
    return 0;
  }
  uint32_t bits = u32_from_le(p);
  /* 符号付きへの変換を処理系に任せない（2 の補数として読む） */
  *out = (bits < 0x80000000u) ? (int32_t)bits : -(int32_t)(~bits) - 1;
  return 1;
}
