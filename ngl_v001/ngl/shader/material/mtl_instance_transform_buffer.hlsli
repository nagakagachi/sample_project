#ifndef NGL_SHADER_MESH_TRANSFORM_BUFFER_H
#define NGL_SHADER_MESH_TRANSFORM_BUFFER_H

// nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
#pragma pack_matrix( row_major )


struct NglInstanceInfo
{
    float3x4 mtx;
    float3x4 mtx_cofactor;// 法線変換用余因子行列. Align用にvec4*3としている. https://github.com/graphitemaster/normals_revisited .
};
// 後でCBではなく巨大なTransformBufferにするかも.
ConstantBuffer<NglInstanceInfo> ngl_cb_instance;


float3x4 NglGetInstanceTransform(int instance_id)
{
    // 現状はCBだが, 後でIDから巨大なBuffer上の情報を取得する形式にしたい.
    return ngl_cb_instance.mtx;
}
float3x4 NglGetInstanceTransformCofactor(int instance_id)
{
    // 現状はCBだが, 後でIDから巨大なBuffer上の情報を取得する形式にしたい.
    return ngl_cb_instance.mtx_cofactor;
}


#endif


