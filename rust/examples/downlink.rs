//! 受信したフレームから，downlink.yaml の定義に従って値を読む．
//!
//! 実行: `cargo run --example downlink`

use space_balloon_communication_format::format::SpaceBalloonCommunicationFormat;
use space_balloon_communication_format::tag::{DecodedTag, read_payload, tags_from_yaml};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // 起動時に 1 回だけ定義を読み込む（以降はずっと持っておく）
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../spec/downlink.yaml");
    let tags = tags_from_yaml(&std::fs::read_to_string(path)?)?;

    // 機体から届いたフレームの代わりに，ここで作っておく
    let received = sample_frame()?;

    // フレームを開いて，ペイロードの TLV を読む
    let frame = SpaceBalloonCommunicationFormat::decode(&received)?;
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

/// 気圧・定義にないタグ・GPS の 3 つを入れたフレーム．
fn sample_frame() -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    let mut payload = Vec::new();
    // 0x01 気圧（u32）：raw 101325 × scale 0.001
    push_tlv(&mut payload, 0x01, &101_325u32.to_le_bytes());
    // 0x7A 定義にないタグ：読み飛ばされる
    push_tlv(&mut payload, 0x7A, &[0xAA, 0xBB]);
    // 0x10 GPS（i32, i32, i32, u8）：福岡あたり
    let mut gps = Vec::new();
    gps.extend_from_slice(&335_902_000i32.to_le_bytes()); // 緯度 33.5902 deg
    gps.extend_from_slice(&1_304_017_000i32.to_le_bytes()); // 経度 130.4017 deg
    gps.extend_from_slice(&30_500_000i32.to_le_bytes()); // 高度 30500 m
    gps.push(3); // 3D fix
    push_tlv(&mut payload, 0x10, &gps);

    let mut packet = SpaceBalloonCommunicationFormat::new();
    packet.set_destination_id(0x01);
    packet.set_source_id(0x10);
    packet.set_payload(&payload)?;
    Ok(packet.encode())
}

fn push_tlv(payload: &mut Vec<u8>, id: u8, value: &[u8]) {
    payload.push(id);
    payload.push(value.len() as u8);
    payload.extend_from_slice(value);
}
