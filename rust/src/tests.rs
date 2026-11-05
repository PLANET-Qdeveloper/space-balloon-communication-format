use crate::format::{DecodeError, PayloadError, SpaceBalloonCommunicationFormat, crc16};
use crate::tag::{
    CatalogError, DecodedTag, Field, FieldType, Tag, TagError, field_size, read_payload,
    tags_from_yaml,
};

// ---------------------------------------------------------------------------
// 共通
// ---------------------------------------------------------------------------

fn frame(destination_id: u8, source_id: u8, payload: &[u8]) -> SpaceBalloonCommunicationFormat {
    let mut packet = SpaceBalloonCommunicationFormat::new();
    packet.set_destination_id(destination_id);
    packet.set_source_id(source_id);
    packet.set_payload(payload).unwrap();
    packet
}

/// Tag と Value から TLV 1 つ分のバイト列を作る．
fn tlv(id: u8, value: &[u8]) -> Vec<u8> {
    let mut bytes = vec![id, value.len() as u8];
    bytes.extend_from_slice(value);
    bytes
}

/// 読んだ結果が定義にあるタグであることを確かめて，定義と値を取り出す．
fn known<'a>(decoded: &'a DecodedTag) -> (&'a Tag, &'a [i64]) {
    match decoded {
        DecodedTag::Known { tag, raws } => (tag, raws),
        DecodedTag::Unknown { id, .. } => panic!("tag 0x{id:02X} was not decoded as known"),
    }
}

fn gps() -> Tag {
    Tag::new(
        0x10,
        "gps_position",
        vec![
            Field::new("latitude", FieldType::I32, Some("deg".to_string()), 1.0e-7),
            Field::new("longitude", FieldType::I32, Some("deg".to_string()), 1.0e-7),
            Field::new("altitude", FieldType::I32, Some("m".to_string()), 0.001),
            Field::new("fix_type", FieldType::U8, None, 1.0),
        ],
    )
}

/// GPS の Value．latitude=10, longitude=20, altitude=30, fix_type=3．
const GPS_VALUE: [u8; 13] = [
    0x0A, 0x00, 0x00, 0x00, // latitude
    0x14, 0x00, 0x00, 0x00, // longitude
    0x1E, 0x00, 0x00, 0x00, // altitude
    0x03, // fix_type
];

/// 圧力 101325 の TLV（u32，リトルエンディアン）．
const PRESSURE_TLV: [u8; 6] = [0x01, 0x04, 0xCD, 0x8B, 0x01, 0x00];

// ---------------------------------------------------------------------------
// フレーム
// ---------------------------------------------------------------------------

#[test]
fn decode_roundtrip() {
    let packet = frame(0x10, 0x01, &[0x01, 0x02]);
    let decoded = SpaceBalloonCommunicationFormat::decode(&packet.encode()).unwrap();
    assert_eq!(decoded.get_destination_id(), 0x10);
    assert_eq!(decoded.get_source_id(), 0x01);
    assert_eq!(decoded.get_payload(), &[0x01, 0x02]);
}

/// C 実装と同じ期待値．CRC は宛先ID・送信元ID・ペイロード長・ペイロードに対して計算する．
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

// ---------------------------------------------------------------------------
// TLV の読み取り
// ---------------------------------------------------------------------------

#[test]
fn reads_one_tlv_from_payload() {
    let tags = [Tag::new(
        0x01,
        "pressure",
        vec![Field::new(
            "pressure",
            FieldType::U32,
            Some("Pa".to_string()),
            0.001,
        )],
    )];
    let decoded = read_payload(&PRESSURE_TLV, &tags).unwrap();

    let (tag, raws) = known(&decoded[0]);
    let field = &tag.get_fields()[0];
    assert_eq!(tag.get_id(), 0x01);
    assert_eq!(raws, &[101_325]);
    assert_eq!(field.to_value(raws[0]), 101_325.0 * 0.001);
    assert_eq!(field.get_unit(), Some("Pa"));
}

#[test]
fn reads_gps_fields_in_order() {
    let tags = [gps()];
    let decoded = read_payload(&tlv(0x10, &GPS_VALUE), &tags).unwrap();

    let (tag, raws) = known(&decoded[0]);
    let fields = tag.get_fields();
    assert_eq!(raws, &[10, 20, 30, 3]);
    assert_eq!(fields[0].get_name(), "latitude");
    assert_eq!(fields[0].get_unit(), Some("deg"));
    assert_eq!(fields[0].to_value(raws[0]), 10.0 * 1.0e-7);
    assert_eq!(fields[3].get_name(), "fix_type");
    assert_eq!(fields[3].get_unit(), None);
}

#[test]
fn skips_unknown_tag_and_keeps_reading() {
    let mut payload = tlv(0x20, &[0xAA, 0xBB, 0xCC]);
    payload.extend(tlv(0x10, &GPS_VALUE));
    let tags = [gps()];
    let decoded = read_payload(&payload, &tags).unwrap();

    assert_eq!(decoded.len(), 2);
    assert_eq!(
        decoded[0],
        DecodedTag::Unknown {
            id: 0x20,
            value: vec![0xAA, 0xBB, 0xCC],
        }
    );
    let (tag, raws) = known(&decoded[1]);
    assert_eq!(tag.get_id(), 0x10);
    assert_eq!(raws, &[10, 20, 30, 3]);
}

#[test]
fn rejects_value_shorter_than_fields() {
    let error = read_payload(&tlv(0x10, &[0; 4]), &[gps()]).unwrap_err();
    assert_eq!(
        error,
        TagError::LengthMismatch {
            id: 0x10,
            actual: 4
        }
    );
}

#[test]
fn rejects_value_longer_than_fields() {
    let error = read_payload(&tlv(0x10, &[0; 14]), &[gps()]).unwrap_err();
    assert_eq!(
        error,
        TagError::LengthMismatch {
            id: 0x10,
            actual: 14
        }
    );
}

#[test]
fn rejects_truncated_value_even_for_unknown_tag() {
    let error = read_payload(&[0x20, 0x05, 0x00], &[gps()]).unwrap_err();
    assert_eq!(error, TagError::Truncated);
}

// ---------------------------------------------------------------------------
// yaml からの読み込み
// ---------------------------------------------------------------------------

#[test]
fn loads_tags_from_downlink_yaml() {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../spec/downlink.yaml");
    let text = std::fs::read_to_string(path).unwrap();
    let tags = tags_from_yaml(&text).unwrap();
    assert_eq!(tags.len(), 3);

    let pressure = &tags[0];
    assert_eq!(pressure.get_id(), 0x01);
    assert_eq!(pressure.get_name(), "pressure");
    assert_eq!(pressure.get_fields()[0].get_name(), "pressure");
    assert_eq!(pressure.get_fields()[0].get_type(), FieldType::U32);
    assert_eq!(pressure.get_fields()[0].get_scale(), 0.001);

    let gps = &tags[2];
    assert_eq!(gps.get_id(), 0x10);
    assert_eq!(gps.get_name(), "gps_position");
    assert_eq!(gps.get_fields().len(), 4);
    assert_eq!(gps.get_fields()[3].get_name(), "fix_type");
    assert_eq!(gps.get_fields()[3].get_type(), FieldType::U8);
    assert_eq!(gps.get_fields()[3].get_scale(), 1.0);
    assert_eq!(gps.get_fields()[3].get_unit(), None);

    let decoded = read_payload(&PRESSURE_TLV, &tags).unwrap();
    let (_, raws) = known(&decoded[0]);
    assert_eq!(raws, &[101_325]);
}

/// yaml に書くタグ 1 つ分：(番号, 名前, [(フィールド名, 型)])．
type TagSpec<'a> = (u8, &'a str, &'a [(&'a str, &'a str)]);

/// タグを並べた yaml を作る．
fn catalog_yaml(tags: &[TagSpec]) -> String {
    let mut text = String::from("tags:\n");
    for (id, name, fields) in tags {
        text += &format!("  - tag: {id}\n    name: \"{name}\"\n    fields:\n");
        for (field_name, field_type) in *fields {
            text += &format!("      - name: {field_name}\n        type: {field_type}\n");
        }
    }
    text
}

#[test]
fn field_size_matches_each_type() {
    assert_eq!(field_size(FieldType::U8), 1);
    assert_eq!(field_size(FieldType::I16), 2);
    assert_eq!(field_size(FieldType::U32), 4);
    assert_eq!(field_size(FieldType::I32), 4);
}

#[test]
fn rejects_duplicate_tag_id() {
    let text = catalog_yaml(&[(1, "a", &[("x", "u8")]), (1, "b", &[("y", "u8")])]);
    assert!(matches!(
        tags_from_yaml(&text),
        Err(CatalogError::DuplicateTag(1))
    ));
}

#[test]
fn rejects_empty_tag_name() {
    let text = catalog_yaml(&[(1, "", &[("x", "u8")])]);
    assert!(matches!(
        tags_from_yaml(&text),
        Err(CatalogError::EmptyName(1))
    ));
}

#[test]
fn rejects_unknown_field_type() {
    let text = catalog_yaml(&[(1, "a", &[("x", "f128")])]);
    assert!(matches!(
        tags_from_yaml(&text),
        Err(CatalogError::UnknownType(ref name)) if name == "f128"
    ));
}

#[test]
fn accepts_value_up_to_max_and_rejects_longer() {
    // i32 を 63 個（252 バイト）＋ u8 で，ちょうど MAX_VALUE_LEN（253 バイト）
    let mut fields = vec![("x", "i32"); 63];
    fields.push(("y", "u8"));
    assert!(tags_from_yaml(&catalog_yaml(&[(1, "a", &fields)])).is_ok());

    // もう 1 バイト足すと送れない
    fields.push(("z", "u8"));
    assert!(matches!(
        tags_from_yaml(&catalog_yaml(&[(1, "a", &fields)])),
        Err(CatalogError::ValueTooLong { id: 1, len: 254 })
    ));
}

#[test]
fn errors_have_readable_messages() {
    assert_eq!(
        TagError::LengthMismatch {
            id: 0x10,
            actual: 4
        }
        .to_string(),
        "tag 0x10: length 4 does not match its fields"
    );
    assert_eq!(
        DecodeError::InvalidChecksum.to_string(),
        "checksum mismatch"
    );
    assert_eq!(
        CatalogError::DuplicateTag(0x01).to_string(),
        "tag 0x01 is defined more than once"
    );
}
