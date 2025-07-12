#pragma once

#include "gfx_scene_entity.h"

#include "rhi/d3d12/resource_view.d3d12.h"
#include "resource/resource_manager.h"

#include "gfx/render/standard_render_model.h"

namespace ngl::fwk
{

    class GfxSceneEntityMeshProxy
    {
    public:
        math::Mat34                 transform_ = math::Mat34::Identity();
        gfx::StandardRenderModel*	model_ = {};
    };

    // Mesh描画情報をGfxへ通知するためのクラス. Proxyと対になる.
    class GfxSceneEntityMesh : public GfxSceneEntityBase<GfxSceneEntityMesh, GfxSceneEntityMeshProxy>
    {
    public:
    };
    
}