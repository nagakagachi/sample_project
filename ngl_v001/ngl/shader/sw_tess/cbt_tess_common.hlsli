// cbt_tess_common.hlsli
/*
    CBT (Concurrent Binary Tree) ベースソフトウェアテッセレーション システム
    
    【CBT仕様概要】
    - 完全二分木でBisectorプールの使用状況を管理
    - リーフノードは32bit uintビットフィールドで32要素分の使用状況を1ワードで管理
    - インデックス0は未使用、インデックス1がルートノード
    - 左の子: parent_index << 1, 右の子: (parent_index << 1) + 1
    - ノード総数: 1 << (cbt_tree_depth + 1)
    - リーフ数: 1 << cbt_tree_depth
    - リーフオフセット: 1 << cbt_tree_depth (ルートから見たリーフ開始位置)
    
    【CBT操作アルゴリズム】
    - Sum Reduction: 各ノードは子ノードの合計値を保持（ボトムアップ集計）
    - 二分探索: i番目の使用中/未使用ビット位置を高速検索（O(log n)）
    - アトミック操作: SetCBTBit での排他制御（InterlockedOr/And使用）
    - ビット位置計算: bit_position = leaf_offset + (index / 32), bit_mask = 1 << (index % 32)
    
    【メモリレイアウト】
    cbt_buffer構造例（depth=3, 32bit uint リーフの場合）:
    [0=unused] [1=root] [2,3=level1] [4,5,6,7=level2] [8,9,10,11,12,13,14,15=leaves(32bit each)]
    各リーフは32ビットフィールドでBisector使用状況を管理（1リーフ = 32 Bisector分）
    
    【バッファ設計方針】
    - Buffer/RWBuffer: プリミティブ型(uint, int2等)用、レジスタ指定不要
    - StructuredBuffer/RWStructuredBuffer: 複合型(Bisector等)用
    - 定数バッファ最適化: 計算可能なパラメータは動的算出（メモリ帯域節約）
    - _rw サフィックス: UAV（書き込み可能）バッファの命名規則


    【パフォーマンス考慮事項】
    - Wave intrinsics対応: WaveActiveSum等でSIMD最適化可能
    - Cache locality: リーフレベルでの連続アクセスパターン
    - Divergence回避: ビット操作での分岐最小化
    
    【デバッグ支援】
    - frame_index: フレーム番号でデバッグ時の状態追跡
    - アサーション: bit_position < bisector_pool_max_size
    - 境界チェック: リーフインデックス範囲検証
    
    【リソース詳細とライフサイクル】

    cbt_buffer / cbt_buffer_rw : CBT完全二分木のノード配列（uint型）
                ライフサイクル: CPU初期化 → GPU SumReduction更新 → GPU二分探索読み取り
                リーフがbitfieldであるような完全二分木のノードとリーフを含むuintバッファ
                後述するBisectorPoolの各要素の使用中/未使用の管理を1ビットで行う
                SumReductionと二分探索によってi番目の使用中ビットの位置や, i番目の未使用ビットの位置を高速に検索します
                CPU側で初期化時に割り当てた最大サイズのプールを管理する分の木のノードとリーフがフラットに並んだバッファ
                リーフをuintビットフィールドにすることで省メモリと高速化を目的とする.
                書き込み用途の場合のUAVは末尾に rw をつけた名前にする.
    
    bisector_pool / bisector_pool_rw : テッセレーションBisector構造体配列
                ライフサイクル: CPU事前確保 → GPU動的割り当て/解放 → GPU細分化処理
                テッセレーションで細分化された三角形を表現するBisectorプールバッファ
                CPU側で初期化時に割り当てた最大サイズで運用される

    index_cache / index_cache_rw : インデックス解決キャッシュ（int2型）
                ライフサイクル: 毎フレーム cache_index.hlsl で更新 → 各段階で高速アクセス
                int2で xにはi番目の使用中Bisectorインデックス, yにはi番目の未使用Bisectorインデックスをキャッシュする
                割り当て済みのBisectorに対する処理や, 未使用Bisectorに対する処理の際のインデックス解決を高速にするため, cbt_bufferを使って毎フレーム更新される

    alloc_counter / alloc_counter_rw : 新規割り当てカウンタ（uint型）
                ライフサイクル: フレーム開始時リセット → 割り当て時アトミック増分
                Tessellation処理中に新規Bisector割り当てをする際のカウンタ管理 uintひとつだけでatomic操作される

    indirect_dispatch_arg_for_bisector : Bisector処理用Dispatch引数バッファ（uint3型）
                ライフサイクル: begin_update.hlsl で更新 → 後続DispatchIndirect呼び出し
                割り当て済みBisectorの数だけDispatchするためのIndirect命令引数バッファ.
                begin_updateでcbt_bufferに格納されている有効Bisector総数を使って更新される
                
    indirect_dispatch_arg_for_index_cache : インデックスキャッシュ更新用Dispatch引数バッファ（uint3型）
                ライフサイクル: begin_update.hlsl で更新 → cache_index.hlsl のDispatchIndirect
                index_cacheの更新をするために必要なThreadwoDIspatchするためのIndirect命令引数バッファ.
                begin_updateでcbt_bufferに格納されている有効Bisector総数と, プール最大サイズ-有効Bisector総数 のうち大きい方を使って更新される
                
    【実装ガイドライン】
    1. CBT操作時は必ずGetCBT*()ヘルパー関数を使用
    2. ビット操作前にbit_position < bisector_pool_max_sizeをチェック
    3. アトミック操作はSetCBTBit()経由で統一
    4. FindIthBit*InCBT()は戻り値-1で「見つからない」を示す
    5. Sum Reduction更新後は必ずGroupMemoryBarrierWithGroupSync()
    6. 範囲チェックによるスキップは可能な限り早期リターン（return）の形にする
    　　　　　　　　 
*/

// CBTテッセレーション共通定数定義
#define CBT_THREAD_GROUP_SIZE 128   // 標準スレッドグループサイズ（1スレッド実行する特殊なもの以外で使用）

// Bisector Command ビットマスク定数
// 分割コマンド (3ビット)
#define BISECTOR_CMD_TWIN_SPLIT     (1 << 0)    // Twin分割
#define BISECTOR_CMD_PREV_SPLIT     (1 << 1)    // Prev分割
#define BISECTOR_CMD_NEXT_SPLIT     (1 << 2)    // Next分割

// 分割コマンド統合マスク（3つの分割コマンドのOR）
#define BISECTOR_CMD_ANY_SPLIT      (BISECTOR_CMD_TWIN_SPLIT | BISECTOR_CMD_PREV_SPLIT | BISECTOR_CMD_NEXT_SPLIT)

// 統合コマンド (4ビット, bit3-6)
#define BISECTOR_CMD_BOUNDARY_MERGE     (1 << 3)    // 境界統合
#define BISECTOR_CMD_INTERIOR_MERGE     (1 << 4)    // 非境界統合
#define BISECTOR_CMD_MERGE_REPRESENTATIVE   (1 << 5)    // 統合代表ビット
#define BISECTOR_CMD_MERGE_CONSENT      (1 << 6)    // 統合同意ビット
    

#include "bisector.hlsli"

// CBT操作定数（32bit uint リーフ特化）
#define CBT_UINT32_BIT_WIDTH 32
#define CBT_UINT32_BIT_MASK 31

// 共通定数バッファ
cbuffer CBTTessellationConstants
{
    uint cbt_tree_depth;                // CBTの木の深さ (log2(leaf_count))
    uint cbt_mesh_minimum_tree_depth;   // オリジナルメッシュ表現の最小深度（Bisectorのdepthオフセット）
    uint bisector_pool_max_size;        // Bisectorプールの最大サイズ
    uint total_half_edges;              // 初期化すべきHalfEdge総数

    int fixed_subdivision_level;       // 固定分割レベル（-1で無効、0以上で固定分割）
    float tessellation_split_threshold; // テッセレーション分割閾値
    float tessellation_merge_factor;    // テッセレーション統合係数 (0.0~1.0, 分割閾値に対する比率)
    uint debug_mode_int;                       // 16byte alignment（C++側CBTConstantsと対応）

    float3x4 object_to_world;           // オブジェクト空間からワールド空間への変換行列
    float3x4 world_to_object;           // ワールド空間からオブジェクト空間への変換行列
    float3 important_point;             // テッセレーション評価で重視する座標（ワールド空間）

    uint padding0;
};

// CBT計算ヘルパー関数
uint GetCBTLeafCount()
{
    // リーフのビットフィールド数 = bisector_pool_max_sizeを32bitフィールドで表現するのに必要な数
    //return (bisector_pool_max_size + 31) / 32;  // ceil(bisector_pool_max_size / 32)
    return 1 << cbt_tree_depth;
}

uint GetCBTLeafOffset()
{
    return 1u << cbt_tree_depth;  // リーフ開始位置（内部ノード数 + 1）
}

uint GetCBTTotalNodeCount()
{
    // 内部ノード数（インデックス1から2^cbt_tree_depth - 1） + リーフビットフィールド数
    uint internal_node_count = (1u << cbt_tree_depth) - 1;  // 2^cbt_tree_depth - 1
    uint leaf_bitfield_count = GetCBTLeafCount();
    return internal_node_count + leaf_bitfield_count + 1;  // +1 for index 0 (unused)
}

// CBTルートノード値取得（インデックス1ベースを強調）
uint GetCBTRootValue(Buffer<uint> cbt)
{
    return cbt[1]; // CBTルートは常にインデックス1
}

uint GetCBTRootValue(RWBuffer<uint> cbt)
{
    return cbt[1]; // CBTルートは常にインデックス1
}

// リソースバインディング
Buffer<uint> cbt_buffer;
RWBuffer<uint> cbt_buffer_rw;

StructuredBuffer<Bisector> bisector_pool;
RWStructuredBuffer<Bisector> bisector_pool_rw;

StructuredBuffer<HalfEdge> half_edge_buffer;
StructuredBuffer<float3> vertex_position_buffer;  // 頂点座標バッファ

Buffer<int2> index_cache;
RWBuffer<int2> index_cache_rw;

Buffer<uint> alloc_counter;
RWBuffer<uint> alloc_counter_rw;

RWBuffer<uint> indirect_dispatch_arg_for_bisector;
RWBuffer<uint> indirect_dispatch_arg_for_index_cache;

RWBuffer<uint> draw_indirect_arg;

// CBT基本操作関数（32bit uint リーフ特化）
uint GetCBTLeafIndex(uint bit_position)
{
    return bit_position >> 5; // bit_position / 32 をビットシフトで高速化
}

uint GetCBTBitMask(uint bit_position)
{
    return 1u << (bit_position & CBT_UINT32_BIT_MASK);
}

void SetCBTBit(RWBuffer<uint> cbt, uint bit_position, uint value)
{
    uint leaf_index = GetCBTLeafOffset() + GetCBTLeafIndex(bit_position);
    uint bit_mask = GetCBTBitMask(bit_position);
    
    if (value != 0)
    {
        InterlockedOr(cbt[leaf_index], bit_mask);
    }
    else
    {
        InterlockedAnd(cbt[leaf_index], ~bit_mask);
    }
}

uint GetCBTBit(Buffer<uint> cbt, uint bit_position)
{
    uint leaf_index = GetCBTLeafOffset() + GetCBTLeafIndex(bit_position);
    uint bit_mask = GetCBTBitMask(bit_position);
    return (cbt[leaf_index] & bit_mask) != 0 ? 1 : 0;
}
uint GetCBTBit(RWBuffer<uint> cbt, uint bit_position)
{
    uint leaf_index = GetCBTLeafOffset() + GetCBTLeafIndex(bit_position);
    uint bit_mask = GetCBTBitMask(bit_position);
    return (cbt[leaf_index] & bit_mask) != 0 ? 1 : 0;
}

// ビットカウント関数
uint CountBits32(uint value)
{
    // ハミング重み計算
    value = value - ((value >> 1) & 0x55555555);
    value = (value & 0x33333333) + ((value >> 2) & 0x33333333);
    return (((value + (value >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

// CBT検索関数 - i番目の1ビットの位置を検索（完全二分木ビットシフト最適化版）
int FindIthBit1InCBT(Buffer<uint> cbt, uint target_index)
{
    uint bit_id = 1; // ルートから開始

    if(GetCBTRootValue(cbt) <= target_index)
    {
        return -1; // 総数より大きい時点で無効.
    }

    // リーフノードは実際にはパッキングされているため, リーフの一つ上までを探索.
    while(bit_id < (1 << (cbt_tree_depth - 1)))
    {
        bit_id = bit_id << 1; // bit_id * 2 をビットシフトで高速化
        if(target_index >= cbt[bit_id])
        {
            target_index = target_index - cbt[bit_id];
            bit_id = bit_id + 1; // 右の子に移動
        }
    }
    // 最後の2bitから探索.
    int even_bit_index = (bit_id << 1) - GetCBTLeafCount();
    return (GetCBTBit(cbt, even_bit_index) == 0)? even_bit_index + 1 : even_bit_index + target_index;
}

// CBT検索関数 - i番目の0ビットの位置を検索（完全二分木ビットシフト最適化版）
int FindIthBit0InCBT(Buffer<uint> cbt, uint target_index)
{
    uint bit_id = 1; // ルートから開始
    uint c = 1 << (cbt_tree_depth - 1);

    if((1 << cbt_tree_depth) - GetCBTRootValue(cbt) <= target_index)
    {
        return -1; // 総数より大きい時点で無効.
    }
    
    // リーフノードは実際にはパッキングされているため, リーフの一つ上までを探索.
    while(bit_id < (1 << (cbt_tree_depth - 1)))
    {
        bit_id = bit_id << 1; // bit_id * 2 をビットシフトで高速化
        if(target_index >= (c - cbt[bit_id]))
        {
            target_index = target_index - (c - cbt[bit_id]);
            bit_id = bit_id + 1;
        }
        c = c >> 1;
    }
    // 最後の2bitから探索.
    int even_bit_index = (bit_id << 1) - GetCBTLeafCount();
    return (GetCBTBit(cbt, even_bit_index) == 0)? even_bit_index + target_index : even_bit_index + 1;
}

// Bisectorから元のRootBisectorインデックスを取得する関数
// bs_id >> (bs_depth - minimum_tree_depth) でRootBisectorインデックスを算出
uint GetRootBisectorIndex(uint bisector_id, uint bisector_depth)
{
    // BisectorIDから元のRootBisectorインデックスを計算
    // 深度差分だけ右シフトすることで、細分化前の元インデックスを取得
    uint depth_shift = bisector_depth - cbt_mesh_minimum_tree_depth;
    return bisector_id >> depth_shift;
}

// Bisector階層構造操作関数

// 分割時の最初の子Bisector情報を計算
// 二つ目の子のIDは returned_id + 1 で取得可能
uint2 CalcFirstChildBisectorInfo(uint parent_id, uint parent_depth)
{
    uint child_depth = parent_depth + 1;
    uint child_id = parent_id << 1;  // parent_id * 2 をビットシフトで高速化
    return uint2(child_id, child_depth);
}

// 統合時の親Bisector情報を計算
uint2 CalcParentBisectorInfo(uint child_id, uint child_depth)
{
    uint parent_depth = child_depth - 1;
    uint parent_id = child_id >> 1;  // child_id / 2 をビットシフトで高速化
    return uint2(parent_id, parent_depth);
}

// RootBisectorの基本頂点情報を取得
int3 CalcRootBisectorBaseVertex(uint bisector_id, uint bisector_depth)
{
    // RootBisectorインデックスを取得
    uint root_index = GetRootBisectorIndex(bisector_id, bisector_depth);
    
    // 対応するHalfEdgeを取得
    HalfEdge half_edge = half_edge_buffer[root_index];
    HalfEdge next_edge = half_edge_buffer[half_edge.next];
    HalfEdge prev_edge = half_edge_buffer[half_edge.prev];
    
    // curr、next、prevの順番で頂点番号を返す
    return int3(half_edge.vertex, next_edge.vertex, prev_edge.vertex);
}

// Bisectorの頂点属性補間マトリックスを計算
// 行に頂点の属性を配置した行列に対して左から乗算することで
// Bisectorの頂点属性を得るための行列
float3x3 CalcBisectorAttributeMatrix(uint bisector_id, uint bisector_depth)
{
    // 初期マトリックス（単位行列 + 重心座標）
    float3x3 m = float3x3(
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        1.0/3.0, 1.0/3.0, 1.0/3.0
    );
    
    // minimum_tree_depthより深い階層を遡って処理
    while (bisector_depth > cbt_mesh_minimum_tree_depth)
    {
        // 最下位ビットに基づいて変換マトリックスを選択
        if (bisector_id & 1) // 最下位ビットが1の場合
        {
            m = mul(float3x3(
                1.0, 0.0, 0.0,
                0.0, 0.0, 1.0,
                0.5, 0.5, 0.0
            ), m);
        }
        else // 最下位ビットが0の場合
        {
            m = mul(float3x3(
                0.0, 0.0, 1.0,
                0.0, 1.0, 0.0,
                0.5, 0.5, 0.0
            ), m);
        }
        
        // 次の階層へ
        bisector_id = bisector_id >> 1;
        bisector_depth = bisector_depth - 1;
    }
    
    return m;
}
