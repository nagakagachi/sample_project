/*
    ngl_shader_config.hlsli
    共通設定HLSLI.
*/

#ifndef NGL_SHADER_CONFIG_H
#define NGL_SHADER_CONFIG_H

#if !defined(NGL_SHADER_CPP_INCLUDE)
    // Shader Code.

    // nglのmatrix系ははrow-majorメモリレイアウトであるための指定.
    #pragma pack_matrix( row_major )
    
    #define NGL_CPP_MEMBER_INIT(v)
#else
    // C++ Code.
    #define NGL_CPP_MEMBER_INIT(v) v
#endif // !defined(NGL_SHADER_CPP_INCLUDE)

#endif // NGL_SHADER_CONFIG_H