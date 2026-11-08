//! downlink.yaml の定義を使って，送る側でフレームを作り，受ける側で値を読む．
//!
//! 実行: `cargo run --example downlink`

use space_balloon_communication_format::catalog::tags_from_yaml;
use space_balloon_communication_format::format::SpaceBalloonCommunicationFormat;
use space_balloon_communication_format::payload::{DecodedTag, read_payload, write_tag};
use space_balloon_communication_format::tag::{Field, FieldType, Tag};

type Error = Box<dyn std::error::Error>;

fn main() -> Result<(), Error> {
    // 起動時に 1 回だけ定義を読み込む（以降はずっと持っておく）
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../spec/downlink.yaml");
    let tags = tags_from_yaml(&std::fs::read_to_string(path)?)?;

    // ---- 送る側（機体）----
    let sent = build_frame(&tags)?;
    println!("sent {} bytes", sent.len());

    // ---- 受ける側（地上局）----
    let frame = SpaceBalloonCommunicationFormat::decode(&sent)?;
    println!(
        "from 0x{:02X} to 0x{:02X}",
        frame.get_source_id(),
        frame.get_destination_id()
    );
    for decoded in read_payload(frame.get_payload(), &tags)? {
        match decoded {
            DecodedTag::Known { tag, raws } => {
                println!("[0x{:02X}] {}", tag.get_id(), tag.get_name());
                for (field, &raw) in tag.get_fields().iter().zip(&raws) {
                    println!(
                        "    {:<10} = {} {}  (raw {raw})",
                        field.get_name(),
                        field.to_value(raw),
                        field.get_unit().unwrap_or(""),
                    );
                }
            }
            DecodedTag::Unknown { id, value } => {
                println!("[0x{id:02X}] unknown tag, {} bytes skipped", value.len());
            }
        }
    }

    Ok(())
}

/// 気圧・GPS と，受ける側が知らないタグを 1 つ入れたフレームを作る．
fn build_frame(tags: &[Tag]) -> Result<Vec<u8>, Error> {
    let find = |name: &str| {
        tags.iter()
            .find(|tag| tag.get_name() == name)
            .ok_or(format!("tag `{name}` is not defined"))
    };
    let pressure = find("pressure")?;
    let gps = find("gps_position")?;
    // 受ける側の yaml にないタグ（新しいセンサーを足した場合など）
    let extra = Tag::new(
        0x7A,
        "extra",
        vec![Field::new("x", FieldType::I16, None, 1.0)],
    );

    let mut payload = Vec::new();
    // 物理量から raw にするときは to_raw を使う
    let fields = gps.get_fields();
    write_tag(
        &mut payload,
        pressure,
        &[pressure.get_fields()[0].to_raw(101.325)],
    )?;
    write_tag(&mut payload, &extra, &[-1])?;
    write_tag(
        &mut payload,
        gps,
        &[
            fields[0].to_raw(33.5902),  // 緯度 deg
            fields[1].to_raw(130.4017), // 経度 deg
            fields[2].to_raw(30_500.0), // 高度 m
            3,                          // 3D fix
        ],
    )?;

    let mut packet = SpaceBalloonCommunicationFormat::new();
    packet.set_destination_id(0x01); // ground_station
    packet.set_source_id(0x10); // main1
    packet.set_payload(&payload)?;
    Ok(packet.encode())
}
