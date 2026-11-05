//! TLV（Tag-Length-Value）の定義と読み取り．
//!
//! タグの定義（`Tag` / `Field`）は yaml から 1 回だけ作り，
//! ペイロードを読んだ結果（`DecodedTag`）はその定義を参照して数値だけを持つ．
//! 複数バイトの整数はリトルエンディアン（下位バイトが先）で読む．

// ---------------------------------------------------------------------------
// 定義
// ---------------------------------------------------------------------------

/// フィールドの型．何バイト読むかもこれで決まる．
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

    /// 読んだ raw にスケールをかけて物理量にする．
    pub fn to_value(&self, raw: i64) -> f64 {
        raw as f64 * self.scale
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
}

// ---------------------------------------------------------------------------
// 読み取り
// ---------------------------------------------------------------------------

/// ペイロードから読んだ TLV 1 つ分．
#[derive(Debug, Clone, PartialEq)]
pub enum DecodedTag<'a> {
    /// 定義にあるタグ．`raws[i]` は `tag.fields[i]` を読んだ値．
    Known { tag: &'a Tag, raws: Vec<i64> },
    /// 定義にないタグ．Length を使って読み飛ばし，番号と生のバイト列だけを残す．
    Unknown { id: u8, value: Vec<u8> },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TagError {
    /// Tag・Length，または Length の分の Value が残っていない．
    Truncated,
    /// Length がフィールドの並びと合わない（足りない・余る）．
    LengthMismatch { id: u8, actual: usize },
}

impl std::fmt::Display for TagError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            TagError::Truncated => write!(f, "TLV is truncated"),
            TagError::LengthMismatch { id, actual } => write!(
                f,
                "tag 0x{id:02X}: length {actual} does not match its fields"
            ),
        }
    }
}

impl std::error::Error for TagError {}

/// TLV の並びを先頭から順に読む．
pub fn read_payload<'a>(payload: &[u8], tags: &'a [Tag]) -> Result<Vec<DecodedTag<'a>>, TagError> {
    let mut rest = payload;
    let mut decoded = Vec::new();
    while !rest.is_empty() {
        let Some((&[id, len], body)) = rest.split_first_chunk::<2>() else {
            return Err(TagError::Truncated);
        };
        let Some((value, next)) = body.split_at_checked(usize::from(len)) else {
            return Err(TagError::Truncated);
        };

        decoded.push(match tags.iter().find(|tag| tag.id == id) {
            Some(tag) => {
                let raws = read_fields(tag, value).ok_or(TagError::LengthMismatch {
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
        .fields
        .iter()
        .map(|field| read_raw(field.field_type, &mut cursor))
        .collect::<Option<Vec<_>>>()?;
    cursor.is_empty().then_some(raws)
}

/// `cursor` の先頭から型の分だけ読み，読んだ分だけ `cursor` を進める．
/// 足りなければ `None`．何バイト読むかは `from_le_bytes` に渡す配列の長さで決まる．
fn read_raw(field_type: FieldType, cursor: &mut &[u8]) -> Option<i64> {
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

/// 型のバイト数．`read_raw` に実際に読ませて，進んだ分を数える．
/// サイズは `read_raw` の中にだけ書き，ここで二重に持たないためにこうしている．
pub(crate) fn field_size(field_type: FieldType) -> usize {
    /// どの型よりも大きいバッファ．
    const BUF_LEN: usize = 8;
    let buf = [0u8; BUF_LEN];
    let mut cursor = &buf[..];
    read_raw(field_type, &mut cursor)
        .expect("BUF_LEN must be at least the size of every FieldType");
    buf.len() - cursor.len()
}

// ---------------------------------------------------------------------------
// yaml からの読み込み
// ---------------------------------------------------------------------------

/// 1 つのタグの Value の最大長．
/// ペイロードは最大 255 バイトで，そこに Tag と Length の 2 バイトも入るので 253．
pub const MAX_VALUE_LEN: usize = crate::format::MAX_PAYLOAD_LEN - 2;

#[derive(Debug)]
pub enum CatalogError {
    /// yaml として読めない，または必要な項目がない．
    Yaml(serde_yaml::Error),
    /// `type` が対応していない型．
    UnknownType(String),
    /// 同じタグ番号が 2 回以上書かれている．
    DuplicateTag(u8),
    /// `name` が空．
    EmptyName(u8),
    /// フィールドの合計が `MAX_VALUE_LEN` を超えていて，送れない．
    ValueTooLong { id: u8, len: usize },
}

impl std::fmt::Display for CatalogError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            CatalogError::Yaml(error) => write!(f, "invalid yaml: {error}"),
            CatalogError::UnknownType(text) => write!(f, "unknown field type `{text}`"),
            CatalogError::DuplicateTag(id) => write!(f, "tag 0x{id:02X} is defined more than once"),
            CatalogError::EmptyName(id) => write!(f, "tag 0x{id:02X} has no name"),
            CatalogError::ValueTooLong { id, len } => write!(
                f,
                "tag 0x{id:02X}: fields total {len} bytes, more than {MAX_VALUE_LEN}"
            ),
        }
    }
}

impl std::error::Error for CatalogError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            CatalogError::Yaml(error) => Some(error),
            _ => None,
        }
    }
}

/// `downlink.yaml` の形式からタグの定義を作る．
/// 読み込んだあと，送れないタグや重複がないかを確認する．
pub fn tags_from_yaml(text: &str) -> Result<Vec<Tag>, CatalogError> {
    let file: CatalogFile = serde_yaml::from_str(text).map_err(CatalogError::Yaml)?;
    let tags = file
        .tags
        .into_iter()
        .map(CatalogTag::into_tag)
        .collect::<Result<Vec<_>, _>>()?;
    validate(&tags)?;
    Ok(tags)
}

/// 形は正しいが中身がおかしい定義を弾く．
fn validate(tags: &[Tag]) -> Result<(), CatalogError> {
    let mut seen = [false; 256];
    for tag in tags {
        if std::mem::replace(&mut seen[usize::from(tag.id)], true) {
            return Err(CatalogError::DuplicateTag(tag.id));
        }
        if tag.name.is_empty() {
            return Err(CatalogError::EmptyName(tag.id));
        }
        let len = tag
            .fields
            .iter()
            .map(|field| field_size(field.field_type))
            .sum();
        if len > MAX_VALUE_LEN {
            return Err(CatalogError::ValueTooLong { id: tag.id, len });
        }
    }
    Ok(())
}

#[derive(serde::Deserialize)]
struct CatalogFile {
    tags: Vec<CatalogTag>,
}

#[derive(serde::Deserialize)]
struct CatalogTag {
    tag: TagId,
    #[serde(default)]
    name: Option<String>,
    fields: Vec<CatalogField>,
}

impl CatalogTag {
    fn into_tag(self) -> Result<Tag, CatalogError> {
        let fields = self
            .fields
            .into_iter()
            .map(CatalogField::into_field)
            .collect::<Result<_, _>>()?;
        Ok(Tag::new(self.tag.0, self.name.unwrap_or_default(), fields))
    }
}

#[derive(serde::Deserialize)]
struct CatalogField {
    name: String,
    #[serde(rename = "type")]
    field_type: String,
    #[serde(default)]
    unit: Option<String>,
    #[serde(default = "default_scale")]
    scale: f64,
}

impl CatalogField {
    fn into_field(self) -> Result<Field, CatalogError> {
        let field_type = parse_field_type(&self.field_type)?;
        Ok(Field::new(self.name, field_type, self.unit, self.scale))
    }
}

fn default_scale() -> f64 {
    1.0
}

fn parse_field_type(text: &str) -> Result<FieldType, CatalogError> {
    match text {
        "u8" => Ok(FieldType::U8),
        "i16" => Ok(FieldType::I16),
        "u32" => Ok(FieldType::U32),
        "i32" => Ok(FieldType::I32),
        other => Err(CatalogError::UnknownType(other.to_string())),
    }
}

/// タグ番号．yaml では整数（`16`）でも 16 進文字列（`"0x10"`）でも書ける．
struct TagId(u8);

impl<'de> serde::Deserialize<'de> for TagId {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let id = match serde_yaml::Value::deserialize(deserializer)? {
            serde_yaml::Value::Number(number) => {
                number.as_u64().and_then(|id| u8::try_from(id).ok())
            }
            serde_yaml::Value::String(text) => parse_hex_tag(&text),
            _ => None,
        };
        id.map(TagId)
            .ok_or_else(|| serde::de::Error::custom("tag id must be an integer or 0x hex"))
    }
}

fn parse_hex_tag(text: &str) -> Option<u8> {
    let hex = text
        .strip_prefix("0x")
        .or_else(|| text.strip_prefix("0X"))?;
    u8::from_str_radix(hex, 16).ok()
}
