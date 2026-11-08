use super::{GPS_VALUE, PRESSURE_TLV, gps, known, pressure, tlv};
use crate::payload::{DecodedTag, ReadError, WriteError, read_payload, write_tag};
use crate::tag::{Field, FieldType, Tag};

// ---------------------------------------------------------------------------
// 読み取り
// ---------------------------------------------------------------------------

#[test]
fn reads_one_tlv() {
    let tags = [pressure()];
    let decoded = read_payload(&PRESSURE_TLV, &tags).unwrap();

    let (tag, raws) = known(&decoded[0]);
    let field = &tag.get_fields()[0];
    assert_eq!(tag.get_id(), 0x01);
    assert_eq!(raws, &[101_325]);
    assert_eq!(field.to_value(raws[0]), 101_325.0 * 0.001);
    assert_eq!(field.get_unit(), Some("Pa"));
}

#[test]
fn reads_fields_in_order() {
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
        ReadError::LengthMismatch {
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
        ReadError::LengthMismatch {
            id: 0x10,
            actual: 14
        }
    );
}

#[test]
fn rejects_truncated_value_even_for_unknown_tag() {
    let error = read_payload(&[0x20, 0x05, 0x00], &[gps()]).unwrap_err();
    assert_eq!(error, ReadError::Truncated);
}

#[test]
fn read_error_has_readable_message() {
    assert_eq!(
        ReadError::LengthMismatch {
            id: 0x10,
            actual: 4
        }
        .to_string(),
        "tag 0x10: length 4 does not match its fields"
    );
}

// ---------------------------------------------------------------------------
// 書き込み
// ---------------------------------------------------------------------------

#[test]
fn writes_little_endian_tlv() {
    let mut payload = Vec::new();
    write_tag(&mut payload, &gps(), &[10, 20, 30, 3]).unwrap();
    assert_eq!(payload, tlv(0x10, &GPS_VALUE));
}

#[test]
fn written_payload_reads_back_to_same_values() {
    let tags = [gps(), pressure()];
    let mut payload = Vec::new();
    write_tag(
        &mut payload,
        &tags[0],
        &[-335_902_000, i32::MAX.into(), -5, 0],
    )
    .unwrap();
    write_tag(&mut payload, &tags[1], &[u32::MAX.into()]).unwrap();
    write_tag(&mut payload, &tags[0], &[1, 2, 3, 255]).unwrap();

    let decoded = read_payload(&payload, &tags).unwrap();
    assert_eq!(
        known(&decoded[0]).1,
        &[-335_902_000, i32::MAX.into(), -5, 0]
    );
    assert_eq!(known(&decoded[1]).1, &[i64::from(u32::MAX)]);
    assert_eq!(known(&decoded[2]).1, &[1, 2, 3, 255]);
}

#[test]
fn rejects_wrong_number_of_values() {
    let mut payload = vec![0xAA];
    let error = write_tag(&mut payload, &gps(), &[1, 2, 3]).unwrap_err();
    assert_eq!(
        error,
        WriteError::FieldCountMismatch {
            id: 0x10,
            expected: 4,
            actual: 3
        }
    );
    assert_eq!(payload, [0xAA]); // 失敗しても元のまま
}

#[test]
fn rejects_value_out_of_range_and_keeps_payload() {
    let mut payload = vec![0xAA];
    // fix_type は u8 なので 256 は入らない
    let error = write_tag(&mut payload, &gps(), &[1, 2, 3, 256]).unwrap_err();
    assert_eq!(
        error,
        WriteError::OutOfRange {
            id: 0x10,
            field: 3,
            raw: 256
        }
    );
    assert_eq!(payload, [0xAA]); // 途中まで書いた分も消える
}

#[test]
fn rejects_value_longer_than_length_byte() {
    // i32 を 64 個 = 256 バイト
    let fields = (0..64)
        .map(|i| Field::new(format!("f{i}"), FieldType::I32, None, 1.0))
        .collect();
    let tag = Tag::new(0x30, "big", fields);
    let mut payload = Vec::new();
    let error = write_tag(&mut payload, &tag, &[0; 64]).unwrap_err();
    assert_eq!(error, WriteError::ValueTooLong { id: 0x30, len: 256 });
    assert!(payload.is_empty());
}
