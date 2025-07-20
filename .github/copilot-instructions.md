# Copilot Instructions

## File Encoding Rules
- **新規作成のh, cppファイルは UTF-8 BOM付きとする**
- This prevents "Characters that cannot be displayed in current code page (932)" warnings in Visual Studio
- Ensures proper handling of Japanese comments if needed

## HLSL Shader Naming Rules
- **HLSLシェーダファイルはファイル名末尾のSuffixでシェーダステージを明示する**
- VertexShader = `_vs.hlsl`
- PixelShader = `_ps.hlsl`
- ComputeShader = `_cs.hlsl`
- 例: `sample_pixel_shader_ps.hlsl`
- 対応シェーダステージは今後追加予定

## HLSL Shader Entry Point Naming Rules
- **シェーダのエントリポイント名は `main_[シェーダステージSuffix]` とする**
- PixelShader = `main_ps`
- VertexShader = `main_vs`
- ComputeShader = `main_cs`