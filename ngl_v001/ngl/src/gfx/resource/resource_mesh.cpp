
#include "gfx/resource/resource_mesh.h"

#include "resource/resource_manager.h"

namespace ngl
{
namespace gfx
{
    // 生成用.
    auto CreateShapeDataRhiBuffer(
        ngl::gfx::MeshShapeGeomBufferBase* p_mesh_geom_buffer,

        ngl::rhi::ShaderResourceViewDep* p_out_view,
        ngl::rhi::VertexBufferViewDep* p_out_vbv,
        ngl::rhi::IndexBufferViewDep* p_out_ibv,

        ngl::rhi::DeviceDep* p_device,
        uint32_t bind_flag, rhi::EResourceFormat view_format, int element_size_in_byte, int element_count, void* initial_data = nullptr)
        -> bool
    {
        ngl::rhi::BufferDep::Desc buffer_desc = {};
        // 高速化のため描画用のバッファをDefaultHeapにしてUploadBufferからコピーする対応.
        buffer_desc.heap_type         = ngl::rhi::EResourceHeapType::Default;
        buffer_desc.initial_state     = ngl::rhi::EResourceState::Common;                        // DefaultHeapの場合?は初期ステートがGeneralだとValidationErrorとされるようになった.
        buffer_desc.bind_flag         = bind_flag | ngl::rhi::ResourceBindFlag::ShaderResource;  // Raytrace用のShaderResource.
        buffer_desc.element_count     = element_count;
        buffer_desc.element_byte_size = element_size_in_byte;

        p_mesh_geom_buffer->rhi_init_state_ = buffer_desc.initial_state;  // 初期ステート保存.
        // GPU側バッファ生成.
        if (!p_mesh_geom_buffer->rhi_buffer_->Initialize(p_device, buffer_desc))
        {
            assert(false);
            return false;
        }

        // Upload用Bufferが必要な場合は一時生成.
        rhi::RefBufferDep upload_buffer = {};
        if (initial_data)
        {
            upload_buffer.Reset(new rhi::BufferDep());

            auto upload_desc          = buffer_desc;
            upload_desc.heap_type     = ngl::rhi::EResourceHeapType::Upload;
            upload_desc.initial_state = ngl::rhi::EResourceState::General;  // UploadHeapはGenericRead.

            if (!upload_buffer->Initialize(p_device, upload_desc))
            {
                assert(false);
                return false;
            }

            // 初期データのコピー.
            if (void* mapped = upload_buffer->Map())
            {
                memcpy(mapped, initial_data, element_size_in_byte * element_count);
                upload_buffer->Unmap();
            }
        }

        rhi::BufferDep* p_buffer = p_mesh_geom_buffer->rhi_buffer_.Get();
        // Viewの生成. 引数で生成対象ポインタを指定された要素のみ.
        bool result = true;
        if (p_out_view)
        {
            if (!p_out_view->InitializeAsTyped(p_device, p_buffer, view_format, 0, p_buffer->getElementCount()))
            {
                assert(false);
                result = false;
            }
        }
        if (p_out_vbv)
        {
            rhi::VertexBufferViewDep::Desc vbv_desc = {};
            if (!p_out_vbv->Initialize(p_buffer, vbv_desc))
            {
                assert(false);
                result = false;
            }
        }
        if (p_out_ibv)
        {
            rhi::IndexBufferViewDep::Desc ibv_desc = {};
            if (!p_out_ibv->Initialize(p_buffer, ibv_desc))
            {
                assert(false);
                result = false;
            }
        }

        // UploadBufferからコピーするコマンドを発行. 適切に遅延破棄させるため個々のRefをコピーキャプチャ.
        auto req_buffer = p_mesh_geom_buffer->rhi_buffer_;
        auto req_init_state = p_mesh_geom_buffer->rhi_init_state_;
        fwk::PushCommonRenderCommand([upload_buffer, req_buffer, req_init_state](fwk::CommonRenderCommandArgRef arg)
        {
            arg.command_list->GetDevice();
            auto p_commandlist = arg.command_list;

            // バッファ自体が生成されていなければ終了.
            if (!upload_buffer.IsValid() || !req_buffer->GetD3D12Resource())
                return;

            auto* p_d3d_commandlist = p_commandlist->GetD3D12GraphicsCommandList();

            // Init Upload Buffer から DefaultHeapのBufferへコピー. StateはGeneral想定.
            // 生成時のロジックで指定したステート. Bufferの初期ステートはDefaultHeapの場合?はCommonでないとValidationErrorとされるようになったので注意.
            const rhi::EResourceState buffer_state = req_init_state;

            auto ref_buffer = req_buffer;
            p_commandlist->ResourceBarrier(ref_buffer.Get(), buffer_state, rhi::EResourceState::CopyDst);
            p_d3d_commandlist->CopyResource(ref_buffer->GetD3D12Resource(), upload_buffer->GetD3D12Resource());
            p_commandlist->ResourceBarrier(ref_buffer.Get(), rhi::EResourceState::CopyDst, buffer_state);

            // upload bufferは一時バッファとして作ってRefとしているので自動的に解放される.
        });

        return result;
    }

    void MeshShapePart::Initialize(rhi::DeviceDep* p_device, const MeshShapeInitializeSourceData& init_source_data)
    {
        // Slotマッピングクリア.
        p_vtx_attr_mapping_.fill(nullptr);
        vtx_attr_mask_ = {};

        num_vertex_ = init_source_data.num_vertex_;
        num_primitive_ = init_source_data.num_primitive_;

        // Vertex Attribute.
        {
            position_.raw_ptr_ = init_source_data.position_;

            CreateShapeDataRhiBuffer(
                &position_,
                position_.rhi_srv.Get(),
                position_.rhi_vbv_.Get(),
                nullptr,
                p_device, ngl::rhi::ResourceBindFlag::VertexBuffer, rhi::EResourceFormat::Format_R32G32B32_FLOAT, sizeof(ngl::math::Vec3), num_vertex_,
                position_.raw_ptr_);

            // Slotマッピング.
            p_vtx_attr_mapping_[gfx::MeshVertexSemantic::SemanticSlot(gfx::EMeshVertexSemanticKind::POSITION)] = &position_;
            vtx_attr_mask_.AddSlot(gfx::EMeshVertexSemanticKind::POSITION);
        }

        if (init_source_data.normal_)
        {
            normal_.raw_ptr_ = init_source_data.normal_;

            CreateShapeDataRhiBuffer(
                &normal_,
                normal_.rhi_srv.Get(),
                normal_.rhi_vbv_.Get(),
                nullptr,
                p_device, ngl::rhi::ResourceBindFlag::VertexBuffer, rhi::EResourceFormat::Format_R32G32B32_FLOAT, sizeof(ngl::math::Vec3), num_vertex_,
                normal_.raw_ptr_);

            rhi::VertexBufferViewDep::Desc vbv_desc = {};
            normal_.rhi_vbv_->Initialize(normal_.rhi_buffer_.Get(), vbv_desc);

            // Slotマッピング.
            p_vtx_attr_mapping_[gfx::MeshVertexSemantic::SemanticSlot(gfx::EMeshVertexSemanticKind::NORMAL)] = &normal_;
            vtx_attr_mask_.AddSlot(gfx::EMeshVertexSemanticKind::NORMAL);
        }
        if (init_source_data.tangent_)
        {
            tangent_.raw_ptr_ = init_source_data.tangent_;

            CreateShapeDataRhiBuffer(
                &tangent_,
                tangent_.rhi_srv.Get(),
                tangent_.rhi_vbv_.Get(),
                nullptr,
                p_device, ngl::rhi::ResourceBindFlag::VertexBuffer, rhi::EResourceFormat::Format_R32G32B32_FLOAT, sizeof(ngl::math::Vec3), num_vertex_,
                tangent_.raw_ptr_);

            rhi::VertexBufferViewDep::Desc vbv_desc = {};
            tangent_.rhi_vbv_->Initialize(tangent_.rhi_buffer_.Get(), vbv_desc);

            // Slotマッピング.
            p_vtx_attr_mapping_[gfx::MeshVertexSemantic::SemanticSlot(gfx::EMeshVertexSemanticKind::TANGENT)] = &tangent_;
            vtx_attr_mask_.AddSlot(gfx::EMeshVertexSemanticKind::TANGENT);
        }
        if (init_source_data.binormal_)
        {
            binormal_.raw_ptr_ = init_source_data.binormal_;

            CreateShapeDataRhiBuffer(
                &binormal_,
                binormal_.rhi_srv.Get(),
                binormal_.rhi_vbv_.Get(),
                nullptr,
                p_device, ngl::rhi::ResourceBindFlag::VertexBuffer, rhi::EResourceFormat::Format_R32G32B32_FLOAT, sizeof(ngl::math::Vec3), num_vertex_,
                binormal_.raw_ptr_);

            rhi::VertexBufferViewDep::Desc vbv_desc = {};
            binormal_.rhi_vbv_->Initialize(binormal_.rhi_buffer_.Get(), vbv_desc);

            // Slotマッピング.
            p_vtx_attr_mapping_[gfx::MeshVertexSemantic::SemanticSlot(gfx::EMeshVertexSemanticKind::BINORMAL)] = &binormal_;
            vtx_attr_mask_.AddSlot(gfx::EMeshVertexSemanticKind::BINORMAL);
        }

        // SRGBかLinearで問題になるかもしれない. 現状はとりあえずLinear扱い.
        color_.resize(init_source_data.color_.size());
        for (int ci = 0; ci < init_source_data.color_.size(); ++ci)
        {
            color_[ci].raw_ptr_ = init_source_data.color_[ci];

            CreateShapeDataRhiBuffer(
                &color_[ci],
                color_[ci].rhi_srv.Get(),
                color_[ci].rhi_vbv_.Get(),
                nullptr,
                p_device, ngl::rhi::ResourceBindFlag::VertexBuffer, rhi::EResourceFormat::Format_R8G8B8A8_UNORM, sizeof(ngl::gfx::VertexColor), num_vertex_,
                color_[ci].raw_ptr_);

            rhi::VertexBufferViewDep::Desc vbv_desc = {};
            color_[ci].rhi_vbv_->Initialize(color_[ci].rhi_buffer_.Get(), vbv_desc);

            // Slotマッピング.
            p_vtx_attr_mapping_[gfx::MeshVertexSemantic::SemanticSlot(gfx::EMeshVertexSemanticKind::COLOR, ci)] = &color_[ci];
            vtx_attr_mask_.AddSlot(gfx::EMeshVertexSemanticKind::COLOR, ci);
        }
        texcoord_.resize(init_source_data.texcoord_.size());
        for (int ci = 0; ci < init_source_data.texcoord_.size(); ++ci)
        {
            texcoord_[ci].raw_ptr_ = init_source_data.texcoord_[ci];

            CreateShapeDataRhiBuffer(
                &texcoord_[ci],
                texcoord_[ci].rhi_srv.Get(),
                texcoord_[ci].rhi_vbv_.Get(),
                nullptr,
                p_device, ngl::rhi::ResourceBindFlag::VertexBuffer, rhi::EResourceFormat::Format_R32G32_FLOAT, sizeof(ngl::math::Vec2), num_vertex_,
                texcoord_[ci].raw_ptr_);

            rhi::VertexBufferViewDep::Desc vbv_desc = {};
            texcoord_[ci].rhi_vbv_->Initialize(texcoord_[ci].rhi_buffer_.Get(), vbv_desc);

            // Slotマッピング.
            p_vtx_attr_mapping_[gfx::MeshVertexSemantic::SemanticSlot(gfx::EMeshVertexSemanticKind::TEXCOORD, ci)] = &texcoord_[ci];
            vtx_attr_mask_.AddSlot(gfx::EMeshVertexSemanticKind::TEXCOORD, ci);
        }

        // Index.
        {
            index_.raw_ptr_ = init_source_data.index_;

            CreateShapeDataRhiBuffer(
                &index_,
                index_.rhi_srv.Get(),
                nullptr,
                index_.rhi_vbv_.Get(),
                p_device, ngl::rhi::ResourceBindFlag::IndexBuffer, rhi::EResourceFormat::Format_R32_UINT, sizeof(uint32_t), num_primitive_ * 3,
                index_.raw_ptr_);
        }
    }

    /*
        // Procedural MeshData生成.
        std::shared_ptr<ngl::gfx::MeshData> procedural_mesh_data = std::make_shared<ngl::gfx::MeshData>();
        {
            const float mesh_scale = 10.0f;
            ngl::math::Vec3 quad_pos[4] = {
                ngl::math::Vec3(-1.0f, 0.0f, -1.0f) * mesh_scale,
                ngl::math::Vec3(1.0f, 0.0f, -1.0f) * mesh_scale,
                ngl::math::Vec3(1.0f, 0.0f, 1.0f) * mesh_scale,
                ngl::math::Vec3(-1.0f, 0.0f, 1.0f) * mesh_scale
            };
            ngl::math::Vec3 quad_normal[4] = {
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f),
                ngl::math::Vec3(0.0f, 1.0f, 0.0f)
            };
            ngl::math::Vec3 quad_tangent[4] = {
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f),
                ngl::math::Vec3(1.0f, 0.0f, 0.0f)
            };
            ngl::math::Vec3 quad_binormal[4] = {
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f),
                ngl::math::Vec3(0.0f, 0.0f, 1.0f)
            };
            ngl::math::Vec2 quad_texcoord[4] = {
                ngl::math::Vec2(0.0f, 0.0f),
                ngl::math::Vec2(1.0f, 0.0f),
                ngl::math::Vec2(0.0f, 1.0f),
                ngl::math::Vec2(1.0f, 1.0f)
            };
            ngl::gfx::VertexColor quad_color[4] = {
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255},
                ngl::gfx::VertexColor{255, 255, 255, 255}
            };

            ngl::u32 index_data[6] = {
                0, 1, 2,
                0, 2, 3
            };

            ngl::gfx::MeshShapeInitializeSourceData init_source_data{};
            init_source_data.num_vertex_ = 4;
            init_source_data.num_primitive_ = 2;
            init_source_data.index_ = index_data;
            init_source_data.position_ = quad_pos;
            init_source_data.normal_ = quad_normal;
            init_source_data.tangent_ = quad_tangent;
            init_source_data.binormal_ = quad_binormal;
            init_source_data.texcoord_.push_back(quad_texcoord);
            init_source_data.color_.push_back(quad_color);

            // MeshData生成.
            GenerateMeshDataProcedural(*procedural_mesh_data, &device, init_source_data);
        }
    */
    void GenerateMeshDataProcedural(MeshData& out_mesh, rhi::DeviceDep* p_device, const MeshShapeInitializeSourceData& init_source_data)
    {
        const auto num_vtx = init_source_data.num_vertex_;
        const auto num_primitive = init_source_data.num_primitive_;

        int total_offset = 0;

        int position_offset = total_offset;
        total_offset += sizeof(*init_source_data.position_) * num_vtx;

        int normal_offset = 0;
        if(init_source_data.normal_)
        {
            normal_offset = total_offset;
            total_offset += sizeof(*init_source_data.normal_) * num_vtx;
        }
        int tangent_offset = 0;
        if(init_source_data.tangent_)
        {
            tangent_offset = total_offset;
            total_offset += sizeof(*init_source_data.tangent_) * num_vtx;
        }
        int binormal_offset = 0;
        if(init_source_data.binormal_)
        {
            binormal_offset = total_offset;
            total_offset += sizeof(*init_source_data.binormal_) * num_vtx;
        }
        std::vector<int> color_offsets;
        for (int ci = 0; ci < init_source_data.color_.size(); ++ci)
        {
            color_offsets.push_back(total_offset);
            total_offset += sizeof(*init_source_data.color_[ci]) * num_vtx;
        }
        std::vector<int> texcoord_offsets;
        for (int ci = 0; ci < init_source_data.texcoord_.size(); ++ci)
        {
            texcoord_offsets.push_back(total_offset);
            total_offset += sizeof(*init_source_data.texcoord_[ci]) * num_vtx;
        }

        int index_offset = 0;
        {
            index_offset = total_offset;
            total_offset += sizeof(*init_source_data.index_) * num_primitive * 3;
        }

        out_mesh.raw_data_mem_.resize(total_offset);

        // out_mesh.raw_data_mem_ にコピーしたデータでマッピングして初期化.
        MeshShapeInitializeSourceData init_source_data_copy{};
        {
            init_source_data_copy.num_vertex_ = num_vtx;
            init_source_data_copy.num_primitive_ = num_primitive;

            // Copy data to raw memory.
            if (init_source_data.position_)
            {
                init_source_data_copy.position_ = reinterpret_cast<math::Vec3*>(&out_mesh.raw_data_mem_[position_offset]);
                memcpy(init_source_data_copy.position_, init_source_data.position_, sizeof(*init_source_data.position_) * num_vtx);
            }
            if (init_source_data.normal_)
            {
                init_source_data_copy.normal_ = reinterpret_cast<math::Vec3*>(&out_mesh.raw_data_mem_[normal_offset]);
                memcpy(init_source_data_copy.normal_, init_source_data.normal_, sizeof(*init_source_data.normal_) * num_vtx);
            }
            if (init_source_data.tangent_)
            {
                init_source_data_copy.tangent_ = reinterpret_cast<math::Vec3*>(&out_mesh.raw_data_mem_[tangent_offset]);
                memcpy(init_source_data_copy.tangent_, init_source_data.tangent_, sizeof(*init_source_data.tangent_) * num_vtx);
            }
            if (init_source_data.binormal_)
            {
                init_source_data_copy.binormal_ = reinterpret_cast<math::Vec3*>(&out_mesh.raw_data_mem_[binormal_offset]);
                memcpy(init_source_data_copy.binormal_, init_source_data.binormal_, sizeof(*init_source_data.binormal_) * num_vtx);
            }
            for (int ci = 0; ci < init_source_data.color_.size(); ++ci)
            {
                init_source_data_copy.color_.push_back(reinterpret_cast<VertexColor*>(&out_mesh.raw_data_mem_[color_offsets[ci]]));
                memcpy(init_source_data_copy.color_.back(), init_source_data.color_[ci], sizeof(*init_source_data.color_[ci]) * num_vtx);
            }
            for (int ci = 0; ci < init_source_data.texcoord_.size(); ++ci)
            {
                init_source_data_copy.texcoord_.push_back(reinterpret_cast<math::Vec2*>(&out_mesh.raw_data_mem_[texcoord_offsets[ci]]));
                memcpy(init_source_data_copy.texcoord_.back(), init_source_data.texcoord_[ci], sizeof(*init_source_data.texcoord_[ci]) * num_vtx);
            }
            if (init_source_data.index_)
            {
                init_source_data_copy.index_ = reinterpret_cast<uint32_t*>(&out_mesh.raw_data_mem_[index_offset]);
                memcpy(init_source_data_copy.index_, init_source_data.index_, sizeof(*init_source_data.index_) * num_primitive * 3);
            }
        }

        // 初期化済みのMeshShapePartをMeshDataにセット.
        out_mesh.shape_array_.resize(1);
        out_mesh.shape_array_[0].Initialize(p_device, init_source_data_copy);
    }

    bool InitializeMeshDataFromLayout(MeshData& out_mesh, rhi::DeviceDep* p_device)
    {
        if (!p_device)
            return false;
        if (out_mesh.raw_data_mem_.empty() || out_mesh.shape_layout_array_.empty())
            return false;

        auto* base_ptr = out_mesh.raw_data_mem_.data();
        out_mesh.shape_array_.resize(out_mesh.shape_layout_array_.size());

        for (int shape_i = 0; shape_i < out_mesh.shape_layout_array_.size(); ++shape_i)
        {
            const auto& layout = out_mesh.shape_layout_array_[shape_i];

            MeshShapeInitializeSourceData init_data{};
            init_data.num_vertex_ = layout.num_vertex;
            init_data.num_primitive_ = layout.num_primitive;

            if (0 <= layout.offset_position)
                init_data.position_ = reinterpret_cast<math::Vec3*>(base_ptr + layout.offset_position);
            if (0 <= layout.offset_normal)
                init_data.normal_ = reinterpret_cast<math::Vec3*>(base_ptr + layout.offset_normal);
            if (0 <= layout.offset_tangent)
                init_data.tangent_ = reinterpret_cast<math::Vec3*>(base_ptr + layout.offset_tangent);
            if (0 <= layout.offset_binormal)
                init_data.binormal_ = reinterpret_cast<math::Vec3*>(base_ptr + layout.offset_binormal);

            for (int ci = 0; ci < layout.num_color_ch; ++ci)
            {
                if (0 <= layout.offset_color[ci])
                {
                    init_data.color_.push_back(reinterpret_cast<VertexColor*>(base_ptr + layout.offset_color[ci]));
                }
            }
            for (int ci = 0; ci < layout.num_uv_ch; ++ci)
            {
                if (0 <= layout.offset_uv[ci])
                {
                    init_data.texcoord_.push_back(reinterpret_cast<math::Vec2*>(base_ptr + layout.offset_uv[ci]));
                }
            }

            if (0 <= layout.offset_index)
                init_data.index_ = reinterpret_cast<uint32_t*>(base_ptr + layout.offset_index);

            out_mesh.shape_array_[shape_i].Initialize(p_device, init_data);
        }

        return true;
    }
}
}