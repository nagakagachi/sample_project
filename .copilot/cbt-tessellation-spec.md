# CBTテッセレーションシステム - 技術仕様書

## 概要
Concurrent Binary Tree (CBT) を基盤としたソフトウェアテッセレーションシステムの技術仕様と実装詳細。

## システムアーキテクチャ

### 主要コンポーネント
1. **CBTGpuResources**: GPU リソース管理クラス
2. **CBTTessellationConstants**: 共通定数バッファ
3. **CBTシェーダパイプライン**: 11段階の処理パス

## データ構造

### CBT (Concurrent Binary Tree)
```
構造: 完全二分木 + 32bit uintリーフ
インデックス: 1ベース（0は未使用）
ノード総数: 2^(tree_depth + 1)
リーフ数: 2^tree_depth
リーフオフセット: 2^tree_depth
```

### メモリレイアウト例（depth=3）
```
[0=unused] [1=root] [2,3=level1] [4,5,6,7=level2] [8,9,10,11,12,13,14,15=leaves]
```

## リソース詳細

### バッファ構成
- **cbt_buffer**: CBT完全二分木（uint配列）
- **bisector_pool**: Bisector構造体配列
- **index_cache**: インデックス解決キャッシュ（int2配列）
- **alloc_counter**: 新規割り当てカウンタ（uint）
- **indirect_dispatch_args**: 間接ディスパッチ引数（uint3配列）

### 定数バッファ
```cpp
struct CBTConstants {
    uint32_t cbt_tree_depth;              // CBT木の深さ
    uint32_t cbt_mesh_minimum_tree_depth; // 最小深度
    uint32_t bisector_pool_max_size;      // プール最大サイズ
    uint32_t frame_index;                 // フレーム番号
    uint32_t total_half_edges;            // HalfEdge総数
    uint32_t padding[3];                  // 16バイトアライメント
};
```

## シェーダパイプライン

### 初期化フェーズ
1. **cbt_tess_init_leaf_cs**: リーフノードビットフィールド初期化
2. **cbt_sum_reduction_naive_cs**: Sum Reduction実行

### 毎フレーム処理フェーズ
1. **cbt_tess_begin_update_cs**: フレーム開始処理
2. **cbt_tess_cache_index_cs**: インデックスキャッシュ更新
3. **cbt_tess_reset_command_cs**: コマンドバッファリセット
4. **cbt_tess_generate_command_cs**: コマンド生成
5. **cbt_tess_reserve_block_cs**: メモリブロック予約
6. **cbt_tess_fill_new_block_cs**: 新規ブロック初期化
7. **cbt_tess_update_neighbor_cs**: 隣接情報更新
8. **cbt_tess_update_cbt_bitfield_cs**: CBTビットフィールド更新
9. **cbt_sum_reduction_naive_cs**: Sum Reduction実行
10. **cbt_tess_end_update_cs**: 更新完了処理

## 実装パターン

### DispatchHelper パターン
```cpp
// 自動グループ数計算
uint32_t work_size = cbt_gpu_resources.max_bisectors;
pso->DispatchHelper(command_list, work_size, 1, 1);
```

### リソースバインディング
```cpp
void CBTGpuResources::BindResources(
    ComputePipelineStateDep* pso, 
    DescriptorSetDep* desc_set) const {
    pso->SetView(desc_set, "cbt_buffer", cbt_buffer_srv.Get());
    pso->SetView(desc_set, "cbt_buffer_rw", cbt_buffer_uav.Get());
    // ...
}
```

## パフォーマンス考慮事項

### スレッドグループサイズ
- **標準**: `CBT_THREAD_GROUP_SIZE` (128)
- **特殊**: Sum Reduction等は1スレッド実行

### メモリアクセス最適化
- リーフレベルでの連続アクセス
- キャッシュ局所性の活用
- ビット操作による分岐最小化

## アルゴリズム詳細

### Sum Reduction
```hlsl
// ボトムアップ集計
for (uint level = tree_depth; level > 0; level--) {
    // 各レベルで左右の子ノードを合計
    // リーフ: ビットカウント
    // 内部ノード: 子ノードの合計値
}
```

### CBT検索
```hlsl
// i番目の1ビット位置を二分探索で高速検索
int FindIthBit1InCBT(Buffer<uint> cbt, uint target_index) {
    uint bit_id = 1; // ルートから開始
    // 完全二分木を下降しながら検索
}
```

## 開発状況

### 完了項目
- ✅ CBTGpuResourcesクラス実装
- ✅ 定数バッファ統一
- ✅ シェーダファイル命名規則統一
- ✅ スレッドグループサイズ統一
- ✅ DispatchHelper実装
- ✅ 初期化パイプライン実装

### 今後の実装予定
- 🔄 各シェーダパスの詳細実装
- 🔄 Indirect Dispatch最適化
- 🔄 Wave intrinsics活用
- 🔄 並列Sum Reduction実装

---

**最終更新**: 2025/07/20  
**責任者**: CBTテッセレーション開発チーム
