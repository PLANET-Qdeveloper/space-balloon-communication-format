use super::{PRESSURE_TLV, known};
use crate::catalog::{CatalogError, tags_from_yaml};
use crate::payload::read_payload;
use crate::tag::{FieldType, TagDefinitionError};

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
    assert_eq!(known(&decoded[0]).1, &[101_325]);
}

#[test]
fn rejects_unknown_field_type() {
    let text = catalog_yaml(&[(1, "a", &[("x", "f128")])]);
    assert!(matches!(
        tags_from_yaml(&text),
        Err(CatalogError::UnknownType(ref name)) if name == "f128"
    ));
}

/// 中身の確認（validate_tags）が yaml の読み込みでも行われること．
/// 確認の中身そのものは tests/tag.rs で確かめる．
#[test]
fn validates_definitions_after_loading() {
    let duplicate = catalog_yaml(&[(1, "a", &[("x", "u8")]), (1, "b", &[("y", "u8")])]);
    assert!(matches!(
        tags_from_yaml(&duplicate),
        Err(CatalogError::Invalid(TagDefinitionError::DuplicateTag(1)))
    ));

    let empty_name = catalog_yaml(&[(1, "", &[("x", "u8")])]);
    assert!(matches!(
        tags_from_yaml(&empty_name),
        Err(CatalogError::Invalid(TagDefinitionError::EmptyName(1)))
    ));
}

#[test]
fn catalog_error_has_readable_message() {
    let error = CatalogError::Invalid(TagDefinitionError::DuplicateTag(0x01));
    assert_eq!(
        error.to_string(),
        "invalid tag definition: tag 0x01 is defined more than once"
    );
}
