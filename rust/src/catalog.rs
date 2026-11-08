//! yaml（`spec/downlink.yaml` の形式）からタグの定義を読み込む．
//!
//! 形の確認（必要な項目・型名）は読み込みのときに，中身の確認（番号の重複・
//! 空の名前・Value の長さ）は読み込んだあとに行う．

use crate::format::MAX_PAYLOAD_LEN;
use crate::tag::{Field, FieldType, Tag};

/// 1 つのタグの Value の最大長．
/// ペイロードは最大 255 バイトで，そこに Tag と Length の 2 バイトも入るので 253．
pub const MAX_VALUE_LEN: usize = MAX_PAYLOAD_LEN - 2;

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

/// yaml からタグの定義を作り，中身がおかしい定義がないか確かめる．
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
        let id = tag.get_id();
        if std::mem::replace(&mut seen[usize::from(id)], true) {
            return Err(CatalogError::DuplicateTag(id));
        }
        if tag.get_name().is_empty() {
            return Err(CatalogError::EmptyName(id));
        }
        let len = tag.value_len();
        if len > MAX_VALUE_LEN {
            return Err(CatalogError::ValueTooLong { id, len });
        }
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// yaml の形
// ---------------------------------------------------------------------------

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
