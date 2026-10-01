//! フレームのエンコード・デコード．
pub struct SpaceBalloonCommunicationFormat {
    destination_id: u8,  /*宛先ID */
    source_id: u8,       /*送信元ID */
    payload_length: u16, /* ペイロードのバイト数 */
    payload: [u8; 255],  /* 実データ（TLVの並び） */
}

impl SpaceBalloonCommunicationFormat {
    pub fn new() -> Self {
        Self {
            destination_id: 0,
            source_id: 0,
            payload_length: 0,
            payload: [0; 255],
        }
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

    pub fn get_payload_length(&self) -> u16 {
        self.payload_length
    }

    pub fn get_payload(&self) -> &[u8] {
        &self.payload[..usize::from(self.payload_length as u8)]
    }

    pub fn set_payload(&mut self, payload: &[u8]) -> Result<(), PayloadError> {
        if payload.len() > self.payload.len() {
            return Err(PayloadError::TooLong);
        }
        self.payload[..payload.len()].copy_from_slice(payload);
        self.payload[payload.len()..].fill(0);
        self.payload_length = payload.len() as u16;
        Ok(())
    }

    pub fn encode(&self) -> Vec<u8> {
        let len = usize::from(self.payload_length as u8);
        let payload = &self.payload[..len];
        let crc = crc16(payload);

        let mut body = Vec::new();
        body.push(self.destination_id);
        body.push(self.source_id);
        body.push(len as u8);
        body.extend_from_slice(payload);
        body.extend_from_slice(&crc.to_be_bytes());

        let mut frame = vec![0x7E];
        for byte in body {
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
        let body = unstuff_bytes(interior)?;
        let (&destination_id, &source_id, &len, rest) = match body.as_slice() {
            [destination_id, source_id, len, rest @ ..] => (destination_id, source_id, len, rest),
            _ => return Err(DecodeError::InvalidLength),
        };
        let (payload_bytes, crc_bytes) = rest
            .split_at_checked(usize::from(len))
            .ok_or(DecodeError::InvalidLength)?;
        let crc_bytes: [u8; 2] = crc_bytes
            .try_into()
            .map_err(|_| DecodeError::InvalidLength)?;
        if crc16(payload_bytes) != u16::from_be_bytes(crc_bytes) {
            return Err(DecodeError::InvalidChecksum);
        }

        let mut payload = [0; 255];
        payload[..payload_bytes.len()].copy_from_slice(payload_bytes);
        Ok(Self {
            destination_id,
            source_id,
            payload_length: u16::from(len),
            payload,
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

fn crc16(payload: &[u8]) -> u16 {
    let mut crc = 0xFFFFu16;
    for &byte in payload {
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
