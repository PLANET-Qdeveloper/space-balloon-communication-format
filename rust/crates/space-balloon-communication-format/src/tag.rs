//! タグの基本形．
//!
//! 1つのタグは番号とフィールドの並びで，フィールドの型から何バイト読むかを決める．
//! `downlink.yaml` の各タグは，あとからこの形へ載せれば同じ手順で読める．
//! 複数バイトの整数は上位バイトから読む．

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FieldType {
    U8,
    I16,
    U32,
    I32,
}

impl FieldType {
    pub fn size(self) -> usize {
        match self {
            FieldType::U8 => 1,
            FieldType::I16 => 2,
            FieldType::U32 | FieldType::I32 => 4,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Field {
    name: String,
    field_type: FieldType,
    unit: Option<String>,
    scale: f64,
    raw: Option<i64>,
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
            raw: None,
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

    pub fn get_raw(&self) -> Option<i64> {
        self.raw
    }

    pub fn get_value(&self) -> Option<f64> {
        self.raw.map(|raw| raw as f64 * self.scale)
    }

    pub fn read_value(&self, bytes: &[u8]) -> Result<Self, TagError> {
        let size = self.field_type.size();
        if bytes.len() != size {
            return Err(TagError::LengthMismatch {
                expected: size,
                actual: bytes.len(),
            });
        }
        let mut field = self.clone();
        field.raw = Some(read_raw(self.field_type, bytes));
        Ok(field)
    }
}

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

    pub fn value_len(&self) -> usize {
        self.fields
            .iter()
            .map(|field| field.field_type.size())
            .sum()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TagError {
    Truncated,
    UnknownTag(u8),
    LengthMismatch { expected: usize, actual: usize },
}

pub fn read_payload(payload: &[u8], tags: &[Tag]) -> Result<Vec<Tag>, TagError> {
    let mut rest = payload;
    let mut decoded = Vec::new();
    while !rest.is_empty() {
        if rest.len() < 2 {
            return Err(TagError::Truncated);
        }
        let id = rest[0];
        let len = usize::from(rest[1]);
        let Some(value) = rest.get(2..2 + len) else {
            return Err(TagError::Truncated);
        };
        let tag = tags
            .iter()
            .find(|tag| tag.id == id)
            .ok_or(TagError::UnknownTag(id))?;
        if value.len() != tag.value_len() {
            return Err(TagError::LengthMismatch {
                expected: tag.value_len(),
                actual: value.len(),
            });
        }
        let mut offset = 0;
        let mut fields = Vec::with_capacity(tag.fields.len());
        for field in &tag.fields {
            let size = field.field_type.size();
            fields.push(field.read_value(&value[offset..offset + size])?);
            offset += size;
        }
        decoded.push(Tag {
            id,
            name: tag.name.clone(),
            fields,
        });
        rest = &rest[2 + len..];
    }
    Ok(decoded)
}

fn read_raw(field_type: FieldType, bytes: &[u8]) -> i64 {
    match field_type {
        FieldType::U8 => i64::from(bytes[0]),
        FieldType::I16 => i64::from(i16::from_be_bytes([bytes[0], bytes[1]])),
        FieldType::U32 => i64::from(u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])),
        FieldType::I32 => i64::from(i32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])),
    }
}

#[derive(Debug)]
pub enum CatalogError {
    Yaml(serde_yaml::Error),
    UnknownType(String),
}

pub fn tags_from_yaml(text: &str) -> Result<Vec<Tag>, CatalogError> {
    let file: CatalogFile = serde_yaml::from_str(text).map_err(CatalogError::Yaml)?;
    file.tags
        .into_iter()
        .map(|tag| {
            let fields = tag
                .fields
                .into_iter()
                .map(|field| {
                    Ok(Field::new(
                        field.name,
                        parse_field_type(&field.field_type)?,
                        field.unit,
                        field.scale,
                    ))
                })
                .collect::<Result<Vec<_>, CatalogError>>()?;
            Ok(Tag::new(tag.tag.0, tag.name.unwrap_or_default(), fields))
        })
        .collect()
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

fn default_scale() -> f64 {
    1.0
}

struct TagId(u8);

impl<'de> serde::Deserialize<'de> for TagId {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let value = serde_yaml::Value::deserialize(deserializer)?;
        let id = match value {
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

fn parse_field_type(text: &str) -> Result<FieldType, CatalogError> {
    match text {
        "u8" => Ok(FieldType::U8),
        "i16" => Ok(FieldType::I16),
        "u32" => Ok(FieldType::U32),
        "i32" => Ok(FieldType::I32),
        other => Err(CatalogError::UnknownType(other.to_string())),
    }
}
