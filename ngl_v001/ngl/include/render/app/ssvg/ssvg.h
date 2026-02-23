/*
    sw_tessellation_mesh.h
*/

#pragma once

#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/constant_buffer_pool.h"
#include "render/app/common/render_app_common.h"
#include "render/task/pass_common.h"
#include "gfx/rtg/graph_builder.h"

namespace ngl::render::app
{

    struct ToroidalGridArea
    {
        math::Vec3i center_cell_id = {};
        math::Vec3i center_cell_id_prev = {};
        math::Vec3 min_pos = {};
        math::Vec3 min_pos_prev = {};
        math::Vec3i toroidal_offset = {};
        math::Vec3i toroidal_offset_prev = {};
        math::Vec3i min_pos_delta_cell = {};

        math::Vec3u resolution = math::Vec3u(32);
        float       cell_size = 3.0f;

        u32 total_count = {};
        
        u32         flatten_2d_width = {};
    };
    class ToroidalGridUpdater
    {
    public:
        ToroidalGridUpdater() = default;
        ~ToroidalGridUpdater() = default;

        void Initialize(const math::Vec3u& grid_resolution, float bbv_cell_size);

        void UpdateGrid(const math::Vec3& important_pos);

        const ToroidalGridArea& Get() const { return grid_; }

        math::Vec3i CalcToroidalGridCoordFromLinearCoord(const math::Vec3i& linear_coord) const;
        math::Vec3i CalcLinearGridCoordFromToroidalCoord(const math::Vec3i& toroidal_coord) const;

    private:
        ToroidalGridArea grid_;
    };


    struct InjectionSourceDepthBufferViewInfo
    {
        math::Mat34 view_mat{};
        math::Mat44 proj_mat{};

        math::Vec2i atlas_offset{};
        math::Vec2i atlas_resolution{};

        rtg::RtgResourceHandle h_depth{};// セットアップフェーズ用.
        rhi::RefSrvDep hw_depth_srv{};// レンダリングフェーズ用.

        bool is_enable_injection_pass{true};// Voxel充填利用するか.
        bool is_enable_removal_pass{true};// Voxel除去に利用するか.
    };
    struct InjectionSourceDepthBufferInfo
    {
        InjectionSourceDepthBufferViewInfo primary{};
        std::vector<InjectionSourceDepthBufferViewInfo> sub_array{};
    };



    // BitmaskBrickVoxel:Bbv.
    class BitmaskBrickVoxel
    {
    public:
        BitmaskBrickVoxel() = default;
        ~BitmaskBrickVoxel();

        // 初期化
        struct InitArg
        {
            math::Vec3u voxel_resolution = math::Vec3u(32);
            float       voxel_size = 3.0f;
            
            math::Vec3u probe_resolution = math::Vec3u(32);
            float       probe_cell_size = 3.0f;
        };
        bool Initialize(ngl::rhi::DeviceDep* p_device, const InitArg& init_arg);

        
        void Dispatch_Begin(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, const math::Vec2i& render_resolution
            );

        void Dispatch_Bbv_OccupancyUpdate_View(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, const InjectionSourceDepthBufferInfo& depth_buffer_info
            );
            
        void Dispatch_Bbv_Main(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv
            );

        void Dispatch_Wcp(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
            );

        void Dispatch_Debug(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);


        ngl::rhi::ConstantBufferPooledHandle GetDispatchCbh() const { return cbh_dispatch_; }
        rhi::RefSrvDep GetWcpProbeAtlasTex() const { return wcp_probe_atlas_tex_.srv; }
        rhi::RefSrvDep GetSsProbeTex() const { return ss_probe_tex_.srv; }

    private:
        bool is_first_dispatch_ = true;

        u32 frame_count_{};

        math::Vec3 important_point_ = {0,0,0};
        math::Vec3 important_dir_ = {0,0,1};

        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_begin_view_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_list_build_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_apply_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_injection_apply_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_generate_visible_voxel_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_indirect_arg_build_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_element_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_visible_surface_element_update_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_visible_surface_proc_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_generate_visible_surface_list_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_visible_surface_element_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_coarse_ray_sample_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_wcp_fill_probe_octmap_atlas_border_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_debug_visualize_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_bbv_debug_probe_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_wcp_debug_probe_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_preupdate_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_update_ = {};


        ngl::rhi::ConstantBufferPooledHandle cbh_dispatch_ = {};

        // Bitmask Brick Voxel. Bbv.
        // ----------------------------------------------------------------
        ToroidalGridUpdater bbv_grid_updater_ = {};

        ComputeBufferSet bbv_buffer_ = {};
        ComputeBufferSet bbv_optional_data_buffer_ = {};

        ngl::u32     bbv_hollow_voxel_list_count_max_ = {};
        // 可視Voxelのみ更新用.
        ngl::u32     bbv_fine_update_voxel_count_max_ = {};
        ComputeBufferSet bbv_fine_update_voxel_list_ = {};
        ComputeBufferSet bbv_fine_update_voxel_indirect_arg_ = {};
        ComputeBufferSet bbv_fine_update_voxel_probe_buffer_ = {};
        
        // 除去用リスト.
        ComputeBufferSet bbv_removal_list_ = {};
        ComputeBufferSet bbv_removal_debug_list_ = {};
        ComputeBufferSet bbv_removal_indirect_arg_ = {};


        // World Cache Probe. Wcp.
        // ----------------------------------------------------------------
        ToroidalGridUpdater wcp_grid_updater_ = {};

        ngl::u32     wcp_visible_surface_buffer_size_ = {};
        ComputeBufferSet wcp_visible_surface_list_ = {};
        ComputeBufferSet wcp_visible_surface_list_indirect_arg_ = {};
        ComputeBufferSet wcp_buffer_ = {};
        ComputeTextureSet wcp_probe_atlas_tex_ = {};

        
        // ScreenSpaceProbe.
        ComputeTextureSet ss_probe_tile_info_tex_ = {}; // 1/8 解像度のProbeタイル用情報. r:probe local pos, gb:hw depth, a: todo.
        ComputeTextureSet ss_probe_tex_ = {};// 8x8 texel per probe.

    };

    
    class SsVg
    {
    public:
        static int dbg_view_mode_;
        
        
        static int dbg_bbv_probe_debug_mode_;
        static int dbg_wcp_probe_debug_mode_;
        static float dbg_probe_scale_;
        static float dbg_probe_near_geom_scale_;

    public:
        SsVg() = default;
        ~SsVg();

        // 初期化
        bool Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u wcp_resolution, float wcp_cell_size);
        bool IsValid() const { return is_initialized_; }
        // 破棄
        void Finalize();

        void DispatchBegin(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, const math::Vec2i& render_resolution);
            

        void DispatchViewBbvOccupancyUpdate(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info,
            
            const InjectionSourceDepthBufferInfo& depth_buffer_info);
            
        void DispatchUpdate(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);

        void SetDescriptor(rhi::PipelineStateBaseDep* p_pso, rhi::DescriptorSetDep* p_desc_set) const;

    private:
            bool is_initialized_ = false;
            BitmaskBrickVoxel* ssvg_instance_;
    };




    class RenderTaskSsvgBegin : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::SsVg* p_ssvg = {};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_ssvg)
                return;

			desc_ = desc;
            
            // ssvgへの情報直接設定をBeginで実行.
            desc_.p_ssvg->SetImportantPointInfo(view_info.camera_pos, view_info.camera_pose.GetColumn2());

			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this, view_info](ngl::rtg::RenderTaskGraphBuilder& builder, ngl::rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSsvgBegin");

                    desc_.p_ssvg->DispatchBegin(gfx_commandlist, desc_.scene_cbv, 
                        view_info, math::Vec2i(desc_.w, desc_.h));
				}
			);
		}
	};
    

    class RenderTaskSsvgViewVoxelInjection : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::SsVg* p_ssvg = {};

            //ngl::rtg::RtgResourceHandle h_depth{};

            InjectionSourceDepthBufferInfo depth_buffer_info{};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_ssvg)
                return;

			desc_ = desc;// コピー.
			// Rtgリソースセットアップ.
			{
				// リソースアクセス定義.
                desc_.depth_buffer_info.primary.h_depth = builder.RecordResourceAccess(*this, desc_.depth_buffer_info.primary.h_depth, ngl::rtg::AccessType::SHADER_READ);

                for(int i = 0; i < desc_.depth_buffer_info.sub_array.size(); ++i)
                {
                    // ハンドルへのアクセスレコード(ハンドル変わる可能性があるので更新).
                    desc_.depth_buffer_info.sub_array[i].h_depth = builder.RecordResourceAccess(*this, desc_.depth_buffer_info.sub_array[i].h_depth, ngl::rtg::AccessType::SHADER_READ);
                }
			}
			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this, view_info](ngl::rtg::RenderTaskGraphBuilder& builder, ngl::rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSsvgViewVoxelInjection");

                    InjectionSourceDepthBufferInfo injection_depth_buffer_info{};
                    {
                        {
                            auto res_depth = builder.GetAllocatedResource(this, desc_.depth_buffer_info.primary.h_depth);
                            assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
                            
                            injection_depth_buffer_info.primary = desc_.depth_buffer_info.primary;// copy.
                            injection_depth_buffer_info.primary.hw_depth_srv = res_depth.srv_;// リソース設定.

                            for(int i = 0; i < desc_.depth_buffer_info.sub_array.size(); ++i)
                            {
                                auto res_sub_depth = builder.GetAllocatedResource(this, desc_.depth_buffer_info.sub_array[i].h_depth);
                                assert(res_sub_depth.tex_.IsValid() && res_sub_depth.srv_.IsValid());

                                InjectionSourceDepthBufferViewInfo sub_view_info = desc_.depth_buffer_info.sub_array[i];// copy.
                                sub_view_info.hw_depth_srv = res_sub_depth.srv_;// リソース設定.

                                    injection_depth_buffer_info.sub_array.push_back(sub_view_info);
                            }
                        }
                    }

                    
                    desc_.p_ssvg->DispatchViewBbvOccupancyUpdate(gfx_commandlist, desc_.scene_cbv, view_info, injection_depth_buffer_info);
				}
			);
		}
	};

    class RenderTaskSsvgUpdate : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		ngl::rtg::RtgResourceHandle h_depth_{};
		ngl::rtg::RtgResourceHandle h_work_{};

		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::SsVg* p_ssvg = {};

            ngl::rtg::RtgResourceHandle h_depth{};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_ssvg)
                return;

			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// リソース定義.
                ngl::rtg::RtgResourceDesc2D work_desc = ngl::rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R32G32B32A32_FLOAT);

				// リソースアクセス定義.
                h_depth_ = builder.RecordResourceAccess(*this, desc.h_depth, rtg::AccessType::SHADER_READ);
                h_work_ = builder.RecordResourceAccess(*this, builder.CreateResource(work_desc), rtg::AccessType::UAV);
			}

			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this, view_info](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSsvgUpdate");

					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
                    auto res_work = builder.GetAllocatedResource(this, h_work_);
					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
                    assert(res_work.tex_.IsValid() && res_work.uav_.IsValid());

                    desc_.p_ssvg->DispatchUpdate(gfx_commandlist, desc_.scene_cbv, 
                        view_info, res_depth.tex_, res_depth.srv_,
                        res_work.tex_, res_work.uav_);
				}
			);
		}
	};

}  // namespace ngl::render::app
