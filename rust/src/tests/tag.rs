use super::gps;
use crate::tag::{FieldType, field_size};

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
