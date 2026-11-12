#ifndef SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_H
#define SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_H

/*
 * ペイロードの TLV（Tag(1) + Length(1) + Value）を書く・読む．
 *
 * タグの定義（spec/downlink.yaml）は見ない．どのタグにどの型を入れるかは使う側で決める．
 * 値はリトルエンディアンで書く（Rust 版と同じ）．
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/** TLV 1 つの Value の最大長（ペイロード 255 から Tag と Length の 2 バイトを引いたもの） */
#define SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_MAX_VALUE_SIZE 253

// ---------------------------------------------------------------------------
// 書く
// ---------------------------------------------------------------------------

/**
 * @struct space_balloon_communication_format_tlv_writer_t
 * @brief TLV を書いていく途中の状態．メンバは内部用なので直接触らない．
 *
 * 一度失敗すると，それ以降の書き込みはすべて失敗する（最後に 1 回確かめればよい）．
 * 失敗したら，そのペイロードは作り直す．
 */
typedef struct {
  uint8_t *buffer;
  size_t capacity;
  size_t length;
  /** begin したタグの Length の位置 */
  size_t length_position;
  uint8_t is_open;
  uint8_t has_error;
} space_balloon_communication_format_tlv_writer_t;

/**
 * @brief 書き始める
 * @param buffer 書く先（ふつうは packet.payload）
 * @param capacity buffer の大きさ
 */
void space_balloon_communication_format_tlv_writer_init(space_balloon_communication_format_tlv_writer_t *writer, uint8_t *buffer, size_t capacity);

/** @brief タグを書き始める．このあと add_* で値を足して，end で閉じる． */
int space_balloon_communication_format_tlv_begin(space_balloon_communication_format_tlv_writer_t *writer, uint8_t tag);

/** @brief begin したタグに値を足す（リトルエンディアン） */
int space_balloon_communication_format_tlv_add_u8(space_balloon_communication_format_tlv_writer_t *writer, uint8_t value);
int space_balloon_communication_format_tlv_add_i16(space_balloon_communication_format_tlv_writer_t *writer, int16_t value);
int space_balloon_communication_format_tlv_add_u32(space_balloon_communication_format_tlv_writer_t *writer, uint32_t value);
int space_balloon_communication_format_tlv_add_i32(space_balloon_communication_format_tlv_writer_t *writer, int32_t value);
int space_balloon_communication_format_tlv_add_bytes(space_balloon_communication_format_tlv_writer_t *writer, const uint8_t *data, size_t length);

/** @brief タグを閉じる．Length はここで入る． */
int space_balloon_communication_format_tlv_end(space_balloon_communication_format_tlv_writer_t *writer);

/** @brief 値が 1 つだけのタグを書く（begin + add + end） */
int space_balloon_communication_format_tlv_put_u8(space_balloon_communication_format_tlv_writer_t *writer, uint8_t tag, uint8_t value);
int space_balloon_communication_format_tlv_put_i16(space_balloon_communication_format_tlv_writer_t *writer, uint8_t tag, int16_t value);
int space_balloon_communication_format_tlv_put_u32(space_balloon_communication_format_tlv_writer_t *writer, uint8_t tag, uint32_t value);
int space_balloon_communication_format_tlv_put_i32(space_balloon_communication_format_tlv_writer_t *writer, uint8_t tag, int32_t value);

/**
 * @brief 書き終えたバイト数を返す．
 * @return 書いたバイト数．途中で失敗していた，または閉じていないタグがあれば 0
 */
size_t space_balloon_communication_format_tlv_writer_finish(const space_balloon_communication_format_tlv_writer_t *writer);

// ---------------------------------------------------------------------------
// 読む
// ---------------------------------------------------------------------------

/**
 * @struct space_balloon_communication_format_tlv_reader_t
 * @brief ペイロードを先頭から読んでいく途中の状態．メンバは内部用なので直接触らない．
 */
typedef struct {
  const uint8_t *data;
  size_t length;
  size_t offset;
} space_balloon_communication_format_tlv_reader_t;

/**
 * @struct space_balloon_communication_format_tlv_t
 * @brief 読み出した TLV 1 つ．read_* で Value の先頭から順に値を取り出せる．
 */
typedef struct {
  uint8_t tag;
  uint8_t length;
  const uint8_t *value;
  /** read_* でどこまで取り出したか（内部用） */
  uint8_t read_offset;
} space_balloon_communication_format_tlv_t;

/** @brief next の結果 */
typedef enum {
  /** TLV を 1 つ読んだ */
  SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_FOUND = 1,
  /** ペイロードを最後まで読んだ */
  SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_END = 0,
  /** Length のぶんのバイトが残っていない（ペイロードが壊れている） */
  SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_TRUNCATED = -1,
} space_balloon_communication_format_tlv_result_t;

/** @brief 読み始める（data はふつう packet.payload） */
void space_balloon_communication_format_tlv_reader_init(space_balloon_communication_format_tlv_reader_t *reader, const uint8_t *data, size_t length);

/**
 * @brief 次の TLV を読む．知らないタグは，何もせずに次を呼べば読み飛ばせる．
 * @param tlv 読んだ TLV を入れる先
 */
space_balloon_communication_format_tlv_result_t space_balloon_communication_format_tlv_next(space_balloon_communication_format_tlv_reader_t *reader, space_balloon_communication_format_tlv_t *tlv);

/**
 * @brief Value の先頭から順に値を取り出す（リトルエンディアン）．
 * @return 取り出せたら 1．Value が足りなければ 0（out は変えない）
 */
int space_balloon_communication_format_tlv_read_u8(space_balloon_communication_format_tlv_t *tlv, uint8_t *out);
int space_balloon_communication_format_tlv_read_i16(space_balloon_communication_format_tlv_t *tlv, int16_t *out);
int space_balloon_communication_format_tlv_read_u32(space_balloon_communication_format_tlv_t *tlv, uint32_t *out);
int space_balloon_communication_format_tlv_read_i32(space_balloon_communication_format_tlv_t *tlv, int32_t *out);

#ifdef __cplusplus
}
#endif

#endif
