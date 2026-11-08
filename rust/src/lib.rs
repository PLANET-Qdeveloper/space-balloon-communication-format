//! スペースバルーン通信フォーマット．
//!
//! - `format`  : フレーム（0x7E…0x7F，スタッフィング，CRC）
//! - `tag`     : タグの定義（フィールドの名前・型・単位・スケール）
//! - `payload` : ペイロード（TLV の並び）の読み書き
//! - `catalog` : yaml からタグの定義を読み込む

pub mod catalog;
pub mod format;
pub mod payload;
pub mod tag;

#[cfg(test)]
mod tests;
