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
- **メンバー変数: snake_case + 末尾 `_` を推奨** (例: `p_window_`, `swapchain_rtvs_`)
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



