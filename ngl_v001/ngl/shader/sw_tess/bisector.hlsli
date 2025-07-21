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


struct Bisector
{
    uint bs_depth;    // Bisectorの所属深さ, BIsectorの子の深さは bs_depth+1 である.
    uint bs_id;       // BisectorのID. Bisectorの子のIDは  bs_id*2, 及び bs_id*2+1 である.
    uint command;     // generate_command.hlsl で生成される, このBisectorに対する分割/統合を指示するコマンドビットフィールド.
    int  next;        // HalfEdgeと同様に自身からみてNextの関係にある隣接Bisectorのインデックス (16 bytes total)

    int prev;         // HalfEdgeと同様に自身からみてPrevの関係にある隣接Bisectorのインデックス
    int twin;         // HalfEdgeと同様に自身からみてTWINの関係にある隣接Bisectorのインデックス
    int alloc_ptr[4]; // generate_command.hlsl で生成されたコマンドをreserve_blockで評価して実際に割り当てた新規Bisectorへのインデックスを保持する (16 bytes total)
};


#endif // NGL_SHADER_TESS_BISECTOR_H