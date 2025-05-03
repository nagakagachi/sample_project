#pragma once

#include "math/math.h"
#include "text/hash_text.h"

namespace ngl::render::task
{
    // NGL側のディレクトリのシェーダファイルパスを有効なパスにする.
    static constexpr char k_shader_path_base[] = "../ngl/shader/";
    #define NGL_RENDER_TASK_SHADER_PATH(shader_file) text::FixedString<128>("%s/%s", ngl::render::task::k_shader_path_base, shader_file)

    static constexpr char k_shader_model[] = "6_3";

    
    // Pass用のView情報.
    struct RenderPassViewInfo
    {
        ngl::math::Vec3		camera_pos = {};
        ngl::math::Mat33	camera_pose = ngl::math::Mat33::Identity();
        float				near_z{};
        float				far_z{};
        float				aspect_ratio{};
        float				camera_fov_y = ngl::math::Deg2Rad(60.0f);
    };
    
}
