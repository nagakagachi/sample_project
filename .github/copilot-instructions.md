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



