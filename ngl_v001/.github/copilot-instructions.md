# SRVS project instructions

このリポジトリで SRVS 周辺を扱う際は、以下の構造と用語を前提に作業すること。

## 対象領域

- SRVS の主要シェーダは `ngl/shader/srvs/` 配下にある。
- BBV デバッグ表示は `ngl/shader/srvs/debug_util/voxel_debug_visualize_cs.hlsl`。
- SRVS の共通トレース / アドレス計算 / 補助関数は `ngl/shader/srvs/srvs_util.hlsli`。
- ランタイム側のデバッグ UI と dispatch パラメータ設定は `ngl/src/render/app/srvs/srvs.cpp`。

## 用語

- **SRVS**: Screen Reconstructed Voxel Structure 系の機能群全体。
- **BBV**: Bitmask Brick Voxel。bitmask と coarse occupancy を持つ voxel 構造。
- **Brick**: BBV の coarse セル。1 Brick は `k_bbv_per_voxel_resolution` 単位の fine voxel を内包する。
- **Fine voxel / bitmask voxel**: Brick 内の最細セル。
- **HiBrick**: 2x2x2 Brick cluster を 1 単位にした上位アクセラレータ。
- **WCP**: World/Window/whatever current project convention の probe 系 voxel 構造。BBV とは別カテゴリ。
- **SSP / Screen Space Probe**: 画面空間側の probe 更新 / デバッグ機能。

## BBV バッファ構造

- BBV 本体バッファの並びは `[bitmask region][brick data region][hibrick data region]`。
- Brick / HiBrick の count は bitmask 更新後に後段パスで再構築する前提。
- HiBrick data region は logical 2x2x2 Brick cluster 順で保持する。

## BBV trace の整理方針

- 通常の BBV トレース入口:
  - `trace_bbv`
  - `trace_bbv_initial_hit_avoidance`
  - `trace_bbv_inverse_bit`
- HiBrick を使う入口:
  - `trace_bbv_hibrick`
  - `trace_bbv_initial_hit_avoidance_hibrick`
  - `trace_bbv_dev_hibrick`
- 開発用の比較入口:
  - `trace_bbv_dev`
  - `trace_bbv_hibrick_no_skip`
  - `trace_bbv_dev_hibrick_no_skip`

trace 系を触る場合は、通常版 / 初期ヒット回避版 / dev 版で意味が揃っているか確認すること。

## 最近の重要事項

- BBV デバッグ表示は HiBrick 版トレースへ寄せて整理済み。
- BBV debug sub mode 1 は **非HiBrick版での最細セル色分け表示**。
- 以前あった HiBrick 重複モードや skip 無効比較モードは整理済みのため、番号追加時は `srvs.cpp` の sub mode 上限と合わせて更新すること。

## DDA / reciprocal に関する注意

- `calc_safe_trace_ray_dir_inv` は名前に反して、現状は **旧来どおり `1.0 / ray_dir` を返す** 実装を採用している。
- ray dir の 0 軸を巨大値へ置き換える safe reciprocal を試したところ、HiBrick 有無に関係なく BBV デバッグ表示で特定カメラ角度に **1px の横線欠け** が発生した。
- DDA 側の開始セル、侵入位置、外側ステップ更新を切り分けても症状は消えず、reciprocal の扱いを戻すと解消した。
- BBV trace の境界欠けを見た場合は、DDA 本体より先に reciprocal の扱いを疑うこと。

## 編集時の期待

- SRVS 変更時は shader 側と `srvs.cpp` 側の debug mode / 定数 / 呼び出し先の整合を保つこと。
- BBV debug 表示の mode を追加・削除したら、少なくとも `voxel_debug_visualize_cs.hlsl` と `srvs.cpp` の両方を更新すること。
- trace 関数を整理するときは、コメントで「どのレイヤを走査するか」「debug 出力の各成分の意味」を簡潔に残すこと。
