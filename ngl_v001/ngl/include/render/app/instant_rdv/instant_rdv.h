/*
    instant_rdv.h
    Instant Raster Derived Voxel Sceneの公開型・描画パス定義。
*/

#pragma once

#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/constant_buffer_pool.h"
#include "render/app/common/render_app_common.h"
#include "render/task/pass_common.h"
#include "gfx/rtg/graph_builder.h"

#ifndef NGL_SHADER_CPP_INCLUDE
#define NGL_SHADER_CPP_INCLUDE
#define NGL_INSTANT_RDV_H_UNDEF_SHADER_CPP_INCLUDE
#endif
#include "../../../../shader/instant_rdv/instant_rdv_common_header.hlsli"
#ifdef NGL_INSTANT_RDV_H_UNDEF_SHADER_CPP_INCLUDE
#undef NGL_INSTANT_RDV_H_UNDEF_SHADER_CPP_INCLUDE
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
            // BBVはMorton X10Y10Z10を密なbuffer indexとして使うため、
            // cubic power-of-twoかつ各軸1..1024。buffer count overflowも拒否する。
            math::Vec3u voxel_resolution = math::Vec3u(32);
            float       voxel_size = 3.0f;
            
            // FSP共通grid解像度。全cascadeで同一値を使用する。
            // ActiveProbe/SurfaceMask/IrradianceVolumeはすべてX-major。
            // bit-mask Toroidal wrapと解析的cascade選択にはcubic power-of-twoかつ各軸4以上が必要。
            // Initializeで検証する。
            math::Vec3u probe_resolution = math::Vec3u(32);
            // Cascade 0のcell size。後続cascadeは厳密に2倍ずつ生成される。
            float       probe_cell_size = 3.0f;
            // 1..k_fsp_max_cascade_count。shader constant-buffer配列長を超える値は拒否する。
            u32         probe_cascade_count = 5;
        };
        bool Initialize(ngl::rhi::DeviceDep* p_device, const InitArg& init_arg);

        
        // RenderThread起動前の同期タイミングで、フレーム値の確定と必要なリソース再確保を行う。
        bool PrepareFrame(rhi::DeviceDep* p_device,
            const math::Vec3& important_pos,
            const math::Vec3& important_dir,
            const math::Vec3& main_light_dir,
            const math::Vec2i& render_resolution);
        // RTG Setupでフレーム用定数バッファを確保し、確定済みの値を書き込む。
        void UploadFrameConstants(rhi::DeviceDep* p_device);

        void Dispatch_Begin(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv);

        void Dispatch_Bbv_OccupancyUpdate_View(rhi::GraphicsCommandListDep* p_command_list,
            const ngl::render::task::RenderPassViewInfo& main_view_info, const InjectionSourceDepthBufferInfo& depth_buffer_info
            );
        void Dispatch_Bbv_RadianceInjection_View(rhi::GraphicsCommandListDep* p_command_list,
            const ngl::render::task::RenderPassViewInfo& main_view_info, const InjectionSourceDepthBufferViewInfo& view_info
            );
            
        void Dispatch_Bbv_Main(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv
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
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);

        void UpdateFspDebugReadback();
        void UpdateAsspDebugReadback();

        ngl::rhi::ConstantBufferPooledHandle GetDispatchCbh() const { return cbh_dispatch_; }
        rhi::RefSrvDep GetFspProbeAtlasTex() const { return fsp_probe_atlas_tex_.srv; }
        rhi::RefSrvDep GetFspIrradianceVolumeSHBuffer() const { return fsp_irradiance_volume_sh_buffer_.srv; }
        rhi::RefSrvDep GetFspCellProbeIndexBuffer() const { return fsp_cell_probe_index_buffer_.srv; }
        rhi::RefSrvDep GetFspProbePoolBuffer() const { return fsp_probe_pool_buffer_.srv; }
        rhi::RefSrvDep GetAsspProbeTex() const { return assp_probe_tex_[assp_latest_filtered_frame_tex_index_].srv; }
        rhi::RefSrvDep GetAsspProbeTileInfoTex() const { return assp_probe_tile_info_tex_[assp_tile_info_curr_frame_tex_index_].srv; }
        rhi::RefSrvDep GetAsspProbePackedShTex() const { return assp_probe_packed_sh_tex_.srv; }

    private:
        bool ResizeScreenProbeResources(ngl::rhi::DeviceDep* p_device, const math::Vec2i& render_resolution);

        bool is_first_dispatch_ = true;
        bool dispatch_requires_initial_clear_ = true;
        u32 frame_count_{};

        math::Vec3 important_point_ = {0,0,0};
        math::Vec3 important_dir_ = {0,0,1};

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
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_reduced_surface_buffer_build_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_radiance_injection_apply_short_ray_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_radiance_injection_apply_reduced_surface_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_radiance_resolve_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_brick_count_aggregate_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_element_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_frustum_cull_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_carving_indirect_arg_build_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_occupancy_injection_apply_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_occupancy_injection_apply_integrated_surface_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_occupancy_injection_apply_reduced_surface_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_removal_carving_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_begin_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_debug_stats_collect_ = {};
        // FSP SurfaceCell検出。
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_surface_mask_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_surface_mask_compact_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_surface_detect_reduced_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_generate_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_generate_prev_active_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_generate_curr_active_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_pre_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_probe_ray_request_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_probe_finalize_linear_indirect_arg_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_probe_ray_trace_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_probe_ray_resolve_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_sh_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_fsp_irradiance_volume_propagate_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_bbv_debug_visualize_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_bbv_debug_probe_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_fsp_debug_probe_ = {};
        ngl::rhi::RhiRef<ngl::rhi::GraphicsPipelineStateDep> pso_fsp_debug_probe_no_depth_ = {};


        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_clear_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_begin_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_preupdate_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_build_ray_meta_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_finalize_ray_query_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_trace_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_update_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_spatial_filter_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_variance_ = {};
        ngl::rhi::RhiRef<ngl::rhi::ComputePipelineStateDep> pso_assp_probe_sh_update_ = {};


        ngl::rhi::ConstantBufferPooledHandle cbh_dispatch_ = {};
        InstantRdvParam dispatch_param_cache_ = {};

        // Bitmask Brick Voxel. Bbv.
        // ----------------------------------------------------------------
        ToroidalGridUpdater bbv_grid_updater_ = {};

        ComputeBufferSet bbv_buffer_ = {};
        ComputeBufferSet bbv_optional_data_buffer_ = {};
        ComputeBufferSet bbv_radiance_accum_buffer_ = {};
        ComputeTextureSet reduced_surface_buffer_tex_ = {};

        ngl::u32     bbv_hollow_voxel_list_count_max_ = {};
        ngl::u32     bbv_fine_update_voxel_count_max_ = {};

        // 深度テストベース更新用 ActiveList.
        // 0番は active counter, 1..N は voxel index.
        ComputeBufferSet bbv_removal_frustum_brick_list_ = {};
        ComputeBufferSet bbv_removal_frustum_indirect_arg_ = {};


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
        ComputeBufferSet fsp_visible_surface_source_texel_list_ = {};
        ComputeBufferSet fsp_indirect_arg_ = {};
        ComputeBufferSet fsp_cell_probe_index_buffer_ = {};
        ComputeBufferSet fsp_probe_pool_buffer_ = {};
        ComputeBufferSet fsp_probe_free_stack_buffer_ = {};
        // ActiveProbeListは各Bufferの先頭2ワードを交互counter、ワード2以降をProbe index listとして使用する。
        ComputeBufferSet fsp_active_probe_list_[2] = {};
        // FSP SurfaceCell重複排除用。Cascadeごとの8x8x8 Brick内16ワードへ格納する。
        ComputeBufferSet fsp_surface_cell_mask_buffer_ = {};
        ngl::u32 fsp_surface_mask_word_count_ = {};
        // FSP update multipass 用ワーク:
        // request/result の 0番は atomic counter。1..N に payload を append する。
        ComputeBufferSet fsp_probe_ray_request_buffer_ = {};
        ComputeBufferSet fsp_probe_trace_indirect_arg_ = {};
        ComputeBufferSet fsp_probe_resolve_indirect_arg_ = {};
        ComputeBufferSet fsp_probe_ray_result_buffer_ = {};
        ComputeTextureSet fsp_probe_atlas_tex_ = {};
        ComputeBufferSet fsp_irradiance_volume_sh_buffer_ = {};
        ComputeBufferSet fsp_debug_stats_buffer_ = {};
        rhi::RefBufferDep fsp_debug_stats_readback_buffer_ = {};

        
        ComputeTextureSet assp_probe_tile_info_tex_[2] = {}; // f16_rgba, 1/4解像度のASSPタイル情報.
        ComputeTextureSet assp_probe_tex_[2] = {}; // 4x4 texel per probe.
        ComputeTextureSet assp_probe_variance_tex_[2] = {}; // f16_rgba, x: filtered mean, y: filtered second moment, z: raw mean, w: raw variance.
        ComputeTextureSet assp_probe_packed_sh_tex_ = {}; // f16_rgba, 係数優先2x2 atlas.
        ComputeTextureSet assp_probe_best_prev_tile_tex_ = {}; // r32_uint, Preupdateで計算したBestPrevTile.
        ComputeBufferSet assp_probe_trace_indirect_arg_ = {}; // RayTrace pass 用 DispatchIndirect 3 uint.
        ComputeBufferSet assp_probe_total_ray_count_buffer_ = {}; // [0] = frame total traced ray count.
        ComputeBufferSet assp_probe_ray_meta_buffer_ = {}; // packed ray meta: offset|count per active ASSP probe(tile).
        ComputeBufferSet assp_probe_ray_query_buffer_ = {}; // packed ray query: probe_list_index|local_ray_index.
        ComputeBufferSet assp_probe_ray_result_buffer_ = {}; // ray結果: [octCell, skyVis, radiance.rgb] を uint5 で保持.
        rhi::RefBufferDep assp_probe_total_ray_count_readback_buffer_ = {};

    };

    
    class InstantRasterDerivedVoxelScene
    {
    public:
        static int dbg_view_category_;
        static int dbg_view_sub_mode_;
        
        
        static int dbg_bbv_probe_debug_mode_;
        static int dbg_bbv_depth_test_enable_;
        static int dbg_fsp_probe_debug_mode_;
        static int dbg_fsp_irradiance_volume_debug_mode_;
        static int dbg_fsp_probe_depth_test_;
        static int dbg_fsp_probe_use_relocated_pos_;
        static int dbg_fsp_update_ray_jitter_enable_;
        static int dbg_fsp_probe_debug_cascade_;
        static int dbg_fsp_cascade_count_;
        static float dbg_fsp_relocation_offset_scale_for_cascade_cell_size_;
        static float dbg_probe_scale_;
        static float dbg_probe_near_geom_scale_;
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
        static int dbg_fsp_lighting_interpolation_enable_;
        static int dbg_fsp_probe_lifecycle_enable_;
        static int dbg_fsp_warm_start_enable_;
        static int dbg_fsp_probe_pool_size_;
        static int dbg_fsp_free_probe_count_;
        static int dbg_fsp_allocated_probe_count_;
        static int dbg_fsp_active_probe_count_;
        static int dbg_fsp_visible_surface_cell_count_;
        static bool dbg_main_view_reduced_surface_enable_;
        static bool dbg_fsp_debug_readback_enable_;
        static int dbg_assp_total_ray_count_;
        static int dbg_assp_probe_count_;
        static int dbg_gi_update_sample_mode_;
        static float dbg_bbv_occupancy_injection_fine_cells_default_;
        static float dbg_bbv_occupancy_injection_fine_cells_;

        // デバッグメニューを描画する. ImGuiウィンドウ内で呼び出すこと.
        static void DrawDebugMenu(
            bool* p_enable_all_injection,
            bool* p_enable_all_removal,
            bool* p_enable_main_view_injection,
            bool* p_enable_main_view_removal,
            bool* p_enable_shadow_view_injection,
            bool* p_enable_shadow_view_removal);

    public:
        InstantRasterDerivedVoxelScene() = default;
        ~InstantRasterDerivedVoxelScene();

        // 初期化
        bool Initialize(ngl::rhi::DeviceDep* p_device, math::Vec3u bbv_resolution, float bbv_cell_size, math::Vec3u fsp_resolution, float fsp_cell_size, u32 fsp_cascade_count = 5);
        bool IsValid() const { return is_initialized_; }
        // 破棄
        void Finalize();

        // RenderThread起動前の同期タイミングで呼び出す。
        bool PrepareFrame(rhi::DeviceDep* p_device,
            const math::Vec3& important_pos,
            const math::Vec3& important_dir,
            const math::Vec3& main_light_dir,
            const math::Vec2i& render_resolution);
        void UploadFrameConstants(rhi::DeviceDep* p_device);

        void DispatchBegin(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv);
            

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
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            int gi_sample_mode);
        void DispatchDebug(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            const ngl::render::task::RenderPassViewInfo& main_view_info, rhi::RefTextureDep hw_depth_tex, rhi::RefSrvDep hw_depth_srv,
            rhi::RefTextureDep work_tex, rhi::RefUavDep work_uav);

        void DebugDraw(rhi::GraphicsCommandListDep* p_command_list,
            rhi::ConstantBufferPooledHandle scene_cbv, 
            rhi::RefTextureDep hw_depth_tex, rhi::RefDsvDep hw_depth_dsv,
            rhi::RefTextureDep lighting_tex, rhi::RefRtvDep lighting_rtv);
        void SetDescriptor(rhi::PipelineStateBaseDep* p_pso, rhi::DescriptorSetDep* p_desc_set) const;

    private:
            bool is_initialized_ = false;
            BitmaskBrickVoxelGi* bbvgi_instance_;
    };




    class RenderTaskInstantRdvBegin : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::InstantRasterDerivedVoxelScene* p_instant_rdv = {};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_instant_rdv)
                return;

			desc_ = desc;
            
			// 定数値とリソースはMainThread側で確定済み。RTG側ではフレーム用CBだけを確保する。
			desc_.p_instant_rdv->UploadFrameConstants(p_device);

			// Render処理のLambdaをRTGに登録.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](ngl::rtg::RenderTaskGraphBuilder& builder, ngl::rtg::TaskGraphicsCommandListAllocator command_list_allocator)
				{
					command_list_allocator.Alloc(1);
					auto gfx_commandlist = command_list_allocator.GetOrCreate(0);
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskInstantRdvBegin");

                    desc_.p_instant_rdv->DispatchBegin(gfx_commandlist, desc_.scene_cbv);
				}
			);
		}
	};

    class RenderTaskInstantRdvViewBbvRadianceInjection : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::InstantRasterDerivedVoxelScene* p_instant_rdv = {};

            InjectionSourceDepthBufferViewInfo view_info{};
		};
		SetupDesc desc_{};
		
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_instant_rdv || desc.view_info.h_depth.IsInvalid() || desc.view_info.h_color.IsInvalid() || !desc.view_info.is_enable_radiance_injection_pass)
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskInstantRdvViewBbvRadianceInjection");

                    InjectionSourceDepthBufferViewInfo injection_view_info = desc_.view_info;
                    {
                        auto res_depth = builder.GetAllocatedResource(this, desc_.view_info.h_depth);
                        auto res_color = builder.GetAllocatedResource(this, desc_.view_info.h_color);
                        assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
                        assert(res_color.tex_.IsValid() && res_color.srv_.IsValid());
                        injection_view_info.hw_depth_srv = res_depth.srv_;
                        injection_view_info.hw_color_srv = res_color.srv_;
                    }

                    desc_.p_instant_rdv->DispatchViewBbvRadianceInjection(gfx_commandlist, desc_.scene_cbv, view_info, injection_view_info);
				}
			);
		}
    };
    

    class RenderTaskInstantRdvViewBbvOccupancyInjection : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::InstantRasterDerivedVoxelScene* p_instant_rdv = {};

            //ngl::rtg::RtgResourceHandle h_depth{};

            InjectionSourceDepthBufferInfo depth_buffer_info{};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_instant_rdv)
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskInstantRdvViewBbvOccupancyInjection");

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

                    
                    desc_.p_instant_rdv->DispatchViewBbvOccupancyUpdate(gfx_commandlist, desc_.scene_cbv, view_info, injection_depth_buffer_info);
				}
			);
		}
	};

    class RenderTaskInstantRdvUpdate : public ngl::rtg::IGraphicsTaskNode
    {
    public:
		ngl::rtg::RtgResourceHandle h_depth_{};

		struct SetupDesc
		{
            int w{};
            int h{};
			
            rhi::ConstantBufferPooledHandle scene_cbv{};
            render::app::InstantRasterDerivedVoxelScene* p_instant_rdv = {};
            int gi_sample_mode = 2;

            ngl::rtg::RtgResourceHandle h_depth{};
		};
		SetupDesc desc_{};
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_instant_rdv)
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskInstantRdvUpdate");

					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());

                    desc_.p_instant_rdv->DispatchUpdate(gfx_commandlist, desc_.scene_cbv, 
                        view_info, res_depth.tex_, res_depth.srv_, desc_.gi_sample_mode);
				}
			);
		}
	};

    class RenderTaskInstantRdvDebug : public ngl::rtg::IGraphicsTaskNode
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
            render::app::InstantRasterDerivedVoxelScene* p_instant_rdv = {};

            ngl::rtg::RtgResourceHandle h_depth{};
            ngl::rtg::RtgResourceHandle h_color{};
		};
		SetupDesc desc_{};

		void Setup(ngl::rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const ngl::render::task::RenderPassViewInfo& view_info,
			const SetupDesc& desc)
		{
            if(!desc.p_instant_rdv || desc.h_depth.IsInvalid() || desc.h_color.IsInvalid())
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
					NGL_RHI_GPU_SCOPED_EVENT_MARKER(gfx_commandlist, "RenderTaskInstantRdvDebug");

					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
                    auto res_work = builder.GetAllocatedResource(this, h_work_);
					assert(res_depth.tex_.IsValid() && res_depth.srv_.IsValid());
                    assert(res_work.tex_.IsValid() && res_work.uav_.IsValid());

                    desc_.p_instant_rdv->DispatchDebug(gfx_commandlist, desc_.scene_cbv,
                        view_info, res_depth.tex_, res_depth.srv_,
                        res_work.tex_, res_work.uav_);
				}
			);
		}
    };

}  // namespace ngl::render::app
