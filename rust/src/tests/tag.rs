use super::{gps, pressure};
use crate::tag::{
    Field, FieldType, MAX_VALUE_LEN, Tag, TagDefinitionError, field_size, validate_tags,
};

#[test]
fn field_size_matches_each_type() {
    assert_eq!(field_size(FieldType::U8), 1);
    assert_eq!(field_size(FieldType::I16), 2);
    assert_eq!(field_size(FieldType::U32), 4);
    assert_eq!(field_size(FieldType::I32), 4);
}

#[test]
fn value_len_is_sum_of_field_sizes() {
    assert_eq!(gps().value_len(), 13); // i32 × 3 + u8
}

#[test]
fn to_raw_is_inverse_of_to_value() {
    let gps = gps();
    let latitude = &gps.get_fields()[0]; // scale 1e-7
    assert_eq!(latitude.to_raw(33.5902), 335_902_000);
    assert_eq!(latitude.to_raw(-33.5902), -335_902_000);
    assert_eq!(latitude.to_value(latitude.to_raw(130.4017)), 130.4017);
}

// ---------------------------------------------------------------------------
// 定義の確認（コードで組み立てた定義）
// ---------------------------------------------------------------------------

/// `len` バイトの Value を持つタグ（i32 と u8 を組み合わせて作る）．
fn tag_with_value_len(id: u8, len: usize) -> Tag {
    let mut fields: Vec<Field> = (0..len / 4)
        .map(|i| Field::new(format!("i{i}"), FieldType::I32, None, 1.0))
        .collect();
    fields.extend((0..len % 4).map(|i| Field::new(format!("u{i}"), FieldType::U8, None, 1.0)));
    Tag::new(id, "t", fields)
}

#[test]
fn accepts_valid_definitions() {
    assert_eq!(validate_tags(&[pressure(), gps()]), Ok(()));
}

#[test]
fn rejects_duplicate_tag_id() {
    // pressure と同じ 0x01 を使う
    let other = Tag::new(0x01, "other", gps().get_fields().to_vec());
    assert_eq!(
        validate_tags(&[pressure(), other]),
        Err(TagDefinitionError::DuplicateTag(0x01))
    );
}

#[test]
fn rejects_empty_tag_name() {
    let tag = Tag::new(0x20, "", vec![Field::new("x", FieldType::U8, None, 1.0)]);
    assert_eq!(
        validate_tags(&[tag]),
        Err(TagDefinitionError::EmptyName(0x20))
    );
}

#[test]
fn accepts_value_up_to_max_and_rejects_longer() {
    assert_eq!(
        validate_tags(&[tag_with_value_len(1, MAX_VALUE_LEN)]),
        Ok(())
    );
    assert_eq!(
        validate_tags(&[tag_with_value_len(1, MAX_VALUE_LEN + 1)]),
        Err(TagDefinitionError::ValueTooLong {
            id: 1,
            len: MAX_VALUE_LEN + 1
        })
    );
}
