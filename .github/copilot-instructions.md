# Copilot Instructions

## 人間とのやり取り
- **回答は日本語で行うこと**

## File Encoding Rules
- **新規作成のh, cppファイルは UTF-8 BOM付きとする**

## Naming Conventions (最大公約数)
- **対象範囲: ngl_v001/ngl/include, ngl_v001/ngl/src, ngl_v001/ngl/shader, sample_app の自作コードのみ**
- **外部依存 (ngl_v001/ngl/external など) と生成物は対象外**

### C++ File Naming
- **原則 snake_case** (例: `gfx_framework.h`, `test_render_path.cpp`)
- **プラットフォーム/バックエンドのサフィックスは現状利用を許容** (例: `device.d3d12.h`)

### C++ Type Naming
- **クラス/構造体/型: PascalCase** (例: `GraphicsFramework`, `RenderFrameDesc`)
- **列挙体: `E` プレフィックスを許容** (例: `EResourceState`, `EShaderStage`)
- **例外**
  - material_shader_manager は 自動生成コードとの対応をわかりやすくするため例外とする

### C++ Member/Function/Constant
- **classメンバー変数: snake_case + 末尾 `_` を推奨** (例: `p_window_`, `swapchain_rtvs_`)
- **structメンバー変数: snake_case（末尾 `_` なし）を推奨** (例: `param`, `xyz`)
- **ポインタ/参照系の接頭辞: `p_` / `ref_` を推奨** (例: `p_device_`, `ref_swapchain_`)
- **関数名: PascalCase を推奨** (例: `Initialize`, `BeginFrame`)
- **定数: `k_` プレフィックス + snake_case を推奨** (例: `k_pi_f`)
- **マクロ: 大文字 + `NGL_` プレフィックスを許容**
- **既存コードに lower_snake の関数名など混在があるため、厳格な強制はしない**

## HLSL Shader Naming Rules
- **.hlsliファイルはシェーダインクルードファイルとしてSnakeCaseとする**
- 例: `common_functions.hlsli`, `lighting_models.hlsli`
- **.hlslファイルは末尾にシェーダステージを示すSuffixを付加する**
- VertexShader = `_vs.hlsl`
- PixelShader = `_ps.hlsl`
- ComputeShader = `_cs.hlsl`
- DxrShader = `_lib.hlsl`
- 例: `sample_pixel_shader_ps.hlsl`

## HLSL Shader Entry Point Naming Rules
- **シェーダのエントリポイント名は `main_[シェーダステージSuffix]` とする**
- PixelShader = `main_ps`
- VertexShader = `main_vs`
- ComputeShader = `main_cs`

## Architecture Overview: Render Task Graph (RTG)

このプロジェクトはグラフィックスパイプラインを **RenderTaskGraph (RTG)** で管理する設計です。

### 基本概念
- **Pass/Task の依存関係をグラフで管理**
- **RTG がリソース割り当てと状態遷移を統括**
- **構築→最適化→実行のフローで処理**

## Project Structure & Namespaces

### モジュール構成
```
ngl_v001/ngl/
├── include/
│   ├── rhi/           - RHI層 (D3D12抽象化、CommandList/DescriptorPool)
│   ├── gfx/           - 低レベルグラフィックス (RenderTaskGraph, リソース管理)
│   ├── framework/     - フレームワーク層 (GfxScene, エンティティシステム)
│   ├── render/        - レンダリング実装 (Pass, Feature, RTG構築)
│   ├── resource/      - リソースハンドル、キャッシング
│   ├── math/          - 数学ユーティリティ
│   ├── memory/        - メモリ管理（PoolAllocator等）
│   └── util/          - 一般ユーティリティ (Handle factory, NonCopyable等)
└── src/               - 対応する実装ファイル
```

### Namespace 階層
- **`ngl`**: Root namespace
- **`ngl::rhi`**: D3D12 低レベルインタフェース
- **`ngl::gfx`**: グラフィックス中核
- **`ngl::rtg`**: RenderTaskGraph 関連（ヘッダは gfx/rtg 配下）
- **`ngl::fwk`**: フレームワーク（GfxScene等）
- **`ngl::render`**: レンダリング実装、Pass、Feature
- **`ngl::res`**: リソース管理

## File Layout (Structure Only)

### C++ ファイル対応規則
- `include/` と `src/` は同じパス/ファイル名で対応させる
  - 例: `include/framework/gfx_framework.h` ↔ `src/framework/gfx_framework.cpp`
  - 例: `include/render/test_render_path.h` ↔ `src/render/test_render_path.cpp`

## グラフィックス実装・デバッグ運用ルール（恒久）

### 実行優先順位
1. 対症療法より根本原因の修正を優先する。
2. 変更は最小限に保ちつつ、要求範囲は欠けなく満たす。
3. 明示要求がない限り、既存レンダリング挙動を維持する。

### GPUデバッグ必須チェック
- CPU側ディスパッチ設定とシェーダ宣言の CBV/SRV/UAV/Sampler バインド整合を確認する。
- IndirectArg / Counter バッファの初期化・更新・消費順序を確認する。
- Producer/Consumer パス境界のリソース状態遷移と UAV バリアを確認する。
- 座標系前提（NDC範囲、Reverse-Z、手系、View空間の符号）を確認する。
- 深度規約（hardware depth と linear/view Z の変換）を統一して確認する。
- MainView と ShadowView（カスケード含む）を分離し、独立ON/OFFで切り分ける。
- coarse/fine 階層を別々に検証し、Brick結果を fine 正常動作と混同しない。

### 不具合調査の進め方
- 最小再現経路（単一View・最小パス・決定的トグル）でまず切り分ける。
- 次にパス単位の観測点（カウンタ、デバッグバッファ、軽量可視化）を追加する。
- 報告は **症状** / **疑い段** / **確定根本原因** / **適用修正** を明示する。
- もっともらしい修正で改善しない場合、シェーダ数式変更より先にバインドとディスパッチ前提を再確認する。

### SRVS/BBV向け優先確認点
- Injection不発: 深度ソース、view情報CB、候補リスト生成、culling条件を確認する。
- Fine removal不発: fine bit更新経路が有効か、coarse専用経路や古いcounterに潰されていないか確認する。
- カメラ近傍の幽霊Injection: サーフェイス復元不正、frustum/depth判定のずれ、深度解釈ミスを確認する。

### コード変更スタイル
- 型安全で明示的な変更を優先する。
- 広域try/catch、無言フォールバック、失敗握りつぶしを避ける。
- コメントはGPU同期や数式前提など非自明点に限定して短く書く。


