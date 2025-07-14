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
    uint bs_depth;
    uint bs_index;

    int next;
    int prev;
    int twin;

    uint command;
    uint alloc_ptr[4];
};


#endif // NGL_SHADER_TESS_BISECTOR_H