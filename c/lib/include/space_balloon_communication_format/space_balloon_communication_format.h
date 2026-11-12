#ifndef SPACE_BALLOON_COMMUNICATION_FORMAT_H
#define SPACE_BALLOON_COMMUNICATION_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/** ペイロードの最大長（ペイロード長は 1 バイトで表す） */
#define SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_PAYLOAD_SIZE 255
/**
 * エンコードしたフレームの最大長．
 * 開始・終了マーカー 2 バイトと，ヘッダ 3 + ペイロード + CRC 2 がすべてスタッフィングで 2 倍になった場合．
 */
#define SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_FRAME_SIZE \
  (2 + 2 * (3 + SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_PAYLOAD_SIZE + 2))

#define SPACE_BALLOON_COMMUNICATION_FORMAT_START_MARKER 0x7E
#define SPACE_BALLOON_COMMUNICATION_FORMAT_END_MARKER 0x7F
#define SPACE_BALLOON_COMMUNICATION_FORMAT_ESCAPE 0x7D

/**
 * @enum space_balloon_communication_format_decode_result_t
 * @brief 1 バイト読んだ結果
 */
typedef enum {
  /** 読んだ．フレームはまだ続く */
  SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_VALID = 0,
  /** フレームを読み終えた．decoder->packet に中身が入っている */
  SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED = 1,
  /** 開始マーカーを待っている間に別のバイトが来た */
  SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_HEADER = -1,
  /** 終了マーカーの位置がおかしい */
  SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_FOOTER = -2,
  /** CRC が合わない */
  SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_CHECKSUM = -3,
  /** 0x7D のあとに決められていないバイトが来た */
  SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_ERROR_INVALID_STUFFING = -4,
} space_balloon_communication_format_decode_result_t;

/**
 * @struct space_balloon_communication_format_t
 * @brief フレームの中身
 */
typedef struct {
  /** 宛先ID */
  uint8_t destination_id;
  /** 送信元ID */
  uint8_t source_id;
  /** ペイロード長(バイト数) */
  uint8_t payload_length;
  /** ペイロードデータ */
  uint8_t payload[SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_PAYLOAD_SIZE];
} space_balloon_communication_format_t;

/**
 * @struct space_balloon_communication_format_decoder_t
 * @brief デコードの途中の状態．受信するストリームごとに 1 つ用意する．
 *
 * packet 以外のメンバは内部用なので直接触らない．
 */
typedef struct {
  /** 読み終えた（または読みかけの）フレーム */
  space_balloon_communication_format_t packet;
  /** 受け取った CRC（内部用） */
  uint16_t crc_checksum;
  /** ペイロードの何バイト目まで読んだか（内部用） */
  uint8_t index;
  /** 今どこを読んでいるか（内部用） */
  uint8_t state;
  /** 直前が 0x7D だったか（内部用） */
  uint8_t is_stuffed;
} space_balloon_communication_format_decoder_t;

/**
 * @fn
 * @brief パケットを空にする
 * @param packet 空にするパケット
 */
void space_balloon_communication_format_clear(space_balloon_communication_format_t *packet);

/**
 * @fn
 * @brief CRC16 チェックサムを計算する（CRC-16/CCITT-FALSE）
 * @param data チェックサムを計算するデータ
 * @param length チェックサムを計算するデータの長さ
 * @return CRC16 チェックサム
 */
uint16_t space_balloon_communication_format_calculate_crc_checksum(const uint8_t *data,
                                                                   uint16_t length);

/**
 * @fn
 * @brief パケットをフレームにして output に書く．
 *
 * CRC はヘッダ（宛先ID・送信元ID・ペイロード長）とペイロードが対象．
 * output は SPACE_BALLOON_COMMUNICATION_FORMAT_MAX_FRAME_SIZE バイトあれば必ず足りる．
 * @param packet エンコードするパケット
 * @param output フレームを書く先
 * @param capacity output の大きさ
 * @return 書いたバイト数．output が足りなければ 0
 */
size_t space_balloon_communication_format_encode(const space_balloon_communication_format_t *packet,
                                                 uint8_t *output, size_t capacity);

/**
 * @fn
 * @brief デコーダを初期状態にする（開始マーカーを待つ）
 * @param decoder 初期化するデコーダ
 */
void space_balloon_communication_format_decoder_init(
    space_balloon_communication_format_decoder_t *decoder);

/**
 * @fn
 * @brief 受信したバイトを 1 つずつ渡す．
 *
 * SPACE_BALLOON_COMMUNICATION_FORMAT_DECODE_COMPLETED を返したら，decoder->packet にフレームの
 * 中身が入っている（次の開始マーカーが来るまで変わらない）．
 * エラーを返したあとも，そのままバイトを渡し続ければ次のフレームから読み直す．
 * @param decoder デコーダ
 * @param input 受信したバイト
 * @return 読んだ結果
 * @see space_balloon_communication_format_decode_result_t
 */
space_balloon_communication_format_decode_result_t space_balloon_communication_format_decode(
    space_balloon_communication_format_decoder_t *decoder, uint8_t input);

#ifdef __cplusplus
}
#endif

#endif
