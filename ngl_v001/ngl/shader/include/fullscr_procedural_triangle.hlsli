#ifndef NGL_SHADER_FULLSCR_PROCEDURAL_TRIANGLE_H
#define NGL_SHADER_FULLSCR_PROCEDURAL_TRIANGLE_H

#if !defined(NGL_SHADER_FULLSCR_PROCEDURAL_TRIANGLE_Z)
#define NGL_SHADER_FULLSCR_PROCEDURAL_TRIANGLE_Z 0.0
#endif

#if 0

頂点入力無しで深度値NGL_SHADER_FULLSCR_PROCEDURAL_TRIANGLE_Zのフルスクリーン描画Triangleを出力するVS.
PrimitiveTopology::TriangleList
として
DrawInstanced(3, 1, 0, 0);
によって計算でフルスクリーン描画用Triangleを描画する

 --------------
  ＼    |     |
    ＼  |     |
      ＼|_____|
        ＼    |
          ＼  |
            ＼|

#endif


struct VS_OUTPUT
{
    float4 pos	:	SV_POSITION;
    float2 uv	:	TEXCOORD0;
};

VS_OUTPUT main_vs(uint sv_vtx_id : SV_VertexID)
{
    VS_OUTPUT output = (VS_OUTPUT)0;

    const float2 out_pos[] =
    {
        float2(-3.0, 1.0),
        float2(1.0, 1.0),
        float2(1.0, -3.0),
    };

    const float2 out_uv[] =
    {
        float2(-1.0, 0.0),
        float2(1.0, 0.0),
        float2(1.0, 2.0),
    };

    const uint v_id = sv_vtx_id & 0x3;

    output.pos = float4(out_pos[v_id], NGL_SHADER_FULLSCR_PROCEDURAL_TRIANGLE_Z, 1.0);
    output.uv = out_uv[v_id];

    return output;
}

#endif