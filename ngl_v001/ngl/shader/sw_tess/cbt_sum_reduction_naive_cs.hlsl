// cbt_sum_reduction_naive.hlsl - CBT SumReduction ナイーブ実装（シングルスレッド）
/*
    CBT Sum Reduction アルゴリズム概要:
    
    1. リーフノード（ビットフィールド）は他の処理で既に設定済み
    2. リーフレベルから親ノードへボトムアップで集計
    3. リーフ: CountBits32でビット数を計算
    4. 内部ノード: 左右の子ノードの合計値
    5. ルートノード（インデックス1）に全体の使用中ビット数が格納される
    
    注意事項:
    - リーフノードの値は変更しない（ビット情報保持）
    - 内部ノードのみ更新
    - 並列実装では各レベルでバリア同期が必要
*/
#include "cbt_tess_common.hlsli"


// CBT SumReduction - リーフのビットカウントを上位ノードに伝播（ナイーブなシングルスレッド実装）
[numthreads(1, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    // スレッド0のみで全体を処理（ナイーブ実装）
    if (DTid.x != 0) return;
    
    // 共通定数バッファからパラメータを取得
    const uint leaf_count = GetCBTLeafCount();
    const uint leaf_offset = GetCBTLeafOffset();
    const uint tree_depth = cbt_tree_depth;
    
    // リーフから上位ノードへボトムアップでSum Reductionを実行
    // リーフのビット情報は保持したまま、内部ノードのみ更新
    for (uint level = tree_depth; level > 0; level--)
    {
        uint level_start = 1u << (level - 1);
        uint level_count = level_start;
        
        for (uint i = 0; i < level_count; i++)
        {
            uint node_index = level_start + i;
            uint left_child = node_index << 1;      // node_index * 2 をビットシフトで高速化
            uint right_child = left_child + 1;
            
            uint sum = 0;
            
            if (level == tree_depth)
            {
                // リーフレベル：32bit uintビットフィールドのビットカウント
                // リーフの値は変更せず、カウントのみ計算
                if (left_child < leaf_offset + leaf_count)
                    sum += CountBits32(cbt_buffer[left_child]);
                if (right_child < leaf_offset + leaf_count)
                    sum += CountBits32(cbt_buffer[right_child]);
            }
            else
            {
                // 内部ノード：子ノードの合計値を使用
                sum = cbt_buffer_rw[left_child] + cbt_buffer_rw[right_child];
            }
            
            // 内部ノードのみ更新（リーフは変更しない）
            cbt_buffer_rw[node_index] = sum;
        }
    }
}
