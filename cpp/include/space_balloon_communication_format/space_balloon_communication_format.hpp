// C 版（c/）を C++ のクラスでくるんだもの．処理の中身はすべて C 版にある．
//
// Arduino でも使えるように，標準ライブラリ（std::vector など）は使わず C++11 の範囲で書く．
#ifndef SPACE_BALLOON_COMMUNICATION_FORMAT_HPP
#define SPACE_BALLOON_COMMUNICATION_FORMAT_HPP

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "space_balloon_communication_format/space_balloon_communication_format.h"

namespace space_balloon_communication_format {

// C++11 には inline 変数がないので，ヘッダに置く定数は enum にする
// （constexpr 変数だと，使わないファイルで unused の警告が出る）
enum : size_t {
  /** ペイロードの最大長 */
  MAX_PAYLOAD_LEN = SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_PAYLOAD_SIZE,
  /** エンコードしたフレームの最大長（このバイト数のバッファがあれば必ず足りる） */
  MAX_FRAME_LEN = SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_FRAME_SIZE,
};

/** 1 バイト読んだ結果 */
enum class DecodeResult : int {
  /** 読んだ．フレームはまだ続く */
  Valid = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID,
  /** フレームを読み終えた．Decoder::get_frame() で中身を取り出せる */
  Completed = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED,
  /** 開始マーカーを待っている間に別のバイトが来た */
  InvalidHeader = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_HEADER,
  /** 終了マーカーの位置がおかしい */
  InvalidFooter = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER,
  /** CRC が合わない */
  InvalidChecksum = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_CHECKSUM,
  /** 0x7D のあとに決められていないバイトが来た */
  InvalidStuffing = SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_STUFFING,
};

/** フレーム 1 つ分の中身（宛先ID・送信元ID・ペイロード）． */
class SpaceBalloonCommunicationFormat {
 public:
  SpaceBalloonCommunicationFormat() { space_balloon_communication_format_clear(&packet_); }

  uint8_t get_destination_id() const { return packet_.destination_id; }
  void set_destination_id(uint8_t id) { packet_.destination_id = id; }

  uint8_t get_source_id() const { return packet_.source_id; }
  void set_source_id(uint8_t id) { packet_.source_id = id; }

  const uint8_t *get_payload() const { return packet_.payload; }
  size_t get_payload_length() const { return packet_.payload_length; }

  /** ペイロードを入れる．MAX_PAYLOAD_LEN より長ければ何も変えずに false を返す． */
  bool set_payload(const uint8_t *data, size_t length) {
    if (length > MAX_PAYLOAD_LEN) {
      return false;
    }
    if (length > 0) {
      memcpy(packet_.payload, data, length);
    }
    packet_.payload_length = static_cast<uint8_t>(length);
    return true;
  }

  /** フレームにして output に書く．書いたバイト数を返す（output が足りなければ 0）． */
  size_t encode(uint8_t *output, size_t capacity) const {
    return space_balloon_communication_format_encode(&packet_, output, capacity);
  }

  /** 配列にそのまま書く．`uint8_t buf[MAX_FRAME_LEN]; frame.encode(buf);` */
  template <size_t N>
  size_t encode(uint8_t (&output)[N]) const {
    return encode(output, N);
  }

  /** C 版の構造体（C の関数に直接渡したいとき用） */
  const space_balloon_communication_format_t &get_raw() const { return packet_; }

 private:
  friend class Decoder;
  space_balloon_communication_format_t packet_;
};

/**
 * 受信したバイトを 1 つずつ読んでフレームにする．受信するストリームごとに 1 つ作る．
 *
 *     Decoder decoder;
 *     if (decoder.push(byte) == DecodeResult::Completed) {
 *       const SpaceBalloonCommunicationFormat &frame = decoder.get_frame();
 *     }
 */
class Decoder {
 public:
  Decoder() { space_balloon_communication_format_decoder_init(&state_); }

  /** 1 バイト読む．エラーのあとも，そのまま渡し続ければ次のフレームから読み直す． */
  DecodeResult push(uint8_t byte) {
    DecodeResult result =
        static_cast<DecodeResult>(space_balloon_communication_format_decode(&state_, byte));
    if (result == DecodeResult::Completed) {
      frame_.packet_ = state_.packet;
    }
    return result;
  }

  /** 最後に読み終えたフレーム（push が Completed を返したあとに使う） */
  const SpaceBalloonCommunicationFormat &get_frame() const { return frame_; }

 private:
  space_balloon_communication_format_decoder_t state_;
  SpaceBalloonCommunicationFormat frame_;
};

}  // namespace space_balloon_communication_format

#endif
