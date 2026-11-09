// C 版の TLV（c/lib/include/space_balloon_communication_format/tlv.h）を C++ のクラスでくるんだもの．
//
// タグの定義（spec/downlink.yaml）は見ない．どのタグにどの型を入れるかは使う側で決める．
#ifndef SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_HPP
#define SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_HPP

#include <stddef.h>
#include <stdint.h>

#include "space_balloon_communication_format/space_balloon_communication_format.hpp"
#include "space_balloon_communication_format/tlv.h"

namespace space_balloon_communication_format {

enum : size_t {
  /** TLV 1 つの Value の最大長 */
  MAX_VALUE_LEN = SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_MAX_VALUE_SIZE,
};

/**
 * ペイロードに TLV を詰めていく．
 *
 *     TlvWriter payload;
 *     payload.put_u32(0x01, 101325);   // 値が 1 つのタグ
 *     payload.begin(0x10);             // 値が複数のタグ
 *     payload.add_i32(latitude);
 *     payload.add_u8(fix_type);
 *     payload.end();
 *     if (payload.is_ok()) frame.set_payload(payload.get_data(), payload.get_length());
 *
 * 一度失敗すると，それ以降の書き込みはすべて失敗する（最後に is_ok で 1 回確かめればよい）．
 */
class TlvWriter {
 public:
  TlvWriter() { clear(); }
  // 中で自分の buffer_ を指しているので，コピーすると壊れる
  TlvWriter(const TlvWriter &) = delete;
  TlvWriter &operator=(const TlvWriter &) = delete;

  /** 空にして，最初から書き直す */
  void clear() { space_balloon_communication_format_tlv_writer_init(&writer_, buffer_, sizeof(buffer_)); }

  /** タグを書き始める．このあと add_* で値を足して，end で閉じる． */
  bool begin(uint8_t tag) { return space_balloon_communication_format_tlv_begin(&writer_, tag) != 0; }
  bool add_u8(uint8_t value) { return space_balloon_communication_format_tlv_add_u8(&writer_, value) != 0; }
  bool add_i16(int16_t value) { return space_balloon_communication_format_tlv_add_i16(&writer_, value) != 0; }
  bool add_u32(uint32_t value) { return space_balloon_communication_format_tlv_add_u32(&writer_, value) != 0; }
  bool add_i32(int32_t value) { return space_balloon_communication_format_tlv_add_i32(&writer_, value) != 0; }
  bool add_bytes(const uint8_t *data, size_t length) { return space_balloon_communication_format_tlv_add_bytes(&writer_, data, length) != 0; }
  /** タグを閉じる．Length はここで入る． */
  bool end() { return space_balloon_communication_format_tlv_end(&writer_) != 0; }

  /** 値が 1 つだけのタグを書く（begin + add + end） */
  bool put_u8(uint8_t tag, uint8_t value) { return space_balloon_communication_format_tlv_put_u8(&writer_, tag, value) != 0; }
  bool put_i16(uint8_t tag, int16_t value) { return space_balloon_communication_format_tlv_put_i16(&writer_, tag, value) != 0; }
  bool put_u32(uint8_t tag, uint32_t value) { return space_balloon_communication_format_tlv_put_u32(&writer_, tag, value) != 0; }
  bool put_i32(uint8_t tag, int32_t value) { return space_balloon_communication_format_tlv_put_i32(&writer_, tag, value) != 0; }

  /** ここまで失敗がなく，閉じていないタグもないか */
  bool is_ok() const { return !writer_.has_error && !writer_.is_open; }
  /** 書いたバイト列 */
  const uint8_t *get_data() const { return buffer_; }
  /** 書いたバイト数（is_ok でなければ 0） */
  size_t get_length() const { return space_balloon_communication_format_tlv_writer_finish(&writer_); }

 private:
  uint8_t buffer_[MAX_PAYLOAD_LEN];
  space_balloon_communication_format_tlv_writer_t writer_;
};

/** TlvReader::next の結果 */
enum class TlvReadResult : int {
  /** TLV を 1 つ読んだ */
  Found = SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_FOUND,
  /** ペイロードを最後まで読んだ */
  End = SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_END,
  /** Length のぶんのバイトが残っていない（ペイロードが壊れている） */
  Truncated = SPACE_BALLOON_COMMUNICATION_FORMAT_TLV_TRUNCATED,
};

/** 読み出した TLV 1 つ．read_* で Value の先頭から順に値を取り出せる． */
class Tlv {
 public:
  uint8_t get_tag() const { return tlv_.tag; }
  uint8_t get_length() const { return tlv_.length; }
  const uint8_t *get_value() const { return tlv_.value; }

  /** 次の値を取り出す．Value が足りなければ false（out は変えない） */
  bool read_u8(uint8_t &out) { return space_balloon_communication_format_tlv_read_u8(&tlv_, &out) != 0; }
  bool read_i16(int16_t &out) { return space_balloon_communication_format_tlv_read_i16(&tlv_, &out) != 0; }
  bool read_u32(uint32_t &out) { return space_balloon_communication_format_tlv_read_u32(&tlv_, &out) != 0; }
  bool read_i32(int32_t &out) { return space_balloon_communication_format_tlv_read_i32(&tlv_, &out) != 0; }

 private:
  friend class TlvReader;
  space_balloon_communication_format_tlv_t tlv_ = {};
};

/**
 * ペイロードの TLV を先頭から 1 つずつ読む．知らないタグは，何もせずに次を読めば読み飛ばせる．
 *
 *     TlvReader reader(decoder.get_frame());
 *     Tlv tlv;
 *     while (reader.next(tlv) == TlvReadResult::Found) {
 *       if (tlv.get_tag() == 0x01) tlv.read_u32(pressure);
 *     }
 *
 * 読んでいるペイロード（フレーム）は，読み終えるまで消したり書き換えたりしない．
 */
class TlvReader {
 public:
  TlvReader(const uint8_t *data, size_t length) { space_balloon_communication_format_tlv_reader_init(&reader_, data, length); }
  explicit TlvReader(const SpaceBalloonCommunicationFormat &frame) : TlvReader(frame.get_payload(), frame.get_payload_length()) {}

  /** 次の TLV を読む */
  TlvReadResult next(Tlv &out) { return static_cast<TlvReadResult>(space_balloon_communication_format_tlv_next(&reader_, &out.tlv_)); }

 private:
  space_balloon_communication_format_tlv_reader_t reader_;
};

}  // namespace space_balloon_communication_format

#endif
