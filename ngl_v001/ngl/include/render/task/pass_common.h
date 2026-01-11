/*
    共通パス定義.
*/

#pragma once

#include "math/math.h"
#include "text/hash_text.h"

namespace ngl::render::task
{   
    // Pass用のView情報.
    struct RenderPassViewInfo
    {
        ngl::math::Vec3		camera_pos = {};
        ngl::math::Mat33	camera_pose = ngl::math::Mat33::Identity();
        float				near_z{};
        float				far_z{};
        float				aspect_ratio{};
        float				camera_fov_y = ngl::math::Deg2Rad(60.0f);

        ngl::math::Mat34    view_mat = ngl::math::Mat34::Identity();
        ngl::math::Mat44    proj_mat = ngl::math::Mat44::Identity();
        ngl::math::Vec4     ndc_z_to_view_z_coef = {};
    };
    
}
