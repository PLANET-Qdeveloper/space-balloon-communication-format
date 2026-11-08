use crate::format::{DecodeError, PayloadError, SpaceBalloonCommunicationFormat, crc16};

fn frame(destination_id: u8, source_id: u8, payload: &[u8]) -> SpaceBalloonCommunicationFormat {
    let mut packet = SpaceBalloonCommunicationFormat::new();
    packet.set_destination_id(destination_id);
    packet.set_source_id(source_id);
    packet.set_payload(payload).unwrap();
    packet
}

#[test]
fn decode_roundtrip() {
    let packet = frame(0x10, 0x01, &[0x01, 0x02]);
    let decoded = SpaceBalloonCommunicationFormat::decode(&packet.encode()).unwrap();
    assert_eq!(decoded.get_destination_id(), 0x10);
    assert_eq!(decoded.get_source_id(), 0x01);
    assert_eq!(decoded.get_payload(), &[0x01, 0x02]);
}

/// 他言語の実装と合わせる期待値．CRC は宛先ID・送信元ID・ペイロード長・ペイロードが対象．
#[test]
fn encode_matches_known_frame() {
    let encoded = frame(0x10, 0x01, &[0x01, 0x02]).encode();
    assert_eq!(
        encoded,
        [0x7E, 0x10, 0x01, 0x02, 0x01, 0x02, 0x1E, 0xF1, 0x7F]
    );
}

#[test]
fn crc16_matches_ccitt_false_check_value() {
    assert_eq!(crc16(b"123456789"), 0x29B1);
}

#[test]
fn decode_unstuffs_markers_inside_the_frame() {
    let payload = [0x7E, 0x7F, 0x7D, 0x01];
    let packet = frame(0x7E, 0x7D, &payload);
    let decoded = SpaceBalloonCommunicationFormat::decode(&packet.encode()).unwrap();
    assert_eq!(decoded.get_destination_id(), 0x7E);
    assert_eq!(decoded.get_source_id(), 0x7D);
    assert_eq!(decoded.get_payload(), &payload);
}

#[test]
fn set_payload_rejects_more_than_255_bytes() {
    let mut packet = SpaceBalloonCommunicationFormat::new();
    assert_eq!(packet.set_payload(&[0; 256]), Err(PayloadError::TooLong));
}

#[test]
fn decode_rejects_bad_checksum() {
    let mut encoded = frame(0x10, 0x01, &[0x01, 0x02]).encode();
    encoded[4] ^= 0x01; // ペイロードの 1 バイト目
    assert_eq!(
        SpaceBalloonCommunicationFormat::decode(&encoded),
        Err(DecodeError::InvalidChecksum)
    );
}

#[test]
fn decode_rejects_corrupted_header() {
    for index in [1, 2] {
        // 宛先ID・送信元ID
        let mut encoded = frame(0x10, 0x01, &[0x01, 0x02]).encode();
        encoded[index] ^= 0x01;
        assert_eq!(
            SpaceBalloonCommunicationFormat::decode(&encoded),
            Err(DecodeError::InvalidChecksum),
            "byte {index} was corrupted but decode succeeded"
        );
    }
}

#[test]
fn decode_error_has_readable_message() {
    assert_eq!(
        DecodeError::InvalidChecksum.to_string(),
        "checksum mismatch"
    );
}
