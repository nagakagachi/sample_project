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

## CBT Tessellation Development Guidelines

### ConstantBuffer Management
- **ConstantBufferPoolを使用してダブルバッファリング問題を回避する**
- 毎フレーム`device->GetConstantBufferPool()->Alloc()`で新規確保
- `UpdateConstants()`は`ConstantBufferPooledHandle`を返す
- `BindResources()`で`ConstantBufferPooledHandle`を受け取る設計

### Shader-CPU Data Structure Alignment
- **パディングは配列ではなく個別変数で定義する**
- ❌ `uint32_t padding[3];` (シェーダとC++で挙動差異)
- ✅ `uint32_t padding1, padding2, padding3;` (明示的制御)
- 16byteアライメントを厳密に管理する

### Important Point System
- **カメラ特化ではなく汎用的なimportant_point概念を使用**
- `SetImportantPoint()/GetImportantPoint()` APIでワールド座標指定
- シェーダ内で`world_to_object`変換してオブジェクト空間で処理
- テッセレーション評価基準の柔軟性を確保

### Resource Binding Patterns
- **CBTリソースのバインドはBindResources()で統一**
- ConstantBufferPooledHandleを明示的に渡す
- 複数シェイプでの一貫した処理パターン
- 早期リターンによる効率的な範囲チェック

### Code Organization
- **CBT関連定数は`cbt_tess_common.hlsli`に集約**
- Bisectorコマンドビットマスク定数を適切に定義
- ヘルパー関数による処理の抽象化
- 包括的なコメントでアルゴリズムを説明