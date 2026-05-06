/*
    srvs.h
    screen-reconstructed voxel structure.
*/

#pragma once

#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/constant_buffer_pool.h"
#include "render/app/common/render_app_common.h"
#include "render/task/pass_common.h"
#include "gfx/rtg/graph_builder.h"

#ifndef NGL_SHADER_CPP_INCLUDE
#define NGL_SHADER_CPP_INCLUDE
#define NGL_SRVS_H_UNDEF_SHADER_CPP_INCLUDE
#endif
#include "../../../../shader/srvs/srvs_common_header.hlsli"
#ifdef NGL_SRVS_H_UNDEF_SHADER_CPP_INCLUDE
#undef NGL_SRVS_H_UNDEF_SHADER_CPP_INCLUDE
#undef NGL_SHADER_CPP_INCLUDE
#endif

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
        rtg::RtgResourceHandle h_color{};// radiance injection 用入力カラー.
        rhi::RefSrvDep hw_color_srv{};// レンダリングフェーズ用.

        bool is_enable_injection_pass{true};// Voxel充填利用するか.
        bool is_enable_removal_pass{true};// Voxel除去に利用するか.
        bool is_enable_radiance_injection_pass{false};// Brick radiance 注入に利用するか.
    };
    struct InjectionSourceDepthBufferInfo
    {
        InjectionSourceDepthBufferViewInfo primary{};
        std::vector<InjectionSourceDepthBufferViewInfo> sub_array{};
    };



    // BitmaskBrickVoxelGi:Bbv.
    class BitmaskBrickVoxelGi
    {
    public:
        BitmaskBrickVoxelGi() = default;
        ~BitmaskBrickVoxelGi();

        // 初期化
        struct InitArg
        {
            math::Vec3u voxel_resolution = math::Vec3u(32);
            float       voxel_size = 3.0f;
            
            math::Vec3u probe_resolution = math::Vec3u(32);
            float       probe_cell_size = 3.0f;
            u32         probe_cascade_count = 5;
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
        void Dispatch_Bbv_RadianceInjection_View(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv,
            const ngl::render::task::RenderPassViewInfo& main_view_info, const InjectionSourceDepthBufferViewInfo& view_info
            );
            
        void Dispatch_Bbv_Main(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv
            );

        void Dispatch_SsProbe(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv,
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
            );
        void Dispatch_AsspHierarchy(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv,
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
            );
        void Dispatch_AsspProbe(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv,
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
            );

        void Dispatch_Fsp(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv
            );

        void Dispatch_Debug(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefSrvDep lit_color_srv, rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);

        void UpdateFspDebugReadback();
        void UpdateAsspDebugReadback();


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);


        ngl::rhi::ConstantBufferPooledHandle GetDispatchCbh() const { return cbh_dispatch_; }
        rhi::RefSrvDep GetFspProbeAtlasTex() const { return fsp_probe_atlas_tex_.srv; }
        rhi::RefSrvDep GetFspProbePackedShTex() const { return fsp_probe_packed_sh_tex_.srv; }
        rhi::RefSrvDep GetFspCellProbeIndexBuffer() const { return fsp_cell_probe_index_buffer_.srv; }
        rhi::RefSrvDep GetFspProbePoolBuffer() const { return fsp_probe_pool_buffer_.srv; }
        rhi::RefSrvDep GetSsProbeTex() const { return ss_probe_tex_[ss_probe_latest_filtered_frame_tex_index_].srv; }
        rhi::RefSrvDep GetSsProbeTileInfoTex() const { return ss_probe_tile_info_tex_[ss_probe_tile_info_curr_frame_tex_index_].srv; }
        rhi::RefSrvDep GetSsProbePackedShTex() const { return ss_probe_packed_sh_tex_.srv; }
        rhi::RefSrvDep GetAsspBuffer() const { return assp_buffer_.srv; }
        rhi::RefSrvDep GetAsspProbeTex() const { return assp_probe_tex_[assp_latest_filtered_frame_tex_index_].srv; }
        rhi::RefSrvDep GetAsspProbeTileInfoTex() const { return assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].srv; }
        rhi::RefSrvDep GetAsspProbePackedShTex() const { return assp_probe_packed_sh_tex_.srv; }

    private:
        bool ResizeScreenProbeResources(ngl::rhi::DeviceDep* p_device, const math::Vec2i& render_resolution);

        bool is_first_dispatch_ = true;

        u32 frame_count_{};

        math::Vec3 important_point_ = {0,0,0};
        math::Vec3 important_dir_ = {0,0,1};

        ngl::u32 ss_probe_prev_frame_tex_index_ = 0;
        ngl::u32 ss_probe_curr_frame_tex_index_ = 0;
        ngl::u32 ss_probe_latest_filtered_frame_tex_index_ = 0;

        ngl::u32 ss_probe_tile_info_prev_frame_tex_index_ = 0;
        ngl::u32 ss_probe_tile_info_curr_frame_tex_index_ = 0;

        ngl::u32 assp_prev_frame_tex_index_ = 0;
        ngl::u32 assp_curr_frame_tex_index_ = 0;
        ngl::u32 assp_latest_filtered_frame_tex_index_ = 0;
        ngl::u32 assp_variance_prev_frame_tex_index_ = 0;
        ngl::u32 assp_variance_curr_frame_tex_index_ = 0;

        ngl::u32 assp_tile_info_prev_frame_tex_index_ = 0;
        ngl::u32 assp_tile_info_curr_frame_tex_index_ = 0;

        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_begin_view_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_list_build_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_apply_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_injection_apply_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_radiance_injection_apply_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_radiance_resolve_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_brick_count_aggregate_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_hibrick_count_aggregate_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_generate_visible_voxel_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_indirect_arg_build_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_element_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_visible_surface_element_update_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_visible_surface_proc_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_generate_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_pre_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_sh_update_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_debug_visualize_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_bbv_debug_probe_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_fsp_debug_probe_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_preupdate_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_spatial_filter_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_ss_probe_sh_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_preupdate_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_generate_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_build_ray_meta_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_finalize_ray_query_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_trace_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_spatial_filter_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_variance_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_sh_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_depth_analysis_ = {};


        ngl::rhi::ConstantBufferPooledHandle cbh_dispatch_ = {};
        SrvsParam dispatch_param_cache_ = {};

        // Bitmask Brick Voxel. Bbv.
        // ----------------------------------------------------------------
        ToroidalGridUpdater bbv_grid_updater_ = {};

        ComputeBufferSet bbv_buffer_ = {};
        ComputeBufferSet bbv_optional_data_buffer_ = {};
        ComputeBufferSet bbv_radiance_accum_buffer_ = {};

        ngl::u32     bbv_hollow_voxel_list_count_max_ = {};
        // 可視Voxelのみ更新用.
        ngl::u32     bbv_fine_update_voxel_count_max_ = {};
        ComputeBufferSet bbv_fine_update_voxel_list_ = {};
        ComputeBufferSet bbv_fine_update_voxel_indirect_arg_ = {};
        ComputeBufferSet bbv_fine_update_voxel_probe_buffer_ = {};
        
        // 除去用リスト.
        ComputeBufferSet bbv_removal_list_ = {};
        ComputeBufferSet bbv_removal_indirect_arg_ = {};


        // Frustum Surface Probe. Fsp.
        // ----------------------------------------------------------------
        std::vector<ToroidalGridUpdater> fsp_grid_updaters_ = {};
        std::vector<u32> fsp_cascade_cell_offset_array_ = {};

        ngl::u32     fsp_visible_surface_buffer_size_ = {};
        ngl::u32     fsp_probe_pool_size_ = {};
        ngl::u32     fsp_cascade_count_ = {};
        ngl::u32     fsp_total_cell_count_ = {};
        ngl::u32     fsp_probe_atlas_tile_width_ = {};
        ngl::u32     fsp_probe_atlas_tile_height_ = {};
        ComputeBufferSet fsp_visible_surface_list_ = {};
        ComputeBufferSet fsp_indirect_arg_ = {};
        ComputeBufferSet fsp_cell_probe_index_buffer_ = {};
        ComputeBufferSet fsp_probe_pool_buffer_ = {};
        ComputeBufferSet fsp_probe_free_stack_buffer_ = {};
        ComputeBufferSet fsp_active_probe_list_[2] = {};
        ComputeBufferSet fsp_buffer_ = {};
        ComputeTextureSet fsp_probe_atlas_tex_ = {};
        ComputeTextureSet fsp_probe_packed_sh_tex_ = {};
        rhi::RefBufferDep fsp_visible_surface_list_readback_buffer_ = {};
        rhi::RefBufferDep fsp_probe_free_stack_readback_buffer_ = {};
        rhi::RefBufferDep fsp_active_probe_list_readback_buffer_ = {};

        
        // ScreenSpaceProbe.
        ComputeTextureSet ss_probe_tile_info_tex_[2] = {}; //f16_rgba, 1/8解像度のProbeタイル用情報. x: depth, y: probe local pos(flat), zw: OctEncode WS normal.
        ComputeTextureSet ss_probe_tex_[2] = {};// 8x8 texel per probe.
        ComputeTextureSet ss_probe_packed_sh_tex_ = {}; //f16_rgba, 係数優先2x2 atlas. RGBA = SkyVisibility + RadianceRGB.
        ComputeTextureSet ss_probe_best_prev_tile_tex_ = {}; //r32_uint, Preupdateで計算したBestPrevTile (packed tile id).
        // Persistent Side Cache (minimal): evicted probe octmap + world-space meta per probe tile.
        ComputeTextureSet ss_probe_side_cache_tex_ = {}; // 8x8 texel per cached probe.
        ComputeTextureSet ss_probe_side_cache_meta_tex_ = {}; // 1/8 resolution, xyz: world pos, w: last update frame.
        ComputeTextureSet ss_probe_side_cache_lock_tex_ = {}; // 1/8 resolution, uint lock tag per tile for frame-local CAS.
        ComputeTextureSet assp_probe_tile_info_tex_[2] = {}; // f16_rgba, 1/4解像度のASSPタイル情報.
        ComputeTextureSet assp_probe_tex_[2] = {}; // 4x4 texel per probe.
        ComputeTextureSet assp_probe_variance_tex_[2] = {}; // f16_rgba, x: filtered mean, y: filtered second moment, z: raw mean, w: raw variance.
        ComputeTextureSet assp_probe_packed_sh_tex_ = {}; // f16_rgba, 係数優先2x2 atlas.
        ComputeTextureSet assp_probe_best_prev_tile_tex_ = {}; // r32_uint, Preupdateで計算したBestPrevTile.
        ComputeBufferSet assp_buffer_ = {}; // LOD0-LOD[MAX] unified scalar uint buffer.
        ComputeBufferSet assp_probe_indirect_arg_ = {}; // Probe単位 pass(Resolve/Variance/SH) 用 DispatchIndirect 3 uint.
        ComputeBufferSet assp_probe_trace_indirect_arg_ = {}; // RayTrace pass 用 DispatchIndirect 3 uint.
        ComputeBufferSet assp_probe_total_ray_count_buffer_ = {}; // [0] = frame total traced ray count.
        ComputeBufferSet assp_probe_ray_meta_buffer_ = {}; // packed ray meta: offset|count per representative probe.
        ComputeBufferSet assp_probe_ray_query_buffer_ = {}; // packed ray query: probe_list_index|local_ray_index.
        ComputeBufferSet assp_probe_ray_result_buffer_ = {}; // ray結果: [octCell, skyVis, radiance.rgb] を uint5 で保持.
        rhi::RefBufferDep assp_probe_total_ray_count_readback_buffer_ = {};

    };

    
    class ScreenReconstructedVoxelStructure
    {
    public:
        static int dbg_view_category_;
        static int dbg_view_sub_mode_;
        
        
        static int dbg_bbv_probe_debug_mode_;
        static int dbg_fsp_probe_debug_mode_;
        static int dbg_fsp_probe_debug_cascade_;
        static int dbg_fsp_cascade_count_;
        static float dbg_probe_scale_;
        static float dbg_probe_near_geom_scale_;
        static int dbg_ss_probe_spatial_filter_enable_;
        static int dbg_ss_probe_temporal_reprojection_enable_;
        static int dbg_ss_probe_ray_guiding_enable_;
        static int dbg_ss_probe_side_cache_enable_;
        static float dbg_ss_probe_preupdate_relocation_probability_;
        static float dbg_ss_probe_temporal_filter_normal_cos_threshold_;
        static float dbg_ss_probe_temporal_filter_plane_dist_threshold_;
        static float dbg_ss_probe_spatial_filter_normal_cos_threshold_;
        static float dbg_ss_probe_spatial_filter_depth_exp_scale_;
        static float dbg_ss_probe_side_cache_plane_dist_threshold_;
        static int assp_spatial_filter_enable_;
        static float assp_spatial_filter_normal_cos_threshold_;
        static float assp_spatial_filter_depth_exp_scale_;
        static int assp_temporal_reprojection_enable_;
        static int assp_ray_guiding_enable_;
        static int assp_ray_budget_min_rays_;
        static int assp_ray_budget_max_rays_;
        static float assp_ray_budget_variance_weight_;
        static float assp_ray_budget_normal_delta_weight_;
        static float assp_ray_budget_depth_delta_weight_;
        static float assp_ray_budget_no_history_bias_;
        static float assp_ray_budget_scale_;
        static int assp_debug_freeze_frame_random_enable_;
        static int dbg_assp_leaf_border_enable_;
        static int dbg_fsp_lighting_interpolation_enable_;
        static int dbg_fsp_spawn_far_cell_enable_;
        static int dbg_fsp_lighting_stochastic_sampling_enable_;
        static int dbg_fsp_probe_pool_size_;
        static int dbg_fsp_free_probe_count_;
        static int dbg_fsp_allocated_probe_count_;
        static int dbg_fsp_active_probe_count_;
        static int dbg_fsp_visible_surface_cell_count_;
        static int dbg_assp_total_ray_count_;
        static int dbg_assp_probe_count_;

        // デバッグメニューを描画する. ImGuiウィンドウ内で呼び出すこと.
        static void DrawDebugMenu(bool* p_enable_injection, bool* p_enable_rejection);

    public:
        ScreenReconstructedVoxelStructure() = default;
        ~ScreenReconstructedVoxelStructure();

        // 初期化
        bool Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u fsp_resolution, float fsp_cell_size, u32 fsp_cascade_count = 5);
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
        void DispatchViewBbvRadianceInjection(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv,
            const ngl::render::task::RenderPassViewInfo& main_view_info,
            const InjectionSourceDepthBufferViewInfo& view_info);
            
        void DispatchUpdate(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv);
        void DispatchDebug(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefSrvDep lit_color_srv, rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);


        void SetImportantPointInfo(const math::Vec3& pos, const math::Vec3& dir);

        void SetDescriptor(rhi::PipelineStateBaseDep* p_pso, rhi::DescriptorSetDep* p_desc_set) const;

    private:
            bool is_initialized_ = false;
            BitmaskBrickVoxelGi* bbvgi_instance_;
    };




    class RenderTaskSrvsBegin : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::ScreenReconstructedVoxelStructure* p_srvs = {};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_srvs)
                return;

			desc_ = desc;
            
            // srvsへの情報直接設定をBeginで実行.
            desc_.p_srvs->SetImportantPointInfo(view_info.camera_pos, view_info.camera_pose.GetColumn2());

			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this, view_info](ngl::rtg::RenderTaskGraphBuilder& builder, ngl::rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSrvsBegin");

                    desc_.p_srvs->DispatchBegin(gfx_commandlist, desc_.scene_cbv, 
                        view_info, math::Vec2i(desc_.w, desc_.h));
				}
			);
		}
	};

    class RenderTaskSrvsViewVoxelRadianceInjection : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::ScreenReconstructedVoxelStructure* p_srvs = {};

            InjectionSourceDepthBufferViewInfo view_info{};
		};
		SetupDesc desc_{};
		
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_srvs || desc.view_info.h_depth.IsInvalid() || desc.view_info.h_color.IsInvalid() || !desc.view_info.is_enable_radiance_injection_pass)
                return;

			desc_ = desc;
			{
                desc_.view_info.h_depth = builder.RecordResourceAccess(*this, desc_.view_info.h_depth, ngl::rtg::AccessType::SHADER_READ);
                desc_.view_info.h_color = builder.RecordResourceAccess(*this, desc_.view_info.h_color, ngl::rtg::AccessType::SHADER_READ);
			}
			builder.RegisterTaskNodeRenderFunction(this,
				[this, view_info](ngl::rtg::RenderTaskGraphBuilder& builder, ngl::rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSrvsViewVoxelRadianceInjection");

                    InjectionSourceDepthBufferViewInfo injection_view_info = desc_.view_info;
                    {
                        auto res_depth = builder.GetAllocatedResource(this, desc_.view_info.h_depth);
                        auto res_color = builder.GetAllocatedResource(this, desc_.view_info.h_color);
                        assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
                        assert(res_color.tex_.IsValid() && res_color.srv_.IsValid());
                        injection_view_info.hw_depth_srv = res_depth.srv_;
                        injection_view_info.hw_color_srv = res_color.srv_;
                    }

                    desc_.p_srvs->DispatchViewBbvRadianceInjection(gfx_commandlist, desc_.scene_cbv, view_info, injection_view_info);
				}
			);
		}
    };
    

    class RenderTaskSrvsViewVoxelInjection : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::ScreenReconstructedVoxelStructure* p_srvs = {};

            //ngl::rtg::RtgResourceHandle h_depth{};

            InjectionSourceDepthBufferInfo depth_buffer_info{};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_srvs)
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSrvsViewVoxelInjection");

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

                    
                    desc_.p_srvs->DispatchViewBbvOccupancyUpdate(gfx_commandlist, desc_.scene_cbv, view_info, injection_depth_buffer_info);
				}
			);
		}
	};

    class RenderTaskSrvsUpdate : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		ngl::rtg::RtgResourceHandle h_depth_{};

		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::ScreenReconstructedVoxelStructure* p_srvs = {};

            ngl::rtg::RtgResourceHandle h_depth{};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_srvs)
                return;

			desc_ = desc;
			
			// Rtgリソースセットアップ.
			{
				// リソース定義.
				// リソースアクセス定義.
                h_depth_ = builder.RecordResourceAccess(*this, desc.h_depth, rtg::AccessType::SHADER_READ);
			}

			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this, view_info](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSrvsUpdate");

					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());

                    desc_.p_srvs->DispatchUpdate(gfx_commandlist, desc_.scene_cbv, 
                        view_info, res_depth.tex_, res_depth.srv_);
				}
			);
		}
	};

    class RenderTaskSrvsDebug : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		ngl::rtg::RtgResourceHandle h_depth_{};
		ngl::rtg::RtgResourceHandle h_color_{};
		ngl::rtg::RtgResourceHandle h_work_{};

		struct SetupDesc
		{
            int w{};
            int h{};

            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::ScreenReconstructedVoxelStructure* p_srvs = {};

            ngl::rtg::RtgResourceHandle h_depth{};
            ngl::rtg::RtgResourceHandle h_color{};
		};
		SetupDesc desc_{};

		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_srvs || desc.h_depth.IsInvalid() || desc.h_color.IsInvalid())
                return;

			desc_ = desc;

			{
                ngl::rtg::RtgResourceDesc2D work_desc = ngl::rtg::RtgResourceDesc2D::CreateAsAbsoluteSize(desc.w, desc.h, rhi::EResourceFormat::Format_R32G32B32A32_FLOAT);
                h_depth_ = builder.RecordResourceAccess(*this, desc.h_depth, rtg::AccessType::SHADER_READ);
                h_color_ = builder.RecordResourceAccess(*this, desc.h_color, rtg::AccessType::SHADER_READ);
                h_work_ = builder.RecordResourceAccess(*this, builder.CreateResource(work_desc), rtg::AccessType::UAV);
			}

			builder.RegisterTaskNodeRenderFunction(this,
				[this, view_info](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskSrvsDebug");

					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
                    auto res_color = builder.GetAllocatedResource(this, h_color_);
                    auto res_work = builder.GetAllocatedResource(this, h_work_);
					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
                    assert(res_color.srv_.IsValid());
                    assert(res_work.tex_.IsValid() && res_work.uav_.IsValid());

                    desc_.p_srvs->DispatchDebug(gfx_commandlist, desc_.scene_cbv,
                        view_info, res_depth.tex_, res_depth.srv_,
                        res_color.srv_, res_work.tex_, res_work.uav_);
				}
			);
		}
    };

}  // namespace ngl::render::app
