//! フレームのエンコード・デコード．
//!
//! フレームの形（`0x7E` と `0x7F` の間はスタッフィングする）:
//!
//! ```text
//! [0x7E, 宛先ID, 送信元ID, ペイロード長, ペイロード..., CRC上位, CRC下位, 0x7F]
//!        └──────────── CRC の範囲 ────────────┘
//! ```
//!
//! CRC はスタッフィング前の「宛先ID・送信元ID・ペイロード長・ペイロード」に対して計算する．

/// ペイロードの最大長．フレームのペイロード長が 1 バイトなので 255．
pub const MAX_PAYLOAD_LEN: usize = 255;

/// 宛先ID・送信元ID・ペイロード長の 3 バイト．
const HEADER_LEN: usize = 3;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SpaceBalloonCommunicationFormat {
    destination_id: u8,
    source_id: u8,
    /// 実データ（TLV の並び）．長さは `MAX_PAYLOAD_LEN` 以下．
    payload: Vec<u8>,
}

impl SpaceBalloonCommunicationFormat {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn get_destination_id(&self) -> u8 {
        self.destination_id
    }

    pub fn set_destination_id(&mut self, destination_id: u8) {
        self.destination_id = destination_id;
    }

    pub fn get_source_id(&self) -> u8 {
        self.source_id
    }

    pub fn set_source_id(&mut self, source_id: u8) {
        self.source_id = source_id;
    }

    pub fn get_payload(&self) -> &[u8] {
        &self.payload
    }

    pub fn set_payload(&mut self, payload: &[u8]) -> Result<(), PayloadError> {
        if payload.len() > MAX_PAYLOAD_LEN {
            return Err(PayloadError::TooLong);
        }
        self.payload = payload.to_vec();
        Ok(())
    }

    pub fn encode(&self) -> Vec<u8> {
        // set_payload で MAX_PAYLOAD_LEN 以下にしてあるので u8 に収まる
        let header = [
            self.destination_id,
            self.source_id,
            self.payload.len() as u8,
        ];
        let crc = crc16(header.iter().chain(&self.payload)).to_be_bytes();

        let mut frame = vec![0x7E];
        for &byte in header.iter().chain(&self.payload).chain(&crc) {
            match stuff_byte(byte) {
                Some(pair) => frame.extend_from_slice(&pair),
                None => frame.push(byte),
            }
        }
        frame.push(0x7F);
        frame
    }

    pub fn decode(buf: &[u8]) -> Result<Self, DecodeError> {
        let interior = match buf {
            [0x7E, interior @ .., 0x7F] => interior,
            [0x7E, ..] => return Err(DecodeError::InvalidFooter),
            _ => return Err(DecodeError::InvalidHeader),
        };
        // body = [宛先ID, 送信元ID, ペイロード長, ペイロード..., CRC上位, CRC下位]
        let body = unstuff_bytes(interior)?;
        let [destination_id, source_id, len, ..] = body[..] else {
            return Err(DecodeError::InvalidLength);
        };
        let (covered, crc_bytes) = body
            .split_at_checked(HEADER_LEN + usize::from(len))
            .ok_or(DecodeError::InvalidLength)?;
        let crc_bytes: [u8; 2] = crc_bytes
            .try_into()
            .map_err(|_| DecodeError::InvalidLength)?;
        if crc16(covered) != u16::from_be_bytes(crc_bytes) {
            return Err(DecodeError::InvalidChecksum);
        }

        Ok(Self {
            destination_id,
            source_id,
            payload: covered[HEADER_LEN..].to_vec(),
        })
    }
}

#[derive(Debug, PartialEq, Eq)]
pub enum PayloadError {
    TooLong,
}

#[derive(Debug, PartialEq, Eq)]
pub enum DecodeError {
    InvalidHeader,
    InvalidFooter,
    InvalidStuffing,
    InvalidLength,
    InvalidChecksum,
}

fn stuff_byte(b: u8) -> Option<[u8; 2]> {
    match b {
        0x7E => Some([0x7D, 0x81]),
        0x7F => Some([0x7D, 0x80]),
        0x7D => Some([0x7D, 0x7D]),
        _ => None,
    }
}

fn unstuff_bytes(interior: &[u8]) -> Result<Vec<u8>, DecodeError> {
    let mut out = Vec::with_capacity(interior.len());
    let mut bytes = interior.iter().copied();
    while let Some(byte) = bytes.next() {
        out.push(match byte {
            0x7E => return Err(DecodeError::InvalidHeader),
            0x7F => return Err(DecodeError::InvalidFooter),
            0x7D => match bytes.next() {
                Some(0x81) => 0x7E,
                Some(0x80) => 0x7F,
                Some(0x7D) => 0x7D,
                _ => return Err(DecodeError::InvalidStuffing),
            },
            byte => byte,
        });
    }
    Ok(out)
}

/// CRC-16/CCITT-FALSE（多項式 0x1021，初期値 0xFFFF）．
/// バイトを順に返すものなら何でも受け取れるので，配列をつなげずに計算できる．
pub(crate) fn crc16<'a>(bytes: impl IntoIterator<Item = &'a u8>) -> u16 {
    let mut crc = 0xFFFFu16;
    for &byte in bytes {
        crc ^= u16::from(byte) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 == 0 {
                crc << 1
            } else {
                (crc << 1) ^ 0x1021
            };
        }
    }
    crc
}
