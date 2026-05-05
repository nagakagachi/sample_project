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
- **新規作成・編集するソースファイルは、ファイル先頭に「ファイル名」と「ファイル説明」のコメントを必ず記述すること。**
- **処理スコープ（主要ブロック）ごとに、「何をしているか」を簡素なコメントで記述すること。**

## Project TODO / backlog

- **BrickLocalAABB の追加**
  - 各 BBV Brick について occupied fine voxel の local min/max を保持し、Brick 内トレース開始位置や空領域スキップを改善する。
  - 8x8x8 / 512bit bitmask の leaf 走査を軽くするための次の高速化候補として扱う。

- **HiBrick trace の本番系比較と置き換え**
  - デバッグ表示以外の SRVS トレースでは、まだ `trace_bbv` 呼び出しが残っている箇所がある。
  - 少なくとも `bbv_removal_list_build_cs.hlsl`、`ss_probe_direct_sh_update_cs.hlsl`、`ss_probe_update_cs.hlsl`、`wcp_element_update_cs.hlsl`、`wcp_visible_surface_element_update_cs.hlsl` の性能比較対象にする。
  - `trace_bbv_hibrick` 適用後に性能とヒット整合性を確認し、問題なければ本番経路を段階的に置き換える。

- **HiBrick / Brick 充填率を使った VoxelCone trace の実装と検証**
  - HiBrick と Brick の occupancy ratio を使い、cone 幅に応じた近似トレース / 積分に使えるか検証する。
  - 品質と性能の両面で SRVS への適用可能性を確認する。
