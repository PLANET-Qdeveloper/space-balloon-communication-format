//! テスト．モジュールごとにファイルを分け，共通で使うものはここに置く．

#[cfg(feature = "catalog")]
mod catalog;
mod format;
mod payload;
mod tag;

use crate::payload::DecodedTag;
use crate::tag::{Field, FieldType, Tag};

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

fn pressure() -> Tag {
    Tag::new(
        0x01,
        "pressure",
        vec![Field::new(
            "pressure",
            FieldType::U32,
            Some("Pa".to_string()),
            0.001,
        )],
    )
}

/// GPS の Value．latitude=10, longitude=20, altitude=30, fix_type=3．
const GPS_VALUE: [u8; 13] = [
    0x0A, 0x00, 0x00, 0x00, // latitude
    0x14, 0x00, 0x00, 0x00, // longitude
    0x1E, 0x00, 0x00, 0x00, // altitude
    0x03, // fix_type
];

/// 気圧 101325 の TLV（u32，リトルエンディアン）．
const PRESSURE_TLV: [u8; 6] = [0x01, 0x04, 0xCD, 0x8B, 0x01, 0x00];
