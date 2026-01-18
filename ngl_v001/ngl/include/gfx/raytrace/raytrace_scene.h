#pragma once

// -------------------
#include <cstring>
// for wchar convert.
#include <stdlib.h>
#include <codecvt>
#include <locale>
// --------------------

#include "math/math.h"

#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/resource.d3d12.h"


#include "gfx/resource/resource_mesh.h"
#include "gfx/game_scene.h"

#include "resource/resource_manager.h"
#include "gfx/resource/resource_shader.h"

namespace ngl
{
	namespace rhi
	{
		class ShaderResourceViewDep;
		class UnorderedAccessViewDep;
	}


	namespace gfx
	{
		// ASはシステムから固定のレジスタへ設定する.
		static constexpr uint32_t k_system_raytracing_structure_srv_register = 65535;

		// Local Root Signature のレジスタは 1000 スタートとする.
		static constexpr uint32_t k_system_raytracing_local_register_start = 1000;

		// Global Root Signature の各リソース数.
		static constexpr uint32_t k_rt_global_descriptor_cbvsrvuav_table_size = 16;
		static constexpr uint32_t k_rt_global_descriptor_sampler_table_size = 16;

		// Local Root Signature の各リソース数.
		static constexpr uint32_t k_rt_local_descriptor_cbvsrvuav_table_size = 16;



		struct DescriptorHandleSet
		{
			DescriptorHandleSet() { }
			DescriptorHandleSet(const D3D12_CPU_DESCRIPTOR_HANDLE& cpu_handle, const D3D12_GPU_DESCRIPTOR_HANDLE& gpu_handle)
			{
				h_cpu = cpu_handle;
				h_gpu = gpu_handle;
			}
			D3D12_CPU_DESCRIPTOR_HANDLE h_cpu = {};
			D3D12_GPU_DESCRIPTOR_HANDLE h_gpu = {};
		};



		struct RtBlasGeometryResource
		{
			const rhi::ShaderResourceViewDep* vertex_srv = nullptr;
			const rhi::ShaderResourceViewDep* index_srv = nullptr;
		};


		struct RtBlasGeometryDesc
		{
			const gfx::MeshShapePart* mesh_data = nullptr;
		};

		// BLAS.
		class RtBlas
		{
		public:
			enum SETUP_TYPE : int
			{
				NONE,
				BLAS_TRIANGLE,
			};

			RtBlas();
			~RtBlas();

			// BLAS setup. 必要なBufferやViewの生成まで実行する. Buffer上にAccelerationStructureをビルドするのはBuild関数まで遅延する.
			// index_buffer : optional.
			// bufferの管理責任は外部.
			bool Setup(rhi::DeviceDep* p_device, const std::vector<RtBlasGeometryDesc>& geometry_desc_array);

			// SetupAs... の情報を元に構造構築コマンドを発行する.
			// Buildタイミングをコントロールするために分離している.
			// MEMO. RenderDocでのLaunchはクラッシュするのでNsight推奨.
			bool Build(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_command_list);

			bool IsSetuped() const;
			bool IsBuilt() const;

			// BLAS Buffer.
			rhi::BufferDep* GetBuffer();
			// BLAS Buffer.
			const rhi::BufferDep* GetBuffer() const;

			// 内部Geometry数.
			uint32_t NumGeometry() const
			{
				return static_cast<uint32_t>(geom_desc_array_.size());
			}
			// 内部Geometry情報.
			RtBlasGeometryResource GetGeometryData(uint32_t index);

		private:
			bool is_built_ = false;

			// リソースとして頂点バッファやインデックスバッファへアクセスをするために保持.
			std::vector<RtBlasGeometryDesc> geometry_desc_array_;

			// setup data.
			SETUP_TYPE		setup_type_ = SETUP_TYPE::NONE;
			
			// build info.
			// Setupでバッファや設定を登録される. これを用いてRenderThreadでCommandListにビルドタスクを発行する.
			std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geom_desc_array_ = {};
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS build_setup_info_ = {};


			// built data.
			rhi::RhiRef<rhi::BufferDep> scratch_;
			rhi::RhiRef<rhi::BufferDep> main_;
		};

		// TLAS.
		class RtTlas
		{
		public:
			enum SETUP_TYPE : int
			{
				NONE,
				TLAS,
			};

			RtTlas();
			~RtTlas();

			// TLAS setup. 必要なBufferやViewの生成まで実行する. Buffer上にAccelerationStructureをビルドするのはBuild関数まで遅延する.
			// index_buffer : optional.
			// bufferの管理責任は外部.
			bool Setup(rhi::DeviceDep* p_device, std::vector<RtBlas*>& blas_array,
				const std::vector<uint32_t>& instance_geom_id_array,
				const std::vector<math::Mat34>& instance_transform_array,
				int hitgroup_count
			);

			// SetupAs... の情報を元に構造構築コマンドを発行する.
			// Buildタイミングをコントロールするために分離している.
			// MEMO. RenderDocでのLaunchはクラッシュするのでNsight推奨.
			bool Build(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_command_list);

			bool IsSetuped() const;
			bool IsBuilt() const;

			// Get AccelerationStructure Buffer.
			rhi::BufferDep* GetBuffer();
			const rhi::BufferDep* GetBuffer() const;
			// Get AccelerationStructure Srv.
			rhi::ShaderResourceViewDep* GetSrv();
			const rhi::ShaderResourceViewDep* GetSrv() const;


			uint32_t NumInstance() const;
			const std::vector<uint32_t>& GetInstanceBlasIndexArray() const;
			const std::vector<math::Mat34>& GetInstanceTransformArray() const;
			const std::vector<uint32_t>& GetInstanceHitgroupIndexOffsetArray() const;


			uint32_t NumBlas() const;
			const std::vector<RtBlas*>& GetBlasArray() const;

		private:
			bool is_built_ = false;

			// setup data.
			SETUP_TYPE		setup_type_ = SETUP_TYPE::NONE;

			std::vector<RtBlas*> blas_array_ = {};
			std::vector<uint32_t> instance_blas_id_array_;
			std::vector<math::Mat34> transform_array_;
			std::vector<uint32_t> instance_hitgroup_index_offset_array_;

			// build info.
			// Setupでバッファや設定を登録される. これを用いてRenderThreadでCommandListにビルドタスクを発行する.
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS build_setup_info_ = {};

			// built data.
			rhi::RhiRef<rhi::BufferDep> instance_buffer_;
			rhi::RhiRef<rhi::BufferDep> scratch_;
			rhi::RhiRef<rhi::BufferDep> main_;
			rhi::RhiRef<rhi::ShaderResourceViewDep> main_srv_;
			int tlas_byte_size_ = 0;
		};

		
		// Dxr用にRootSigなどを保持して適切に遅延破棄が必要なため
		class RtDxrObjectHolder : public rhi::RhiObjectBase
		{
		public:
			RtDxrObjectHolder();
			~RtDxrObjectHolder();

			bool Initialize(rhi::DeviceDep* p_device);
		
			Microsoft::WRL::ComPtr<ID3D12RootSignature>	global_root_signature_ = {};
			Microsoft::WRL::ComPtr<ID3D12RootSignature>	local_root_signature_fixed_ = {};
			Microsoft::WRL::ComPtr<ID3D12StateObject>	state_oject_ = {};
		};
		using RefRtDxrObjectHolder = rhi::RhiRef<RtDxrObjectHolder>;

		// RaytraceStateObjectの初期化用シェーダ情報.
		struct RtShaderRegisterInfo
		{
			struct HitgroupInfo
			{
				std::string hitgorup_name = {};
				std::string any_hit_name = {};
				std::string closest_hit_name = {};
				std::string intersection_name = {};
			};

			// 登録シェーダ.
			const rhi::ShaderDep* p_shader_library = nullptr;

			// 登録シェーダから公開するRayGenerationShader名配列.
			std::vector<std::string> ray_generation_shader_array = {};
			// 登録シェーダから公開するMissShader名配列.
			std::vector<std::string> miss_shader_array = {};

			// ShaderLib内でのHitgroup定義配列.
			//	 hitgroupを構成する関数は一つのShaderLib内に含まれているものとする (別のShaderLibからIntersectionだけを利用するといったことはしない).
			std::vector<HitgroupInfo> hitgroup_array = {};
		};

		// RaytracingのStateObject生成と管理.
		// マテリアルシェーダが動的に追加されるような状況では作り直しになるが, ShaderTable側を一旦デフォルトに割り当てて数フレーム後に作り直したStateで置き換える等を検討中.
		class RtStateObject
		{
		public:
			RtStateObject() {}
			~RtStateObject() {}

			bool Initialize(rhi::DeviceDep* p_device,
				const std::vector<RtShaderRegisterInfo>& shader_info_array, 
				uint32_t payload_byte_size = sizeof(float) * 4, uint32_t attribute_byte_size = sizeof(float) * 2, uint32_t max_trace_recursion = 1);

			ID3D12StateObject* GetStateObject() const
			{
				return ref_shader_object_set_->state_oject_.Get();
			}
			ID3D12RootSignature* GetGlobalRootSignature() const
			{
				return ref_shader_object_set_->global_root_signature_.Get();
			}

			int NumHitGroup() const
			{
				return static_cast<int>(hitgroup_database_.size());
			}
			const char* GetHitgroupName(uint32_t hitgroup_id) const 
			{
				assert(hitgroup_database_.size() > hitgroup_id);
				return hitgroup_database_[hitgroup_id].hitgorup_name.c_str();
			}

			int NumMissShader() const
			{
				return static_cast<int>(miss_database_.size());
			}
			const char* GetMissShaderName(int i) const
			{
				return  miss_database_[i].miss_name.c_str();				
			}

		private:
			bool initialized_ = false;

			// 内部管理用.
			struct RayGenerationInfo
			{
				int  shader_index = -1;// 登録元のShaderRef.
				
				std::string ray_generation_name = {};
			};
			// 内部管理用.
			struct MissInfo
			{
				int  shader_index = -1;// 登録元のShaderRef.

				std::string miss_name = {};
			};
			// 内部管理用.
			struct HitgroupInfo
			{
				int  shader_index = -1;// 登録元のShaderRef.

				std::string hitgorup_name = {};

				std::string any_hit_name = {};
				std::string closest_hit_name = {};
				std::string intersection_name = {};
			};

			std::vector<const rhi::ShaderDep*>	shader_database_ = {};
			std::vector<RayGenerationInfo>	raygen_database_ = {};
			std::vector<MissInfo>			miss_database_ = {};
			std::vector<HitgroupInfo>		hitgroup_database_ = {};


			uint32_t						payload_byte_size_ = sizeof(float)*4;
			uint32_t						attribute_byte_size_ = sizeof(float) * 2;
			uint32_t						max_trace_recursion_ = 1;

			RefRtDxrObjectHolder	ref_shader_object_set_;
		};

		class RtShaderTable
		{
		public:
			RtShaderTable() {}
			~RtShaderTable() {}


			rhi::RhiRef<rhi::BufferDep>	shader_table_;

			uint32_t		table_entry_byte_size_ = 0;

			uint32_t		table_raygen_offset_ = 0;
			uint32_t		table_miss_offset_ = 0;
			uint32_t		table_miss_count_ = 0;
			uint32_t		table_hitgroup_offset_ = 0;
			uint32_t		table_hitgroup_count_ = 0;
		};
		// 引数のTLASはSetupでRHIリソース確保等がされていれば良い(Build不要).
		//	エントリとするRayGenシェーダ名を指定する. HitGroupやMissShaderはStateObjectに登録されているものがすべて利用される.
		static bool CreateShaderTable(
			RtShaderTable& out,
			rhi::DeviceDep* p_device,
			rhi::DynamicDescriptorStackAllocatorInterface& desc_alloc_interface,
			const RtTlas& tlas, uint32_t tlas_hitgroup_count_max,
			const RtStateObject& state_object, const char* raygen_name);


		// Raytraceの基本部分を担当するクラス.
		//	実際のPassクラスから利用され, ShaderTableの設定やRtSceneとのやり取りなどを隠蔽する.
		class RtPassCore
		{
		public:
			RtPassCore();
			~RtPassCore();

			// StateObject等の生成. TODO ここももっと柔軟に対応したい.
			bool InitializeBase(rhi::DeviceDep* p_device,
				const std::vector<RtShaderRegisterInfo>& shader_info_array,
				uint32_t payload_byte_size = sizeof(float) * 4, uint32_t attribute_byte_size = sizeof(float) * 2, uint32_t max_trace_recursion = 1);
			
			// ShaderTable再生成等.
			bool UpdateScene(class RtSceneManager* p_rt_scene, const char* ray_gen_name);
			
			struct DispatchRayParam
			{
				// Global Resource.
				std::array<rhi::ConstantBufferViewDep*, k_rt_global_descriptor_cbvsrvuav_table_size>	cbv_slot = {};
				std::array<rhi::ShaderResourceViewDep*, k_rt_global_descriptor_cbvsrvuav_table_size>	srv_slot = {};
				std::array<rhi::UnorderedAccessViewDep*, k_rt_global_descriptor_cbvsrvuav_table_size>	uav_slot = {};
				std::array<rhi::SamplerDep*, k_rt_global_descriptor_sampler_table_size>					sampler_slot = {};

				// dispatch ray count.
				u32 count_x = {};
				u32 count_y = {};
			};
			// Dispatch.
			void DispatchRay(rhi::GraphicsCommandListDep* p_command_list, const DispatchRayParam& param);
		public:
			RtStateObject* GetStateObject();
			RtShaderTable* GetShaderTable();

		protected:
			// UpdateSceneでのShaderTable再生成に伴う破棄処理.
			void DestroyShaderTable();
		protected:
			rhi::DeviceDep* p_device_ = {};

			RtStateObject state_object_ = {};
			rhi::DynamicDescriptorStackAllocatorInterface	desc_alloc_interface_ = {};
			
			RtShaderTable shader_table_ = {};

			class RtSceneManager* p_rt_scene_ = {};
		};
		
		// RtPassCoreを使用したRaytracePassのサンプル.
		class RaytracePassSample
		{
		public:
			RaytracePassSample();
			~RaytracePassSample();

			// Pass毎に自由.
			// サンプルのため外部からShaderとRaygen情報などを指定する.
			bool Initialize(rhi::DeviceDep* p_device, uint32_t max_trace_recursion);

			void PreRenderUpdate(class RtSceneManager* p_rt_scene);
			void Render(rhi::GraphicsCommandListDep* p_command_list);

			ngl::res::ResourceHandle <ngl::gfx::ResShader> res_shader_lib_;
			RtPassCore	rt_pass_core_ = {};

			class RtSceneManager* p_rt_scene_ = {};

			// テスト用のRayDispatch出力先UAV.
			rhi::RhiRef<rhi::TextureDep>				ray_result_;
			rhi::RhiRef<rhi::ShaderResourceViewDep>		ray_result_srv_;
			rhi::RhiRef<rhi::UnorderedAccessViewDep>	ray_result_uav_;
			rhi::EResourceState							ray_result_state_ = {};
		};



		// Raytracing Scene 管理.
		class RtSceneManager
		{
		public:
			RtSceneManager();
			~RtSceneManager();

			// hitgroup_count_max : 現在の設計ではInstanceのHitGroupIndexの設定でhitgroup数が必要が必要なため
			bool Initialize(rhi::DeviceDep* p_device, int hitgroup_count_max);
			bool IsValid() const {return is_initialized_;}

			void UpdateOnRender(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_command_list, const SceneRepresentation& scene);

			struct DispatchRayParam
			{
				// pipeline.
				RtStateObject* p_state_object = {};
				// shader table.
				RtShaderTable* p_shader_table = {};

				// Global Resource.
				std::array<rhi::ConstantBufferViewDep*, k_rt_global_descriptor_cbvsrvuav_table_size>	cbv_slot = {};
				std::array<rhi::ShaderResourceViewDep*, k_rt_global_descriptor_cbvsrvuav_table_size>	srv_slot = {};
				std::array<rhi::UnorderedAccessViewDep*, k_rt_global_descriptor_cbvsrvuav_table_size>	uav_slot = {};
				std::array<rhi::SamplerDep*, k_rt_global_descriptor_sampler_table_size>					sampler_slot = {};

				// dispatch ray count.
				u32 count_x = {};
				u32 count_y = {};
			};
			void DispatchRay(rhi::GraphicsCommandListDep* p_command_list, const DispatchRayParam& param);

			// TLAS他のリビルド. 破棄バッファリングの関係でRenderThread実行を想定.
			void UpdateRtScene(rhi::DeviceDep* p_device, const SceneRepresentation& scene);

		public:
			rhi::DynamicDescriptorStackAllocatorInterface& GetDynamicDescriptorAllocator() { return desc_alloc_interface_; }

			RtTlas* GetSceneTlas();
			const RtTlas* GetSceneTlas() const;

			int NumHitGroupCountMax() const;
			
			rhi::ConstantBufferViewDep* GetSceneViewCbv();
			const rhi::ConstantBufferViewDep* GetSceneViewCbv() const;

			void SetCameraInfo(const math::Vec3& position, const math::Vec3& dir, const math::Vec3& up, float fov_y_radian, float aspect_ratio);

		private:
			bool is_initialized_ = false;
			uint32_t frame_count_ = 0;

			// MeshPtrからBLASへのMap.
			std::unordered_map<const ResMeshData*, int> mesh_to_blas_id_;
			// 動的更新でのBLAS管理.
			std::vector<std::shared_ptr<RtBlas>> dynamic_scene_blas_array_;

			// 動的更新TLAS.
			std::shared_ptr<RtTlas> dynamic_tlas_ = {};
			uint32_t hitgroup_count_max_ = 1;
			
			rhi::DynamicDescriptorStackAllocatorInterface	desc_alloc_interface_ = {};

			rhi::ConstantBufferPooledHandle				cbh_scene_view[2];

			math::Vec3 camera_pos_ = {};
			math::Vec3 camera_dir_ = {};
			math::Vec3 camera_up_ = {};

			float fov_y_radian_ = math::Deg2Rad(60.0f);
			float aspect_ratio_ = 16.0f / 9.0f;
		};
	}
}


namespace
{
	// Convert a wide Unicode string to an UTF8 string
	// DXRのShaderLibrary内エントリアクセスがwcharのためUtilityとして用意.
	std::string wst_to_str(const std::wstring& wstr)
	{
		if (wstr.empty()) return std::string();
		int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
		std::string strTo(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
		return strTo;
	}

	// Convert an UTF8 string to a wide Unicode String
	// DXRのShaderLibrary内エントリアクセスがwcharのためUtilityとして用意.
	std::wstring str_to_wstr(const std::string& str)
	{
		if (str.empty()) return std::wstring();
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
		std::wstring wstrTo(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
		return wstrTo;
	}
}


