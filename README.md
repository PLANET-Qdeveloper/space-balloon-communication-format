# space-balloon-communication-format

成層圏気球のダウンリンク（テレメトリ）向け通信フォーマットと、そのエンコード・デコード用ライブラリ。

PLANET-Q 共通通信フォーマット（`pq_com_format`）をフレーム層にし、地上側のテレメバックエンドが対応表に従ってペイロードを解釈する。

**作業中。** 動く実装は C。同じ役割を Rust へ移植している。

## 何をするものか

気球側と地上局のあいだで、可変長のセンサ値をやり取りする。中身は TLV（Tag-Length-Value）を並べたペイロードで、フレームの外枠は固定。

| 層 | 役割 | 場所 |
| --- | --- | --- |
| フレーム | バイト列のエンコード・デコード。開始・終了マーカー、宛先 / 送信元、CRC、バイトスタッフィング | C: [`c/`](c/)（現行）<br>Rust: [`rust/crates/pq-com-format`](rust/crates/pq-com-format)（移植中） |
| 解釈 | 対応表に沿って TLV を名前付きの値にする | Rust: [`rust/crates/downlink`](rust/crates/downlink)（移植中） |
| 対応表 | ノード ID とタグの定義 | [`spec/downlink.yaml`](spec/downlink.yaml)（例） |

プロトコルの詳細は [docs/pq_com_format.md](docs/pq_com_format.md)。

フレームの形は次のとおり。

```text
0x7E | 宛先ID | 送信元ID | ペイロード長 | ペイロード | CRC16 | 0x7F
```

ペイロードは `Tag(1) + Length(1) + Value(n)` をそのまま連結したもの。`0x7E` / `0x7F` / `0x7D` がデータの途中に出たらスタッフィングする。

## ディレクトリ

```text
c/        C 実装（エンコード・デコード）とテスト
rust/     Rust ワークスペース（移植先）
spec/     ダウンリンク対応表
docs/     プロトコルの説明
scripts/  対応表まわりの補助スクリプト
```

## ビルドとテスト

### C

Ninja と CMake 3.16 以降が必要。

```sh
cd c
cmake --preset default
cmake --build --preset default
ctest --preset default
```

リリースビルドは preset 名を `release` に替える。組み込み向け（テストなし）は `embedded`。

### Rust

```sh
cargo test --manifest-path rust/Cargo.toml
```

ワークスペースは `pq-com-format` と `downlink` の 2 クレート。中身はこれから。

## ライセンス

[MIT](LICENSE)

Copyright (c) 2024-2026 PLANET-Q。
