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


// CBT SumReduction : Single Thread Naive Implementation
[numthreads(1, 1, 1)]
void main_cs(
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    if (DTid.x != 0) return;
    
    for (uint level = cbt_tree_depth; level > 0; level--)
    {
        uint level_start = 1u << (level - 1);
        uint level_count = level_start;
        
        for (uint i = 0; i < level_count; i++)
        {
            uint node_index = level_start + i;
            uint left_child = node_index << 1;
            uint right_child = left_child + 1;
            
            uint sum = 0;
            if (level == cbt_tree_depth)
                sum += GetCBTBit(cbt_buffer_rw, (i << 1)) + GetCBTBit(cbt_buffer_rw, (i << 1)+1);
            else
                sum = cbt_buffer_rw[left_child] + cbt_buffer_rw[right_child];
            
            cbt_buffer_rw[node_index] = sum;
        }
    }
}
