//! タグの定義．
//!
//! 1 つのタグは番号とフィールドの並びで，Value にはフィールドが上から順に
//! リトルエンディアンで並ぶ．定義は yaml（`catalog`）やコードから 1 回だけ作り，
//! ペイロードの読み書き（`payload`）はこの定義を参照して行う．

/// フィールドの型．何バイト使うかもこれで決まる．
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FieldType {
    U8,
    I16,
    U32,
    I32,
}

/// タグの中の 1 項目の定義．
#[derive(Debug, Clone, PartialEq)]
pub struct Field {
    name: String,
    field_type: FieldType,
    unit: Option<String>,
    scale: f64,
}

impl Field {
    pub fn new(
        name: impl Into<String>,
        field_type: FieldType,
        unit: Option<String>,
        scale: f64,
    ) -> Self {
        Self {
            name: name.into(),
            field_type,
            unit,
            scale,
        }
    }

    pub fn get_name(&self) -> &str {
        &self.name
    }

    pub fn get_type(&self) -> FieldType {
        self.field_type
    }

    pub fn get_unit(&self) -> Option<&str> {
        self.unit.as_deref()
    }

    pub fn get_scale(&self) -> f64 {
        self.scale
    }

    /// raw にスケールをかけて物理量にする．
    pub fn to_value(&self, raw: i64) -> f64 {
        raw as f64 * self.scale
    }

    /// 物理量をスケールで割り，一番近い整数に丸めて raw にする（`to_value` の逆）．
    /// 型に収まるかどうかは書き込むときに確かめる．`NaN` は 0 になる．
    pub fn to_raw(&self, value: f64) -> i64 {
        (value / self.scale).round() as i64
    }
}

/// タグの定義．番号とフィールドの並び．
#[derive(Debug, Clone, PartialEq)]
pub struct Tag {
    id: u8,
    name: String,
    fields: Vec<Field>,
}

impl Tag {
    pub fn new(id: u8, name: impl Into<String>, fields: Vec<Field>) -> Self {
        Self {
            id,
            name: name.into(),
            fields,
        }
    }

    pub fn get_id(&self) -> u8 {
        self.id
    }

    pub fn get_name(&self) -> &str {
        &self.name
    }

    pub fn get_fields(&self) -> &[Field] {
        &self.fields
    }

    /// Value のバイト数（フィールドのサイズの合計）．
    pub fn value_len(&self) -> usize {
        self.fields
            .iter()
            .map(|field| field_size(field.field_type))
            .sum()
    }
}

// ---------------------------------------------------------------------------
// 型ごとのバイト変換
//
// 型と Rust の整数型の対応（U8 → u8 など）は `read_raw` と `write_raw` の 2 か所に
// 書いてある．型を足すときは両方に足すこと（書いて読み戻すテストで確かめている）．
// バイト数は `from_le_bytes` / `to_le_bytes` の型で決まるので，数字では書かない．
// ---------------------------------------------------------------------------

/// `cursor` の先頭から型の分だけ読み，読んだ分だけ `cursor` を進める．足りなければ `None`．
pub(crate) fn read_raw(field_type: FieldType, cursor: &mut &[u8]) -> Option<i64> {
    fn take<const N: usize>(cursor: &mut &[u8]) -> Option<[u8; N]> {
        let (head, rest) = cursor.split_first_chunk::<N>()?;
        *cursor = rest;
        Some(*head)
    }
    Some(match field_type {
        FieldType::U8 => i64::from(u8::from_le_bytes(take(cursor)?)),
        FieldType::I16 => i64::from(i16::from_le_bytes(take(cursor)?)),
        FieldType::U32 => i64::from(u32::from_le_bytes(take(cursor)?)),
        FieldType::I32 => i64::from(i32::from_le_bytes(take(cursor)?)),
    })
}

/// raw を型に合わせて `out` の末尾に足す．型に収まらなければ何も足さずに `None`．
pub(crate) fn write_raw(field_type: FieldType, raw: i64, out: &mut Vec<u8>) -> Option<()> {
    match field_type {
        FieldType::U8 => out.extend_from_slice(&u8::try_from(raw).ok()?.to_le_bytes()),
        FieldType::I16 => out.extend_from_slice(&i16::try_from(raw).ok()?.to_le_bytes()),
        FieldType::U32 => out.extend_from_slice(&u32::try_from(raw).ok()?.to_le_bytes()),
        FieldType::I32 => out.extend_from_slice(&i32::try_from(raw).ok()?.to_le_bytes()),
    }
    Some(())
}

/// 型のバイト数．`write_raw` に 0 を書かせて，何バイト増えたかを数える．
pub(crate) fn field_size(field_type: FieldType) -> usize {
    let mut buf = Vec::new();
    write_raw(field_type, 0, &mut buf).expect("0 fits in every FieldType");
    buf.len()
}
