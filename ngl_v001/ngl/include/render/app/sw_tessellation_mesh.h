/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "render/scene/scene_mesh.h"

namespace ngl::render::app
{

    class SwTessellationMesh : public ngl::gfx::scene::SceneMesh
    {
        using Super = ngl::gfx::scene::SceneMesh;
    public:
        SwTessellationMesh() = default;
        ~SwTessellationMesh() { Finalize(); }

        // 初期化
        bool Initialize(
            ngl::rhi::DeviceDep* p_device,
            ngl::fwk::GfxScene* gfx_scene,
            const ngl::res::ResourceHandle<ngl::gfx::ResMeshData>& res_mesh)
        {
            
            // 専用にマテリアル指定.
            constexpr text::HashText<64> attrles_material_name = "opaque_attrless";

            if (!Super::Initialize(p_device, gfx_scene, res_mesh, attrles_material_name.Get()))
            {
                assert(false);
                return false;
            }

            // AttrLess用のDrawShape関数オーバーライド.
            SetProceduralDrawShapeFunc(
                [this](ngl::rhi::GraphicsCommandListDep* p_command_list, int shape_index)
                {
                    //
                    p_command_list->SetPrimitiveTopology(ngl::rhi::EPrimitiveTopology::TriangleList);
                    p_command_list->DrawInstanced(6, 1, 0, 0);
                });

            return true;
        }

    private:
    };

}  // namespace ngl::gfx::scene
