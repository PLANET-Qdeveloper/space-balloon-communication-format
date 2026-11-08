//! ペイロード（TLV の並び）の読み書き．
//!
//! ```text
//! [Tag, Length, Value..., Tag, Length, Value..., ...]
//! ```
//!
//! Tag と Length は 1 バイト，Value は Length バイト．

use crate::tag::{Tag, read_raw, write_raw};

// ---------------------------------------------------------------------------
// 読み取り
// ---------------------------------------------------------------------------

/// ペイロードから読んだ TLV 1 つ分．
#[derive(Debug, Clone, PartialEq)]
pub enum DecodedTag<'a> {
    /// 定義にあるタグ．`raws[i]` は `tag.get_fields()[i]` の値．
    Known { tag: &'a Tag, raws: Vec<i64> },
    /// 定義にないタグ．Length を使って読み飛ばし，番号と生のバイト列だけを残す．
    Unknown { id: u8, value: Vec<u8> },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ReadError {
    /// Tag・Length，または Length の分の Value が残っていない．
    Truncated,
    /// Length がフィールドの並びと合わない（足りない・余る）．
    LengthMismatch { id: u8, actual: usize },
}

impl std::fmt::Display for ReadError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ReadError::Truncated => write!(f, "TLV is truncated"),
            ReadError::LengthMismatch { id, actual } => write!(
                f,
                "tag 0x{id:02X}: length {actual} does not match its fields"
            ),
        }
    }
}

impl std::error::Error for ReadError {}

/// TLV の並びを先頭から順に読む．結果は `tags` の中の定義を参照する．
pub fn read_payload<'a>(payload: &[u8], tags: &'a [Tag]) -> Result<Vec<DecodedTag<'a>>, ReadError> {
    let mut rest = payload;
    let mut decoded = Vec::new();
    while !rest.is_empty() {
        let Some((&[id, len], body)) = rest.split_first_chunk::<2>() else {
            return Err(ReadError::Truncated);
        };
        let Some((value, next)) = body.split_at_checked(usize::from(len)) else {
            return Err(ReadError::Truncated);
        };

        decoded.push(match tags.iter().find(|tag| tag.get_id() == id) {
            Some(tag) => {
                let raws = read_fields(tag, value).ok_or(ReadError::LengthMismatch {
                    id,
                    actual: value.len(),
                })?;
                DecodedTag::Known { tag, raws }
            }
            None => DecodedTag::Unknown {
                id,
                value: value.to_vec(),
            },
        });
        rest = next;
    }
    Ok(decoded)
}

/// Value を定義のフィールドの順に読む．過不足があれば `None`．
fn read_fields(tag: &Tag, value: &[u8]) -> Option<Vec<i64>> {
    let mut cursor = value;
    let raws = tag
        .get_fields()
        .iter()
        .map(|field| read_raw(field.get_type(), &mut cursor))
        .collect::<Option<Vec<_>>>()?;
    cursor.is_empty().then_some(raws)
}

// ---------------------------------------------------------------------------
// 書き込み
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum WriteError {
    /// 値の数が定義のフィールドの数と違う．
    FieldCountMismatch {
        id: u8,
        expected: usize,
        actual: usize,
    },
    /// 値がフィールドの型に収まらない（例：u8 に 256）．`field` は何番目のフィールドか．
    OutOfRange { id: u8, field: usize, raw: i64 },
    /// Value が 255 バイトを超えて Length に入らない．
    ValueTooLong { id: u8, len: usize },
}

impl std::fmt::Display for WriteError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            WriteError::FieldCountMismatch {
                id,
                expected,
                actual,
            } => write!(
                f,
                "tag 0x{id:02X}: expected {expected} values, got {actual}"
            ),
            WriteError::OutOfRange { id, field, raw } => write!(
                f,
                "tag 0x{id:02X}: value {raw} of field {field} is out of range for its type"
            ),
            WriteError::ValueTooLong { id, len } => {
                write!(f, "tag 0x{id:02X}: value is {len} bytes, more than 255")
            }
        }
    }
}

impl std::error::Error for WriteError {}

/// TLV を 1 つ作って `payload` の末尾に足す．`raws[i]` は `tag.get_fields()[i]` の値．
///
/// 失敗したときは `payload` を呼ぶ前の状態のまま返す．
pub fn write_tag(payload: &mut Vec<u8>, tag: &Tag, raws: &[i64]) -> Result<(), WriteError> {
    let id = tag.get_id();
    let fields = tag.get_fields();
    if raws.len() != fields.len() {
        return Err(WriteError::FieldCountMismatch {
            id,
            expected: fields.len(),
            actual: raws.len(),
        });
    }
    let len = tag.value_len();
    let Ok(length) = u8::try_from(len) else {
        return Err(WriteError::ValueTooLong { id, len });
    };

    let start = payload.len();
    payload.extend_from_slice(&[id, length]);
    for (index, (field, &raw)) in fields.iter().zip(raws).enumerate() {
        if write_raw(field.get_type(), raw, payload).is_none() {
            payload.truncate(start); // 途中まで書いた分を消す
            return Err(WriteError::OutOfRange {
                id,
                field: index,
                raw,
            });
        }
    }
    Ok(())
}
