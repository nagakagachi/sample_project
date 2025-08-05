#ifndef NGL_SHADER_TESS_BISECTOR_H
#define NGL_SHADER_TESS_BISECTOR_H

// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )

struct HalfEdge
{
    int twin   ;
    int next   ;
    int prev   ;
    int vertex ;
};

#define BISECTOR_ALLOC_PTR_SIZE 4
struct Bisector
{
    uint bs_depth;    // Bisectorの所属深さ, BIsectorの子の深さは bs_depth+1 である.
    uint bs_id;       // BisectorのID. Bisectorの子のIDは  bs_id*2, 及び bs_id*2+1 である.
    uint command;     // generate_command.hlsl で生成される, このBisectorに対する分割/統合を指示するコマンドビットフィールド.
    int  next;        // HalfEdgeと同様に自身からみてNextの関係にある隣接Bisectorのインデックス (16 bytes total)

    int prev;         // HalfEdgeと同様に自身からみてPrevの関係にある隣接Bisectorのインデックス
    int twin;         // HalfEdgeと同様に自身からみてTWINの関係にある隣接Bisectorのインデックス
    int alloc_ptr[BISECTOR_ALLOC_PTR_SIZE]; // generate_command.hlsl で生成されたコマンドをreserve_blockで評価して実際に割り当てた新規Bisectorへのインデックスを保持する (16 bytes total)
    
    float debug_value; // デバッグ用: GenerateCommandで計算された分割評価値
    uint  padding1, padding2, padding3; // 16byteアライメント調整用
};

// Bisector初期化関数
void ResetBisector(inout Bisector bisector, uint bisector_id, uint bisector_depth)
{
    bisector.bs_depth = bisector_depth;
    bisector.bs_id = bisector_id;
    bisector.command = 0;
    bisector.next = -1;
    bisector.prev = -1;
    bisector.twin = -1;
    
    // alloc_ptrを全て-1で初期化
    for(int i = 0; i < BISECTOR_ALLOC_PTR_SIZE; ++i)
    {
        bisector.alloc_ptr[i] = -1;
    }
    
    bisector.debug_value = 0.0f;
    bisector.padding1 = 0;
    bisector.padding2 = 0;
    bisector.padding3 = 0;
}


#endif // NGL_SHADER_TESS_BISECTOR_H