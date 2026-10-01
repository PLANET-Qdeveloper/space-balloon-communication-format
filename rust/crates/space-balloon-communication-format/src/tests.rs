use crate::format::{DecodeError, PayloadError, SpaceBalloonCommunicationFormat};
use crate::tag::{Field, FieldType, Tag, TagError, read_payload, tags_from_yaml};

fn frame(destination_id: u8, source_id: u8, payload: &[u8]) -> SpaceBalloonCommunicationFormat {
    let mut packet = SpaceBalloonCommunicationFormat::new();
    packet.set_destination_id(destination_id);
    packet.set_source_id(source_id);
    packet.set_payload(payload).unwrap();
    packet
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

#[test]
fn decode_roundtrip() {
    let packet = frame(0x10, 0x01, &[0x01, 0x02]);
    let decoded = SpaceBalloonCommunicationFormat::decode(&packet.encode()).unwrap();
    assert_eq!(decoded.get_destination_id(), 0x10);
    assert_eq!(decoded.get_source_id(), 0x01);
    assert_eq!(decoded.get_payload_length(), 2);
    assert_eq!(decoded.get_payload(), &[0x01, 0x02]);
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
    encoded[4] ^= 0x01;
    assert!(matches!(
        SpaceBalloonCommunicationFormat::decode(&encoded),
        Err(DecodeError::InvalidChecksum)
    ));
}

#[test]
fn reads_gps_fields_in_order() {
    let value = [
        0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x1E, 0x03,
    ];
    let mut payload = vec![0x10, value.len() as u8];
    payload.extend_from_slice(&value);
    let fields = read_payload(&payload, &[gps()]).unwrap()[0]
        .get_fields()
        .to_vec();
    assert_eq!(fields[0].get_name(), "latitude");
    assert_eq!(fields[0].get_raw(), Some(10));
    assert_eq!(fields[0].get_value(), Some(10.0 * 1.0e-7));
    assert_eq!(fields[0].get_unit(), Some("deg"));
    assert_eq!(fields[3].get_name(), "fix_type");
    assert_eq!(fields[3].get_raw(), Some(3));
    assert_eq!(fields[3].get_unit(), None);
}

#[test]
fn reads_one_tlv_from_payload() {
    let pressure = Tag::new(
        0x01,
        "pressure",
        vec![Field::new(
            "pressure",
            FieldType::U32,
            Some("Pa".to_string()),
            0.001,
        )],
    );
    let payload = [0x01, 0x04, 0x00, 0x01, 0x8B, 0xCD];
    let decoded = read_payload(&payload, &[pressure]).unwrap();
    let field = &decoded[0].get_fields()[0];
    assert_eq!(decoded[0].get_id(), 0x01);
    assert_eq!(field.get_raw(), Some(101_325));
    assert_eq!(field.get_value(), Some(101_325.0 * 0.001));
    assert_eq!(field.get_unit(), Some("Pa"));
}

#[test]
fn rejects_value_whose_length_does_not_match_fields() {
    let error = read_payload(&[0x10, 4, 0, 0, 0, 0], &[gps()]).unwrap_err();
    assert_eq!(
        error,
        TagError::LengthMismatch {
            expected: 13,
            actual: 4,
        }
    );
}

#[test]
fn loads_tags_from_downlink_yaml() {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../spec/downlink.yaml");
    let text = std::fs::read_to_string(path).unwrap();
    let tags = tags_from_yaml(&text).unwrap();

    assert_eq!(tags.len(), 3);
    assert_eq!(tags[0].get_id(), 0x01);
    assert_eq!(tags[0].get_name(), "");
    assert_eq!(tags[0].get_fields()[0].get_name(), "pressure");
    assert_eq!(tags[0].get_fields()[0].get_type(), FieldType::U32);
    assert_eq!(tags[0].get_fields()[0].get_scale(), 0.001);

    let gps = &tags[2];
    assert_eq!(gps.get_id(), 0x10);
    assert_eq!(gps.get_name(), "gps_position");
    assert_eq!(gps.get_fields().len(), 4);
    assert_eq!(gps.get_fields()[3].get_name(), "fix_type");
    assert_eq!(gps.get_fields()[3].get_type(), FieldType::U8);
    assert_eq!(gps.get_fields()[3].get_scale(), 1.0);
    assert_eq!(gps.get_fields()[3].get_unit(), None);

    let payload = [0x01, 0x04, 0x00, 0x01, 0x8B, 0xCD];
    let decoded = read_payload(&payload, &tags).unwrap();
    assert_eq!(decoded[0].get_fields()[0].get_raw(), Some(101_325));
}
