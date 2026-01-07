#include "gfx/raytrace_scene.h"

#include <unordered_map>
#include <algorithm>

#include "rhi/d3d12/resource_view.d3d12.h"

#include "gfx/common_struct.h"


namespace ngl
{
	namespace gfx
	{

		RtBlas::RtBlas()
		{
		}
		RtBlas::~RtBlas()
		{
		}
		bool RtBlas::Setup(rhi::DeviceDep* p_device, const std::vector<RtBlasGeometryDesc>& geometry_desc_array)
		{
			// CommandListが不要な生成部だけを実行する.

			if (is_built_)
				return false;

			if (0 >= geometry_desc_array.size())
			{
				assert(false);
				return false;
			}

			// コピー.
			geometry_desc_array_ = geometry_desc_array;

			setup_type_ = SETUP_TYPE::BLAS_TRIANGLE;
			geom_desc_array_.clear();
			geom_desc_array_.reserve(geometry_desc_array.size());// 予約.
			for (auto& g : geometry_desc_array)
			{
				if (nullptr != g.mesh_data)
				{
					geom_desc_array_.push_back({});
					auto& geom_desc = geom_desc_array_[geom_desc_array_.size() - 1];// Tail.

					const auto vertex_ngl_format = ngl::rhi::EResourceFormat::Format_R32G32B32_FLOAT;
					auto index_ngl_format = ngl::rhi::EResourceFormat::Format_R32_UINT;

					auto rhi_position = g.mesh_data->position_.rhi_buffer_.Get();
					auto rhi_index = g.mesh_data->index_.rhi_buffer_.Get();

					geom_desc = {};
					geom_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;	// Triangle Geom.
					geom_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;	// Opaque.
					// Position Vertex BufferをBLAS Descに設定.
					geom_desc.Triangles.VertexBuffer.StartAddress = rhi_position->GetD3D12Resource()->GetGPUVirtualAddress();
					geom_desc.Triangles.VertexBuffer.StrideInBytes = rhi_position->GetElementByteSize();
					geom_desc.Triangles.VertexCount = rhi_position->getElementCount();
					geom_desc.Triangles.VertexFormat = rhi::ConvertResourceFormat(vertex_ngl_format);// DXGI_FORMAT_R32G32B32_FLOAT;// vec3 データとしてはVec4のArrayでStrideでスキップしている可能性がある.
					
					// IndexBufferが存在すれば設定.
					if (rhi_index->GetD3D12Resource())
					{
						geom_desc.Triangles.IndexBuffer = rhi_index->GetD3D12Resource()->GetGPUVirtualAddress();
						geom_desc.Triangles.IndexCount = rhi_index->getElementCount();
						if (rhi_index->GetElementByteSize() == 4)
						{
							geom_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
							index_ngl_format = ngl::rhi::EResourceFormat::Format_R32_UINT;
						}
						else if (rhi_index->GetElementByteSize() == 2)
						{
							geom_desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
							index_ngl_format = ngl::rhi::EResourceFormat::Format_R16_UINT;
						}
						else
						{
							// u16 u32 以外は未対応.
							assert(false);
							continue;
						}
					}
				}
				else
				{
					// スキップ.
					assert(false);
					continue;
				}
			}

			// ここで設定した情報はそのままBuildで利用される.
			build_setup_info_ = {};
			build_setup_info_.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			build_setup_info_.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
			build_setup_info_.NumDescs = static_cast<uint32_t>(geom_desc_array_.size());
			build_setup_info_.pGeometryDescs = geom_desc_array_.data();
			build_setup_info_.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL; // BLAS.

			// Prebuildで必要なサイズ取得.
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO build_info = {};
			p_device->GetD3D12DeviceForDxr()->GetRaytracingAccelerationStructurePrebuildInfo(&build_setup_info_, &build_info);

			// PreBuild情報からバッファ生成
			// Scratch Buffer.
			rhi::BufferDep::Desc scratch_desc = {};
			scratch_desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess;
			scratch_desc.initial_state = rhi::EResourceState::Common;// UnorderedAccessだとValidationエラー.
			scratch_desc.heap_type = rhi::EResourceHeapType::Default;
			scratch_desc.element_count = 1;
			scratch_desc.element_byte_size = (u32)build_info.ScratchDataSizeInBytes;
			scratch_.Reset(new rhi::BufferDep());
			if (!scratch_->Initialize(p_device, scratch_desc))
			{
				std::cout << "[ERROR] Initialize Rt Scratch Buffer." << std::endl;
				assert(false);
				return false;
			}
			// Main Buffer.
			rhi::BufferDep::Desc main_desc = {};
			main_desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess;
			main_desc.initial_state = rhi::EResourceState::RaytracingAccelerationStructure;
			main_desc.heap_type = rhi::EResourceHeapType::Default;
			main_desc.element_count = 1;
			main_desc.element_byte_size = (u32)build_info.ResultDataMaxSizeInBytes;
			main_.Reset(new rhi::BufferDep());
			if (!main_->Initialize(p_device, main_desc))
			{
				std::cout << "[ERROR] Initialize Rt Main Buffer." << std::endl;
				assert(false);
				return false;
			}

			// 実際にscratch_descとmain_descでASをビルドするのはCommandListにタスクとして発行するため分離する.

			return true;
		}

		// Setup の情報を元に構造構築コマンドを発行する.
		// Buildタイミングをコントロールするために分離している.
		// MEMO. RenderDocでのLaunchはクラッシュするのでNsight推奨.
		bool RtBlas::Build(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_command_list)
		{
			assert(p_device);
			assert(p_command_list);

			if (is_built_)
				return false;

			if (!IsSetuped())
			{
				// セットアップされていない.
				assert(false);
				return false;
			}

			// BLAS Build (Triangle Geometry).
			if (SETUP_TYPE::BLAS_TRIANGLE == setup_type_)
			{
				// Setupで準備した情報からASをビルドするコマンドを発行.
				// Build後は入力に利用したVertexBufferやIndexBufferは不要となるとのこと.

				// Builld.
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
				build_desc.Inputs = build_setup_info_;
				build_desc.DestAccelerationStructureData = main_->GetD3D12Resource()->GetGPUVirtualAddress();
				build_desc.ScratchAccelerationStructureData = scratch_->GetD3D12Resource()->GetGPUVirtualAddress();
				p_command_list->GetD3D12GraphicsCommandListForDxr()->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

				// UAV Barrier変更.
				p_command_list->ResourceUavBarrier(main_.Get());
			}

			is_built_ = true;
			return true;
		}
		bool RtBlas::IsSetuped() const
		{
			return SETUP_TYPE::NONE != setup_type_;
		}
		bool RtBlas::IsBuilt() const
		{
			return is_built_;
		}

		rhi::BufferDep* RtBlas::GetBuffer()
		{
			return main_.Get();
		}
		const rhi::BufferDep* RtBlas::GetBuffer() const
		{
			return main_.Get();
		}

		// 内部Geometry情報.
		RtBlasGeometryResource RtBlas::GetGeometryData(uint32_t index)
		{
			RtBlasGeometryResource ret = {};
			if (NumGeometry() <= index)
			{
				assert(false);
				return ret;
			}
			ret.vertex_srv = geometry_desc_array_[index].mesh_data->position_.rhi_srv.Get();
			ret.index_srv = geometry_desc_array_[index].mesh_data->index_.rhi_srv.Get();
			return ret;
		}

		RtTlas::RtTlas()
		{
		}
		RtTlas::~RtTlas()
		{
		}
		// TLAS setup.
		// index_buffer : optional.
		// bufferの管理責任は外部.
		bool RtTlas::Setup(rhi::DeviceDep* p_device, std::vector<RtBlas*>& blas_array,
			const std::vector<uint32_t>& instance_geom_id_array,
			const std::vector<math::Mat34>& instance_transform_array,
			int hitgroup_count
		)
		{
			if (is_built_)
				return false;

			if (0 >= blas_array.size())
			{
				assert(false);
				return false;
			}
			if (0 >= instance_transform_array.size())
			{
				assert(false);
				return false;
			}

			setup_type_ = SETUP_TYPE::TLAS;



			std::vector<int> id_remap;
			id_remap.resize(blas_array.size());

			// 有効でSetup済みのBLASのみ収集.
			blas_array_.clear();
			for (int i = 0; i < blas_array.size(); ++i)
			{
				auto& e = blas_array[i];

				if (nullptr != e && e->IsSetuped())
				{
					blas_array_.push_back(e);
					id_remap[i] = static_cast<int>(blas_array_.size() - 1);
				}
				else
				{
					id_remap[i] = -1;
				}
			}

			{
				instance_blas_id_array_.clear();
				transform_array_.clear();

				int instance_contribution_to_hitgroup = 0;
				// 参照BLASが有効なInstanceのみ収集.
				for (int i = 0; i < instance_geom_id_array.size(); ++i)
				{
					if (id_remap.size() <= instance_geom_id_array[i])
						continue;
					const int blas_id =  id_remap[instance_geom_id_array[i]];
					if (0 > blas_id)
						continue;

					
					instance_blas_id_array_.push_back(blas_id);
					
					instance_hitgroup_index_offset_array_.push_back(instance_contribution_to_hitgroup);
					instance_contribution_to_hitgroup += blas_array[blas_id]->NumGeometry() * hitgroup_count;

					transform_array_.push_back(instance_transform_array[i]);
				}
			}

			assert(0 < blas_array_.size());
			assert(0 < instance_blas_id_array_.size());

			// Instance Desc Buffer.
			const uint32_t num_instance_total = (uint32_t)transform_array_.size();
			rhi::BufferDep::Desc instance_buffer_desc = {};
			instance_buffer_desc.heap_type = rhi::EResourceHeapType::Upload;// CPUからアップロードするInstanceDataのため.
			instance_buffer_desc.initial_state = rhi::EResourceState::General;// UploadヒープのためにGeneral.
			instance_buffer_desc.element_count = num_instance_total;// Instance数を確保.
			instance_buffer_desc.element_byte_size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
			instance_buffer_.Reset(new rhi::BufferDep());
			if (!instance_buffer_->Initialize(p_device, instance_buffer_desc))
			{
				std::cout << "[ERROR] Initialize Rt Instance Buffer." << std::endl;
				assert(false);
				return false;
			}
			// Instance情報をBufferに書き込み.
			if (D3D12_RAYTRACING_INSTANCE_DESC* mapped = (D3D12_RAYTRACING_INSTANCE_DESC*)instance_buffer_->Map())
			{
				//int instance_contribution_to_hitgroup = 0;
				for (auto inst_i = 0; inst_i < transform_array_.size(); ++inst_i)
				{
					// 一応ID入れておく
					mapped[inst_i].InstanceID = inst_i;

					// このInstanceのHitGroupを示すベースインデックス. Instanceのマテリアル情報に近い.
					// DXRではTraceRay()でHitGroupIndex計算時にInstanceに対する乗算パラメータが無いため, Instance毎のHitGroupIndexContributionにHitGroup数を考慮した絶対インデックス指定が必要.
					mapped[inst_i].InstanceContributionToHitGroupIndex = instance_hitgroup_index_offset_array_[inst_i];// instance_contribution_to_hitgroup;
					
					mapped[inst_i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
					mapped[inst_i].InstanceMask = ~0u;// 0xff;
					
					// InstanceのBLASを設定.
					mapped[inst_i].AccelerationStructure = blas_array_[instance_blas_id_array_[inst_i]]->GetBuffer()->GetD3D12Resource()->GetGPUVirtualAddress();

					// InstanceのTransform.
					memcpy(mapped[inst_i].Transform, &transform_array_[inst_i], sizeof(mapped[inst_i].Transform));
				}
				instance_buffer_->Unmap();
			}



			// ここで設定した情報はそのままBuildで利用される.
			build_setup_info_ = {};
			build_setup_info_.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			build_setup_info_.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;// TLASはTrace高速設定.
			build_setup_info_.NumDescs = num_instance_total;// Instance Desc Bufferの要素数を指定.
			build_setup_info_.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL; // TLAS.
			// input情報にInstanceBufferセット.
			build_setup_info_.InstanceDescs = instance_buffer_->GetD3D12Resource()->GetGPUVirtualAddress();

			// Prebuildで必要なサイズ取得.
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO build_info = {};
			p_device->GetD3D12DeviceForDxr()->GetRaytracingAccelerationStructurePrebuildInfo(&build_setup_info_, &build_info);

			// PreBuild情報からバッファ生成
			tlas_byte_size_ = (int)build_info.ResultDataMaxSizeInBytes;
			// Scratch Buffer.
			rhi::BufferDep::Desc scratch_desc = {};
			scratch_desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess;
			scratch_desc.initial_state = rhi::EResourceState::Common;// UnorderedAccessだとValidationエラー.
			scratch_desc.heap_type = rhi::EResourceHeapType::Default;
			scratch_desc.element_count = 1;
			scratch_desc.element_byte_size = (u32)build_info.ScratchDataSizeInBytes;
			scratch_.Reset(new rhi::BufferDep());
			if (!scratch_->Initialize(p_device, scratch_desc))
			{
				std::cout << "[ERROR] Initialize Rt Scratch Buffer." << std::endl;
				assert(false);
				return false;
			}
			// Main Buffer.
			rhi::BufferDep::Desc main_desc = {};
			main_desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::ShaderResource; // シェーダからはSRVとして見えるためShaderResourceフラグも設定.
			main_desc.initial_state = rhi::EResourceState::RaytracingAccelerationStructure;
			main_desc.heap_type = rhi::EResourceHeapType::Default;
			main_desc.element_count = 1;
			main_desc.element_byte_size = (u32)build_info.ResultDataMaxSizeInBytes;
			main_.Reset(new rhi::BufferDep());
			if (!main_->Initialize(p_device, main_desc))
			{
				std::cout << "[ERROR] Initialize Rt Main Buffer." << std::endl;
				assert(false);
				return false;
			}
			// Main Srv.
			main_srv_.Reset(new rhi::ShaderResourceViewDep());
			if (!main_srv_->InitializeAsRaytracingAccelerationStructure(p_device, main_.Get()))
			{
				std::cout << "[ERROR] Initialize Rt TLAS View." << std::endl;
				assert(false);
				return false;
			}

			// 実際にscratch_descとmain_descでASをビルドするのはCommandListにタスクとして発行するため分離する.

			return true;
		}

		// Setup の情報を元に構造構築コマンドを発行する.
		// Buildタイミングをコントロールするために分離している.
		// MEMO. RenderDocでのLaunchはクラッシュするのでNsight推奨.
		bool RtTlas::Build(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_command_list)
		{
			assert(p_device);
			assert(p_command_list);

			if (is_built_)
				return false;

			if (!IsSetuped())
			{
				// セットアップされていない.
				assert(false);
				return false;
			}

			// TLAS Build .
			if (SETUP_TYPE::TLAS == setup_type_)
			{
				// ASビルドコマンドを発行.
				// 
				// Builld.
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc = {};
				build_desc.Inputs = build_setup_info_;
				build_desc.DestAccelerationStructureData = main_->GetD3D12Resource()->GetGPUVirtualAddress();
				build_desc.ScratchAccelerationStructureData = scratch_->GetD3D12Resource()->GetGPUVirtualAddress();
				p_command_list->GetD3D12GraphicsCommandListForDxr()->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);

				// UAV Barrier.
				p_command_list->ResourceUavBarrier(main_.Get());
			}

			is_built_ = true;
			return true;
		}
		bool RtTlas::IsSetuped() const
		{
			return SETUP_TYPE::NONE != setup_type_;
		}
		bool RtTlas::IsBuilt() const
		{
			return is_built_;
		}
		rhi::BufferDep* RtTlas::GetBuffer()
		{
			return main_.Get();
		}
		const rhi::BufferDep* RtTlas::GetBuffer() const
		{
			return main_.Get();
		}
		rhi::ShaderResourceViewDep* RtTlas::GetSrv()
		{
			return main_srv_.Get();
		}
		const rhi::ShaderResourceViewDep* RtTlas::GetSrv() const
		{
			return main_srv_.Get();
		}


		uint32_t RtTlas::NumBlas() const
		{
			return static_cast<uint32_t>(blas_array_.size());
		}
		const std::vector<RtBlas*>& RtTlas::GetBlasArray() const
		{
			return blas_array_;
		}
		uint32_t RtTlas::NumInstance() const
		{
			return static_cast<uint32_t>(transform_array_.size());
		}
		const std::vector<uint32_t>& RtTlas::GetInstanceBlasIndexArray() const
		{
			return instance_blas_id_array_;
		}
		const std::vector<math::Mat34>& RtTlas::GetInstanceTransformArray() const
		{
			return transform_array_;
		}
		const std::vector<uint32_t>& RtTlas::GetInstanceHitgroupIndexOffsetArray() const
		{
			return instance_hitgroup_index_offset_array_;
		}
		// -------------------------------------------------------------------------------

		
		// -------------------------------------------------------------------------------
		// DXRのShaderTable用のRootSigやShaderObjectを遅延破棄するためだけのオブジェクト.
		// RefRtDxrObjectHolderで保持することで参照カウントによって遅延破棄へ送られる.
		RtDxrObjectHolder::RtDxrObjectHolder()
		{
		}
		RtDxrObjectHolder::~RtDxrObjectHolder()
		{
		}
		bool RtDxrObjectHolder::Initialize(rhi::DeviceDep* p_device)
		{
			InitializeRhiObject(p_device);
			return true;
		}
		

		// -------------------------------------------------------------------------------
		// Raytrace用のStateObject生成のためのSubobject関連ヘルパー.
		namespace subobject
		{
			// Subobject生成簡易化.
			
			// SubobjectBuilderでの構築用Setting基底.
			class SubobjectSetting
			{
			public:
				friend class SubobjectBuilder;
				SubobjectSetting()
				{
				}
				virtual ~SubobjectSetting()
				{
				}

				void Assign(D3D12_STATE_SUBOBJECT* p_target)
				{
					assert(p_target);
					// 割当.
					p_target_ = p_target;

					// 実装側のTypeを設定.
					p_target_->Type = GetType();
					// 実装側のSubobjectデータ部を設定.
					p_target_->pDesc = GetData();
				}
				D3D12_STATE_SUBOBJECT* GetAssignedSubobject()
				{
					return p_target_;
				}
				// 派生クラスでTypeを返す.
				virtual D3D12_STATE_SUBOBJECT_TYPE GetType() const = 0;
				// 派生クラスでデータ部を返す.
				virtual void* GetData() = 0;

				// Stateを構成するすべてのSubobjectのメモリ配置が確定した後に解決すべき処理があれば実装.
				virtual void Resolve()
				{}

			public:
				const D3D12_STATE_SUBOBJECT* GetAssignedSubobject() const
				{
					return p_target_;
				}

			private:
				D3D12_STATE_SUBOBJECT* p_target_ = nullptr;
			};

			// Subobject DXIL Library.
			// 複数のシェーダを含んだオブジェクト.
			class SubobjectSettingDxilLibrary : public SubobjectSetting
			{
			public:
				SubobjectSettingDxilLibrary()
				{
				}

				D3D12_STATE_SUBOBJECT_TYPE GetType() const override
				{
					return D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
				}
				void* GetData() override
				{
					return &library_desc_;
				}

			public:
				void Setup(const rhi::ShaderDep* p_shader, const char* entry_point_array[], int num_entry_point)
				{
					library_desc_ = {};
					export_name_cache_.resize(num_entry_point);
					export_desc_.resize(num_entry_point);
					if (p_shader)
					{
						p_shader_lib_ = p_shader;

						library_desc_.DXILLibrary.pShaderBytecode = p_shader_lib_->GetShaderBinaryPtr();
						library_desc_.DXILLibrary.BytecodeLength = p_shader_lib_->GetShaderBinarySize();
						library_desc_.NumExports = num_entry_point;
						library_desc_.pExports = export_desc_.data();

						for (int i = 0; i < num_entry_point; ++i)
						{
							wchar_t tmp_ws[64];
							mbstowcs_s(nullptr, tmp_ws, entry_point_array[i], std::size(tmp_ws));
							// 内部にキャッシュ.
							export_name_cache_[i] = tmp_ws;

							export_desc_[i] = {};
							export_desc_[i].Name = export_name_cache_[i].c_str();
							export_desc_[i].Flags = D3D12_EXPORT_FLAG_NONE;
							export_desc_[i].ExportToRename = nullptr;
						}
					}
				}
			private:
				D3D12_DXIL_LIBRARY_DESC			library_desc_ = {};
				const rhi::ShaderDep* p_shader_lib_ = nullptr;
				std::vector<std::wstring>		export_name_cache_;
				std::vector<D3D12_EXPORT_DESC>	export_desc_;
			};

			// HitGroup.
			// マテリアル毎のRaytraceシェーダグループ.
			class SubobjectSettingHitGroup : public SubobjectSetting
			{
			public:
				SubobjectSettingHitGroup()
				{}

				D3D12_STATE_SUBOBJECT_TYPE GetType() const override
				{
					return D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
				}
				void* GetData() override
				{
					return &hit_group_desc_;
				}

			public:
				void Setup(const char* anyhit, const char* closesthit, const char* intersection, const char* hitgroup_name)
				{
					hit_group_desc_ = {};
					hit_group_desc_.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

					if (anyhit && ('\0' != anyhit[0]))
					{
						anyhit_name_cache_ = str_to_wstr(anyhit);
						hit_group_desc_.AnyHitShaderImport = anyhit_name_cache_.c_str();
					}
					if (closesthit && ('\0' != closesthit[0]))
					{
						closesthit_name_cache_ = str_to_wstr(closesthit);
						hit_group_desc_.ClosestHitShaderImport = closesthit_name_cache_.c_str();
					}
					if (intersection && ('\0' != intersection[0]))
					{
						intersection_name_cache_ = str_to_wstr(intersection);
						hit_group_desc_.IntersectionShaderImport = intersection_name_cache_.c_str();
					}
					if (hitgroup_name && ('\0' != hitgroup_name[0]))
					{
						hitgroup_name_cache_ = str_to_wstr(hitgroup_name);
						hit_group_desc_.HitGroupExport = hitgroup_name_cache_.c_str();
					}
				}
			private:
				D3D12_HIT_GROUP_DESC hit_group_desc_ = {};
				std::wstring anyhit_name_cache_ = {};
				std::wstring closesthit_name_cache_ = {};
				std::wstring intersection_name_cache_ = {};
				std::wstring hitgroup_name_cache_ = {};
			};


			// Shader Config.
			// RtPSOの全体に対する設定と思われる.
			class SubobjectSettingRaytracingShaderConfig : public SubobjectSetting
			{
			public:
				// デフォルト値として BuiltInTriangleIntersectionAttributes のサイズ (float2 barycentrics).
				static constexpr uint32_t k_default_attribute_size = 2 * sizeof(float);

				SubobjectSettingRaytracingShaderConfig()
				{}

				D3D12_STATE_SUBOBJECT_TYPE GetType() const override
				{
					return D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
				}
				void* GetData() override
				{
					return &shader_config_;
				}

			public:
				// RaytracingのPayloadとAttributeのサイズ.
				void Setup(uint32_t raytracing_payload_size, uint32_t raytracing_attribute_size = k_default_attribute_size)
				{
					shader_config_.MaxAttributeSizeInBytes = raytracing_attribute_size;
					shader_config_.MaxPayloadSizeInBytes = raytracing_payload_size;
				}
			private:
				D3D12_RAYTRACING_SHADER_CONFIG shader_config_ = {};
			};

			// Pipeline Config.
			// RtPSOの全体に対する設定と思われる.
			class SubobjectSettingRaytracingPipelineConfig : public SubobjectSetting
			{
			public:
				SubobjectSettingRaytracingPipelineConfig()
				{}

				D3D12_STATE_SUBOBJECT_TYPE GetType() const override
				{
					return D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
				}
				void* GetData() override
				{
					return &pipeline_config_;
				}

			public:
				// Rayの最大再帰回数.
				void Setup(uint32_t max_trace_recursion_depth)
				{
					pipeline_config_.MaxTraceRecursionDepth = max_trace_recursion_depth;
				}
			private:
				D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config_ = {};
			};

			// Global Root Signature.
			class SubobjectSettingGlobalRootSignature : public SubobjectSetting
			{
			public:
				SubobjectSettingGlobalRootSignature()
				{}

				D3D12_STATE_SUBOBJECT_TYPE GetType() const override
				{
					return D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
				}
				void* GetData() override
				{
					// NOTE. RootSignatureのポインタではなく, RootSignatureのポインタ変数のアドレス であることに注意 (これで2日溶かした).
					//return &p_root_signature_;
					return &p_ref_;
				}

			public:
				void Setup(Microsoft::WRL::ComPtr<ID3D12RootSignature> p_root_signature)
				{
					p_root_signature_ = p_root_signature;
					p_ref_ = p_root_signature_.Get();
				}
			private:
				Microsoft::WRL::ComPtr<ID3D12RootSignature> p_root_signature_;
				ID3D12RootSignature*		p_ref_ = {};
			};

			// Local Root Signature.
			class SubobjectSettingLocalRootSignature : public SubobjectSetting
			{
			public:
				SubobjectSettingLocalRootSignature()
				{}

				D3D12_STATE_SUBOBJECT_TYPE GetType() const override
				{
					return D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
				}
				void* GetData() override
				{
					// NOTE. RootSignatureのポインタではなく, RootSignatureのポインタ変数のアドレス であることに注意 (これで2日溶かした).
					//return &p_root_signature_;
					return &p_ref_;
				}

			public:
				void Setup(Microsoft::WRL::ComPtr<ID3D12RootSignature> p_root_signature)
				{
					p_root_signature_ = p_root_signature;
					p_ref_ = p_root_signature_.Get();
				}
			private:
				Microsoft::WRL::ComPtr<ID3D12RootSignature> p_root_signature_;
				ID3D12RootSignature*		p_ref_ = {};
			};

			// Association
			// 基本的には SubobjectLocalRootSignature と一対一で Local Root Signatureとシェーダレコード(shadow hitGroup等)をバインドするためのもの.
			// NVIDIAサンプルではShaderNameとShaderConfig(Payloadサイズ等)のバインドもしているように見える.
			class SubobjectSettingExportsAssociation : public SubobjectSetting
			{
			public:
				SubobjectSettingExportsAssociation()
				{}

				D3D12_STATE_SUBOBJECT_TYPE GetType() const override
				{
					return D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
				}
				void* GetData() override
				{
					return &exports_;
				}
				void Resolve() override
				{
					assert(p_associate_);
					assert(p_associate_->GetAssignedSubobject());
					// Associateターゲットの確定したSubobjectをここで解決.
					exports_.pSubobjectToAssociate = p_associate_->GetAssignedSubobject();
				}

			public:
				void Setup(const SubobjectSetting* p_associate, const char* export_name_array[], int num)
				{
					exports_ = {};

					export_name_cache_.resize(num);
					export_name_array_.resize(num);
					for (int i = 0; i < num; ++i)
					{
						wchar_t tmp_ws[64];
						mbstowcs_s(nullptr, tmp_ws, export_name_array[i], std::size(tmp_ws));
						// 内部にキャッシュ.
						export_name_cache_[i] = tmp_ws;
						// 名前配列.
						export_name_array_[i] = export_name_cache_[i].c_str();
					}


					p_associate_ = p_associate;// 現時点ではAssociate対象のSettingオブジェクトポインタのみ保持.

					exports_.pSubobjectToAssociate = nullptr;	// まだターゲットのメモリ上の配置が不明なのでnull. SubobjectBuilderによって解決される.

					exports_.pExports = export_name_array_.data();
					exports_.NumExports = num;
				}
			private:
				D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION	exports_ = {};

				const SubobjectSetting*		p_associate_ = nullptr;
				std::vector<std::wstring>	export_name_cache_;
				std::vector<const wchar_t*>	export_name_array_;
			};


			/*
				Subobject生成補助クラス
					Subobjectのセットアップや関連付けを含めた依存関係解決などを隠蔽する.


					subobject::SubobjectBuilder subobject_builder;

					// ワークバッファ上にShaderConfig SubobjectSetting生成.
					auto* p_so_shaderconfig = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingRaytracingShaderConfig>();
					// Payloadサイズなどを設定.
					p_so_shaderconfig->Setup(payload_byte_size, attribute_byte_size);

					// ワークバッファ上にLocal Root Signature SubobjectSetting生成.
					auto* p_so_lrs = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingLocalRootSignature>();
					// Root Signature設定.
					p_so_lrs->Setup(local_root_signature_fixed_);

					// ワークバッファ上にExportsAssociation SubobjectSetting生成(Subobject間の関連付け).
					auto* p_so_association = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingExportsAssociation>();
					// Local Root SignatureのSubobjectとHitgroup名の関連付けを設定.
					p_so_association->Setup(p_so_lrs, hitgroup_name_ptr_array.data(), (int)hitgroup_name_ptr_array.size());


					// ワークバッファからSubobjectをビルド.
					subobject_builder.Build();

					// ビルドしたSubobjectからStateObjectを生成.
					D3D12_STATE_OBJECT_DESC state_object_desc = {};
					state_object_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
					state_object_desc.NumSubobjects = subobject_builder.NumSubobject();
					state_object_desc.pSubobjects = subobject_builder.GetSubobject();

					// 生成.
					if (FAILED(p_device->GetD3D12DeviceForDxr()->CreateStateObject(&state_object_desc, IID_PPV_ARGS(&state_oject_))))
					{
						assert(false);
						return false;
					}

			*/
			class SubobjectBuilder
			{
			public:
				// T: SubobjectSetting派生クラス.
				template<typename T>
				T* CreateSubobjectSetting()
				{
					// 生成追加.
					auto p = new T();
					object_array_.push_back(p);
					return p;
				}

				SubobjectBuilder()
				{}
				~SubobjectBuilder()
				{
					Clear();
				}
				void Clear()
				{
					for (auto* e : object_array_)
					{
						if (e)
							delete e;
					}
					object_array_.clear();
					built_data_.clear();
				}

				void Build()
				{
					built_data_.clear();
					built_data_.resize(object_array_.size());

					for (auto i = 0; i < object_array_.size(); ++i)
					{
						object_array_[i]->Assign(&built_data_[i]);
					}

					for (auto i = 0; i < object_array_.size(); ++i)
					{
						object_array_[i]->Resolve();
					}
				}

				D3D12_STATE_SUBOBJECT* GetSubobject()
				{
					return built_data_.data();
				}
				const D3D12_STATE_SUBOBJECT* GetSubobject() const
				{
					return built_data_.data();
				}
				const uint32_t NumSubobject() const
				{
					return static_cast<uint32_t>(built_data_.size());
				}

			private:
				std::vector<SubobjectSetting*> object_array_;

				std::vector<D3D12_STATE_SUBOBJECT> built_data_;
			};

		}

		bool RtStateObject::Initialize(rhi::DeviceDep* p_device, 
			const std::vector<RtShaderRegisterInfo>& shader_info_array, 
			uint32_t payload_byte_size, uint32_t attribute_byte_size, uint32_t max_trace_recursion)
		{
			if (initialized_)
				return false;

			if (0 >= payload_byte_size || 0 >= attribute_byte_size)
			{
				assert(false);
				return false;
			}
			initialized_ = true;

			// 設定を保存.
			payload_byte_size_ = payload_byte_size;
			attribute_byte_size_ = attribute_byte_size;
			max_trace_recursion_ = max_trace_recursion;

			ref_shader_object_set_.Reset(new RtDxrObjectHolder());
			ref_shader_object_set_->Initialize(p_device);

			std::unordered_map<const rhi::ShaderDep*, int> shader_map;
			// これらの名前はStateObject内で重複禁止のためMapでチェック.
			std::unordered_map<std::string, int> raygen_map;
			std::unordered_map<std::string, int> miss_map;
			std::unordered_map<std::string, int> hitgroup_map;
			for (int i = 0; i < shader_info_array.size(); ++i)
			{
				const auto& info = shader_info_array[i];

				// ターゲットのシェーダ参照は必須.
				if (nullptr == info.p_shader_library)
				{
					assert(false);
					continue;
				}

				// 管理用シェーダ参照登録.
				if (shader_map.end() == shader_map.find(info.p_shader_library))
				{
					shader_map.insert(std::make_pair(info.p_shader_library, (int)shader_database_.size()));
					// 内部シェーダ登録.
					shader_database_.push_back(info.p_shader_library);
				}
				// 内部シェーダインデックス.
				int shader_index = shader_map.find(info.p_shader_library)->second;

				// RayGeneration.
				for (auto& register_elem : info.ray_generation_shader_array)
				{
					if (raygen_map.end() == raygen_map.find(register_elem))
					{
						raygen_map.insert(std::make_pair(register_elem, shader_index));

						RayGenerationInfo new_elem = {};
						new_elem.shader_index = shader_index;
						new_elem.ray_generation_name = register_elem;
						raygen_database_.push_back(new_elem);
					}
					else
					{
						// 同名はエラー.
						assert(false);
					}
				}
				// Miss.
				for (auto& register_elem : info.miss_shader_array)
				{
					if (miss_map.end() == miss_map.find(register_elem))
					{
						miss_map.insert(std::make_pair(register_elem, shader_index));

						MissInfo new_elem = {};
						new_elem.shader_index = shader_index;
						new_elem.miss_name = register_elem;
						miss_database_.push_back(new_elem);
					}
					else
					{
						// 同名登録はエラー.
						assert(false);
					}
				}
				// HitGroup.
				for (auto& register_elem : info.hitgroup_array)
				{
					if (hitgroup_map.end() == hitgroup_map.find(register_elem.hitgorup_name))
					{
						hitgroup_map.insert(std::make_pair(register_elem.hitgorup_name, shader_index));

						HitgroupInfo new_elem = {};
						new_elem.shader_index = shader_index;
						new_elem.hitgorup_name = register_elem.hitgorup_name;
						
						new_elem.any_hit_name = register_elem.any_hit_name;
						new_elem.closest_hit_name = register_elem.closest_hit_name;
						new_elem.intersection_name = register_elem.intersection_name;

						hitgroup_database_.push_back(new_elem);
					}
					else
					{
						// 同名登録はエラー.
						assert(false);
					}
				}
			}


			// Subobject構築.
			subobject::SubobjectBuilder subobject_builder;

			// ShaderLib.
			{
				std::vector<std::vector<const char*>> shader_function_export_info = {};
				shader_function_export_info.resize(shader_database_.size());

				auto func_push_func_name_cp = [](auto& name_vec, const std::string& name)
				{
					if (0 < name.length())
						name_vec.push_back(name.c_str());
				};

				for (const auto& e : raygen_database_)
				{
					func_push_func_name_cp(shader_function_export_info[e.shader_index], e.ray_generation_name);
				}
				for (const auto& e : miss_database_)
				{
					func_push_func_name_cp(shader_function_export_info[e.shader_index], e.miss_name);
				}

				for (const auto& e : hitgroup_database_)
				{
					func_push_func_name_cp(shader_function_export_info[e.shader_index], e.any_hit_name);
					func_push_func_name_cp(shader_function_export_info[e.shader_index], e.closest_hit_name);
					func_push_func_name_cp(shader_function_export_info[e.shader_index], e.intersection_name);
				}

				for (int si = 0; si < shader_database_.size(); ++si)
				{
					auto* p_so = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingDxilLibrary>();
					p_so->Setup(shader_database_[si], shader_function_export_info[si].data(), (int)shader_function_export_info[si].size());
				}
			}

			// Subobject Hitgroupセットアップ.
			{
				for (int hi = 0; hi < hitgroup_database_.size(); ++hi)
				{
					const auto p_hitgroup		= hitgroup_database_[hi].hitgorup_name.c_str();
					const auto p_anyhit			= hitgroup_database_[hi].any_hit_name.c_str();
					const auto p_closesthit		= hitgroup_database_[hi].closest_hit_name.c_str();
					const auto p_intersection	= hitgroup_database_[hi].intersection_name.c_str();

					auto* p_so = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingHitGroup>();
					p_so->Setup(p_anyhit, p_closesthit, p_intersection, p_hitgroup);
				}
			}

			// Global Root Signature. 
			// TODO 現状はかなり固定.
			{
				// ASは重複しないであろうレジスタにASを設定.
				// t[k_system_raytracing_structure_srv_register] -> AccelerationStructure.

				std::vector<D3D12_ROOT_PARAMETER> root_param;
				{
					// GlobalRootSignature 0 は固定でAccelerationStructure用SRV.
					root_param.push_back({});
					auto& parame_elem = root_param.back();
					parame_elem.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
					parame_elem.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // SRV Descriptor直接.
					parame_elem.Descriptor.ShaderRegister = k_system_raytracing_structure_srv_register; // システムからASを設定.
					parame_elem.Descriptor.RegisterSpace = 0; // space 0
				}


				// CBV		-> GlobalRoot Table1. b0.
				// SRV		-> GlobalRoot Table2. t0.
				// Sampler	-> GlobalRoot Table3. s0.
				// UAV		-> GlobalRoot Table4. u0.

				std::vector<D3D12_DESCRIPTOR_RANGE> range_array;
				range_array.resize(4);
				range_array[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
				range_array[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
				range_array[0].NumDescriptors = k_rt_global_descriptor_cbvsrvuav_table_size;
				range_array[0].BaseShaderRegister = 0;// バインド先開始レジスタ.
				range_array[0].RegisterSpace = 0;

				range_array[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				range_array[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
				range_array[1].NumDescriptors = k_rt_global_descriptor_cbvsrvuav_table_size;
				range_array[1].BaseShaderRegister = 0;// バインド先開始レジスタ.
				range_array[1].RegisterSpace = 0;

				range_array[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
				range_array[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
				range_array[2].NumDescriptors = k_rt_global_descriptor_sampler_table_size;
				range_array[2].BaseShaderRegister = 0;// バインド先開始レジスタ.
				range_array[2].RegisterSpace = 0;

				range_array[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
				range_array[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
				range_array[3].NumDescriptors = k_rt_global_descriptor_cbvsrvuav_table_size;
				range_array[3].BaseShaderRegister = 0;// バインド先開始レジスタ.
				range_array[3].RegisterSpace = 0;

				for(auto i = 0; i < range_array.size(); ++i)
				{
					// GlobalRootSignature Parameter[1] 以降は色々固定のTable.

					root_param.push_back({});
					auto& parame_elem = root_param.back();
					parame_elem.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
					parame_elem.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					parame_elem.DescriptorTable.NumDescriptorRanges = 1; // 1Table 1用途.
					parame_elem.DescriptorTable.pDescriptorRanges = &range_array[i];
				}

				if (!rhi::helper::SerializeAndCreateRootSignature(ref_shader_object_set_->global_root_signature_, p_device, root_param.data(), (uint32_t)root_param.size()))
				{
					assert(false);
					return false;
				}
			}
			auto* p_so_grs = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingGlobalRootSignature>();
			p_so_grs->Setup(ref_shader_object_set_->global_root_signature_);

			// Local Root Signature.
			{
				// local_root_signature_fixed_
				std::vector<D3D12_ROOT_PARAMETER> root_param;

				// 現状ではLocal側はCBVとSRVのみで, SamplerやUAVはGlobalにしか登録しないことを考えている.
				std::vector<D3D12_DESCRIPTOR_RANGE> range_array;
				range_array.resize(2);

				range_array[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
				range_array[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // LocalRootSigだとこれが使えるかわからないのでダメだったら自前でオフセット値入れる.
				range_array[0].BaseShaderRegister = k_system_raytracing_local_register_start;
				range_array[0].NumDescriptors = k_rt_local_descriptor_cbvsrvuav_table_size;
				range_array[0].RegisterSpace = 0;

				range_array[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				range_array[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // LocalRootSigだとこれが使えるかわからないのでダメだったら自前でオフセット値入れる.
				range_array[1].BaseShaderRegister = k_system_raytracing_local_register_start;
				range_array[1].NumDescriptors = k_rt_local_descriptor_cbvsrvuav_table_size;
				range_array[1].RegisterSpace = 0;

				for (auto i = 0; i < range_array.size(); ++i)
				{
					root_param.push_back({});
					auto& parame_elem = root_param.back();
					parame_elem.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
					parame_elem.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
					parame_elem.DescriptorTable.NumDescriptorRanges = 1;
					parame_elem.DescriptorTable.pDescriptorRanges = &range_array[i];
				}

				if (!rhi::helper::SerializeAndCreateLocalRootSignature(ref_shader_object_set_->local_root_signature_fixed_, p_device, root_param.data(), (uint32_t)root_param.size()))
				{
					assert(false);
					return false;
				}
			}
			auto* p_so_lrs = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingLocalRootSignature>();
			p_so_lrs->Setup(ref_shader_object_set_->local_root_signature_fixed_);


			// Local Root SignatureとHitGroupの関連付け.
			{
				std::vector<const char*> hitgroup_name_ptr_array;
				hitgroup_name_ptr_array.resize(hitgroup_database_.size());
				for (auto i = 0; i < hitgroup_database_.size(); ++i)
				{
					hitgroup_name_ptr_array[i] = hitgroup_database_[i].hitgorup_name.c_str();
				}

				auto* p_so_association = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingExportsAssociation>();
				// Local Root Sig との関連付け.
				p_so_association->Setup(p_so_lrs, hitgroup_name_ptr_array.data(), (int)hitgroup_name_ptr_array.size());
			}

			// Shader Config.
			auto* p_so_shaderconfig = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingRaytracingShaderConfig>();
			p_so_shaderconfig->Setup(payload_byte_size, attribute_byte_size);

			// Pipeline Config.
			auto* p_so_pipelineconfig = subobject_builder.CreateSubobjectSetting<subobject::SubobjectSettingRaytracingPipelineConfig>();
			p_so_pipelineconfig->Setup(max_trace_recursion);



			// ビルド. Native Subobjectとしての関連付けも確定.
			subobject_builder.Build();

			D3D12_STATE_OBJECT_DESC state_object_desc = {};
			state_object_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
			state_object_desc.NumSubobjects = subobject_builder.NumSubobject();
			state_object_desc.pSubobjects = subobject_builder.GetSubobject();

			// 生成.
			if (FAILED(p_device->GetD3D12DeviceForDxr()->CreateStateObject(&state_object_desc, IID_PPV_ARGS(&(ref_shader_object_set_->state_oject_)))))
			{
				assert(false);
				return false;
			}

			return true;
		}
		// -------------------------------------------------------------------------------


		// per_entry_descriptor_param_count が0だとAlignmentエラーになるため注意.
		// BLAS内Geometryは個別のShaderRecordを持つ(multiplier_for_subgeometry_index = 1)
		bool CreateShaderTable(RtShaderTable& out, rhi::DeviceDep* p_device,
			rhi::DynamicDescriptorStackAllocatorInterface& desc_alloc_interface,
			const RtTlas& tlas,
			uint32_t tlas_hitgroup_count_max,
			const RtStateObject& state_object, const char* raygen_name)
		{
			out = {};

			// ダミー用のcbv, srv, uav用デフォルトDescriptor取得.
			auto def_descriptor = p_device->GetPersistentDescriptorAllocator()->GetDefaultPersistentDescriptor();


			// NOTE. 固定のDescriptorTableで CVBとSRVの2テーブルをLocalRootSignatureのリソースとして定義している.
			const uint32_t per_entry_descriptor_table_count = 2;
			
			const auto num_instance = tlas.NumInstance();
			uint32_t num_all_instance_geometry = 0;
			{
				const auto& instance_blas_index_array = tlas.GetInstanceBlasIndexArray();
				const auto& blas_array = tlas.GetBlasArray();
				for (auto i = 0u; i < num_instance; ++i)
				{
					num_all_instance_geometry += blas_array[instance_blas_index_array[i]]->NumGeometry();
				}
			}

			// Shader Table.
			// TODO. ASのインスタンス毎のマテリアルシェーダ情報からStateObjectのShaderIdentifierを取得してテーブルを作る.
			// https://github.com/Monsho/D3D12Samples/blob/95d1c3703cdcab816bab0b5dcf1a1e42377ab803/Sample013/src/main.cpp
			// https://github.com/microsoft/DirectX-Specs/blob/master/d3d/Raytracing.md#shader-tables

			constexpr uint32_t k_shader_identifier_byte_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
			// Table一つにつきベースのGPU Descriptor Handleを書き込むためのサイズ計算.
			const uint32_t shader_record_resource_byte_size = sizeof(D3D12_GPU_DESCRIPTOR_HANDLE) * per_entry_descriptor_table_count;

			const uint32_t shader_record_byte_size = rhi::align_to(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, k_shader_identifier_byte_size + shader_record_resource_byte_size);

			// 現状は全Instanceが別Table.
			// RayGenは一つ.
			constexpr uint32_t num_raygen = 1;
			// Missは複数登録可能とする.
			const uint32_t num_miss = state_object.NumMissShader();
			// HitGroup.
			const uint32_t hit_group_count = state_object.NumHitGroup();
			// 現在の実装ではTLAS側のInstance毎のHitgroupIndex決定時に最大Hitgroup数が必要なため, 実際のShaderTable側のHitgroup数はそれよりも多くなることは許可されない.
			assert(hit_group_count <= tlas_hitgroup_count_max);
			
			// Hitgroupのrecordは全Instanceの全Geometry*Hitgourp数としている.
			const uint32_t table_hitgroup_count = (num_all_instance_geometry * tlas_hitgroup_count_max);
			const uint32_t shader_table_byte_size = shader_record_byte_size * (num_raygen + num_miss + table_hitgroup_count);


			// あとで書き込み位置調整に使うので保存.
			out.table_entry_byte_size_ = shader_record_byte_size;

			// バッファ確保.
			rhi::BufferDep::Desc rt_shader_table_desc = {};
			rt_shader_table_desc.element_count = 1;
			rt_shader_table_desc.element_byte_size = shader_table_byte_size;
			rt_shader_table_desc.heap_type = rhi::EResourceHeapType::Upload;// CPUから直接書き込むため.
			rt_shader_table_desc.initial_state = rhi::EResourceState::General;// UploadヒープのためGeneral.
			out.shader_table_.Reset(new rhi::BufferDep());
			if (!out.shader_table_->Initialize(p_device, rt_shader_table_desc))
			{
				assert(false);
				return false;
			}

			// レコード書き込み.
			if (auto* mapped = static_cast<uint8_t*>(out.shader_table_->Map()))
			{
				Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> p_rt_so_prop;
				if (FAILED(state_object.GetStateObject()->QueryInterface(IID_PPV_ARGS(&p_rt_so_prop))))
				{
					assert(false);
				}

				uint32_t table_cnt = 0;

				// raygen
				out.table_raygen_offset_ = (shader_record_byte_size * table_cnt);
				{
					{
						memcpy(mapped + out.table_raygen_offset_, p_rt_so_prop->GetShaderIdentifier(str_to_wstr(raygen_name).c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

						// TODO. Local Root Signature で設定するリソースがある場合はここでGPU Descriptor Handleを書き込む.
					}
					++table_cnt;
				}

				out.table_miss_offset_ = (shader_record_byte_size * table_cnt);

				// 初期化時にSOに登録したMissShaderを全て設定.
				for(uint32_t mi = 0; mi < num_miss; ++mi)
				{
					const void* shader_identifire = p_rt_so_prop->GetShaderIdentifier(str_to_wstr(state_object.GetMissShaderName(mi)).c_str());
					if(shader_identifire)
					{
						memcpy(mapped + (shader_record_byte_size * table_cnt), shader_identifire, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
						++table_cnt;
					}
				}

				// HitGroup
				/// マテリアル分存在するHitGroupは連続領域でInstanceに指定したインデックスでアクセスされるためここ以降に順序に気をつけて書き込み.
				// InstanceのBLASに複数のGeometryが含まれる場合はここでその分のrecordが書き込まれる.

				const auto table_hitgroup_offset = shader_record_byte_size * table_cnt;
				for (uint32_t inst_i = 0u; inst_i < num_instance; ++inst_i)
				{
					// 内部Geometry毎にRecord.
					const auto& blas_index = tlas.GetInstanceBlasIndexArray()[inst_i];
					const auto& blas = tlas.GetBlasArray()[blas_index];

					const auto hitgroup_table_index_offset = tlas.GetInstanceHitgroupIndexOffsetArray()[inst_i];
					
					for (uint32_t geom_i = 0; geom_i < blas->NumGeometry(); ++geom_i)
					{
						// Geometry毎に連続領域にHitGroup書き込み. ShaderObject側のHitGroup定義主導でTable作成.
						for(uint32_t hitgroup_index = 0; hitgroup_index < hit_group_count; ++hitgroup_index)
						{
							const auto inst_geom_hitgroup_table_index =  hitgroup_table_index_offset + (geom_i * tlas_hitgroup_count_max) + hitgroup_index;
							
							const char* hitgroup_name = state_object.GetHitgroupName(hitgroup_index);
							assert(nullptr != hitgroup_name);
							
							auto* geom_hit_group_name = hitgroup_name;
							
							// hitGroup
							{
								// 固定LocalRootSigにより
								//	DescriptorTable0 -> b1000からCBV最大16
								//	DescriptorTable1 -> t1000からSRV最大16
								// というレイアウトで登録する.

								// Entry毎のSrvセットアップ.
								int l_srv_count = 0;
								std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_rt_local_descriptor_cbvsrvuav_table_size> l_srv_handles;
								{
									const auto geom_data = blas->GetGeometryData(geom_i);
									assert(geom_data.vertex_srv);
									assert(geom_data.index_srv);

									l_srv_handles[l_srv_count++] = geom_data.vertex_srv->GetView().cpu_handle;
									l_srv_handles[l_srv_count++] = geom_data.index_srv->GetView().cpu_handle;
								}

								// Entry毎のCbvセットアップ.
								int l_cbv_count = 0;
								std::array<D3D12_CPU_DESCRIPTOR_HANDLE, k_rt_local_descriptor_cbvsrvuav_table_size> l_cbv_handles;
								{
									// TODO.
								}


								DescriptorHandleSet desc_handle_srv;
								DescriptorHandleSet desc_handle_cbv;
								// 描画用HeapにDescriptorコピー.
								{
									const auto desc_stride = desc_alloc_interface.GetManager()->GetHandleIncrementSize();
									auto func_get_offseted_desc_handle = [](const D3D12_CPU_DESCRIPTOR_HANDLE& h_cpu, uint32_t offset)
									{
										D3D12_CPU_DESCRIPTOR_HANDLE ret = h_cpu;
										ret.ptr += offset;
										return ret;
									};

									// 少なくとも1つは確保する.
									const bool result_alloc_desc_srv = desc_alloc_interface.Allocate(std::max(l_srv_count, 1), desc_handle_srv.h_cpu, desc_handle_srv.h_gpu);
									assert(result_alloc_desc_srv);
									// 有効なViewをコピー.
									for (int l_srv_i = 0; l_srv_i < l_srv_count; ++l_srv_i)
									{
										p_device->GetD3D12Device()->CopyDescriptorsSimple(1, func_get_offseted_desc_handle(desc_handle_srv.h_cpu, desc_stride * l_srv_i), l_srv_handles[l_srv_i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
									}

									// 少なくとも1つは確保する.
									const bool result_alloc_desc_cbv = desc_alloc_interface.Allocate(std::max(l_cbv_count, 1), desc_handle_cbv.h_cpu, desc_handle_cbv.h_gpu);
									assert(result_alloc_desc_cbv);
									// 有効なViewをコピー.
									for (int l_cbv_i = 0; l_cbv_i < l_cbv_count; ++l_cbv_i)
									{
										p_device->GetD3D12Device()->CopyDescriptorsSimple(1, func_get_offseted_desc_handle(desc_handle_cbv.h_cpu, desc_stride * l_cbv_i), l_cbv_handles[l_cbv_i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
									}
								}

								// 書き込み
							
								// Shader Identifier
								memcpy(mapped + (table_hitgroup_offset + shader_record_byte_size * inst_geom_hitgroup_table_index), p_rt_so_prop->GetShaderIdentifier(str_to_wstr(geom_hit_group_name).c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
							
								auto record_res_offset = (table_hitgroup_offset + shader_record_byte_size * inst_geom_hitgroup_table_index) + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

								// CBV Table
								memcpy(mapped + record_res_offset, &desc_handle_cbv.h_gpu, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
								record_res_offset += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);

								// SRV Table
								memcpy(mapped + record_res_offset, &desc_handle_srv.h_gpu, sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
								record_res_offset += sizeof(D3D12_GPU_DESCRIPTOR_HANDLE);
							}
						}
						
					}
				}
				out.table_hitgroup_offset_ = table_hitgroup_offset;
				out.table_hitgroup_count_ = table_hitgroup_count;

				out.shader_table_->Unmap();
			}
			return true;
		}
		// -------------------------------------------------------------------------------

		RtPassCore::RtPassCore()
		{
		}
		RtPassCore::~RtPassCore()
		{
			DestroyShaderTable();
		}
		bool RtPassCore::InitializeBase(rhi::DeviceDep* p_device,
			const std::vector<RtShaderRegisterInfo>& shader_info_array,
			uint32_t payload_byte_size, uint32_t attribute_byte_size, uint32_t max_trace_recursion)
		{
			p_device_ = p_device;
			assert(p_device_);

			// Descriptor確保用Interface初期化.
			{
				rhi::DynamicDescriptorStackAllocatorInterface::Desc descriptor_interface_desc = {};
				if (!desc_alloc_interface_.Initialize(p_device_->GeDynamicDescriptorManager(), descriptor_interface_desc))
				{
					assert(false);
					return false;
				}
			}

			// StateObject.
			if (!state_object_.Initialize(p_device_, shader_info_array, payload_byte_size, attribute_byte_size, max_trace_recursion))
			{
				assert(false);
				return false;
			}

			return true;
		}
		void RtPassCore::DestroyShaderTable()
		{
			// ShaderTableに利用したDynamicDescriptorを安全に破棄.
			desc_alloc_interface_.DeallocateDeferred((u32)desc_alloc_interface_.GetManager()->GetDevice()->GetDeviceFrameIndex());

			// shader table解放.
			shader_table_ = {};
		}
		bool RtPassCore::UpdateScene(RtSceneManager* p_rt_scene, const char* ray_gen_name)
		{
			// Scene用にShaderTable生成.
			assert(p_device_);
			assert(p_rt_scene);
			assert(p_rt_scene->GetSceneTlas());

			p_rt_scene_ = p_rt_scene;

			// 古いShaderTableを破棄.
			DestroyShaderTable();

			// ShaderTable生成. Local Resource等の設定.
			if (!CreateShaderTable(shader_table_,
				p_device_,
				desc_alloc_interface_,
				*p_rt_scene->GetSceneTlas(), p_rt_scene->NumHitGroupCountMax(),
				state_object_, ray_gen_name))
			{
				assert(false);
				return false;
			}

			return true;
		}
		void RtPassCore::DispatchRay(rhi::GraphicsCommandListDep* p_command_list, const DispatchRayParam& param)
		{
			assert(p_rt_scene_);

			RtSceneManager::DispatchRayParam dispatch_param = {};
			dispatch_param.count_x = param.count_x;
			dispatch_param.count_y = param.count_y;
			dispatch_param.p_state_object = &state_object_;
			dispatch_param.p_shader_table = &shader_table_;

			// global resourceのセット (arrayのコピーだがこの関数が呼ばれる回数自体は少ないはずなのでとりあえずこのまま).
			{
				dispatch_param.cbv_slot = param.cbv_slot;
			}
			{
				dispatch_param.srv_slot = param.srv_slot;
			}
			{
				dispatch_param.uav_slot = param.uav_slot;
			}
			{
				dispatch_param.sampler_slot = param.sampler_slot;
			}

			// dispatch.
			p_rt_scene_->DispatchRay(p_command_list, dispatch_param);
		}
		RtStateObject* RtPassCore::GetStateObject()
		{
			return &state_object_;
		}
		RtShaderTable* RtPassCore::GetShaderTable()
		{
			return &shader_table_;
		}

		// ------------------------------------------------------------------------------------------------------------------------------------
		RaytracePassSample::RaytracePassSample()
		{
		}
		RaytracePassSample::~RaytracePassSample()
		{
		}
		bool RaytracePassSample::Initialize(rhi::DeviceDep* p_device, uint32_t max_trace_recursion)
		{
			// Shaderセットアップ.
			{
				auto& ResourceMan = ngl::res::ResourceManager::Instance();

				ngl::gfx::ResShader::LoadDesc loaddesc = {};
				loaddesc.stage = ngl::rhi::EShaderStage::ShaderLibrary;
				loaddesc.shader_model_version = "6_3";
				res_shader_lib_ = ResourceMan.LoadResource<ngl::gfx::ResShader>(p_device, "./shader/dxr_sample_lib.hlsl", &loaddesc);
			}

			// StateObject生成.
			std::vector<ngl::gfx::RtShaderRegisterInfo> shader_reg_info_array = {};
			{
				// Shader登録エントリ新規.
				auto shader_index = shader_reg_info_array.size();
				shader_reg_info_array.push_back({});

				// ShaderLibバイナリ.
				shader_reg_info_array[shader_index].p_shader_library = &res_shader_lib_->data_;

				// シェーダから公開するRayGen名.
				shader_reg_info_array[shader_index].ray_generation_shader_array.push_back("rayGen");

				// シェーダから公開するMissShader名.
				shader_reg_info_array[shader_index].miss_shader_array.push_back("miss");
				shader_reg_info_array[shader_index].miss_shader_array.push_back("miss2");

				// HitGroup関連情報.
				{
					auto hg_index = shader_reg_info_array[shader_index].hitgroup_array.size();
					shader_reg_info_array[shader_index].hitgroup_array.push_back({});

					shader_reg_info_array[shader_index].hitgroup_array[hg_index].hitgorup_name = "hitGroup";
					// このHitGroupはClosestHitのみ.
					shader_reg_info_array[shader_index].hitgroup_array[hg_index].closest_hit_name = "closestHit";
				}
				{
					auto hg_index = shader_reg_info_array[shader_index].hitgroup_array.size();
					shader_reg_info_array[shader_index].hitgroup_array.push_back({});

					shader_reg_info_array[shader_index].hitgroup_array[hg_index].hitgorup_name = "hitGroup2";
					// このHitGroupはClosestHitのみ.
					shader_reg_info_array[shader_index].hitgroup_array[hg_index].closest_hit_name = "closestHit2";
				}
			}

			uint32_t payload_byte_size = sizeof(float) * 4;// Payloadのサイズ.
			uint32_t attribute_byte_size = sizeof(float) * 2;// BuiltInTriangleIntersectionAttributes の固定サイズ.
			if (!rt_pass_core_.InitializeBase(p_device, shader_reg_info_array, payload_byte_size, attribute_byte_size, max_trace_recursion))
			{
				assert(false);
				return false;
			}

			// 出力テスト用のTextureとUAV.
			{
				rhi::TextureDep::Desc tex_desc = {};
				tex_desc.type = rhi::ETextureType::Texture2D;
				tex_desc.format = rhi::EResourceFormat::Format_R8G8B8A8_UNORM;
				tex_desc.bind_flag = rhi::ResourceBindFlag::UnorderedAccess | rhi::ResourceBindFlag::ShaderResource;
				tex_desc.width = 1920;
				tex_desc.height = 1080;

				ray_result_.Reset(new rhi::TextureDep());
				if (!ray_result_->Initialize(p_device, tex_desc))
				{
					assert(false);
				}
				ray_result_uav_.Reset(new rhi::UnorderedAccessViewDep());
				if (!ray_result_uav_->InitializeRwTexture(p_device, ray_result_.Get(), 0, 0, 1))
				{
					assert(false);
				}
				ray_result_srv_.Reset(new rhi::ShaderResourceViewDep());
				if (!ray_result_srv_->InitializeAsTexture(p_device, ray_result_.Get(), 0, 1, 0, 1))
				{
					assert(false);
				}
				// 初期ステート保存.
				ray_result_state_ = tex_desc.initial_state;
			}


			return true;
		}
		void RaytracePassSample::PreRenderUpdate(class RtSceneManager* p_rt_scene)
		{
			p_rt_scene_ = p_rt_scene;
			rt_pass_core_.UpdateScene(p_rt_scene, "rayGen");
		}
		void RaytracePassSample::Render(rhi::GraphicsCommandListDep* p_command_list)
		{
			rhi::DeviceDep* p_device = p_command_list->GetDevice();

			// Resource State Transition.
			{
				// 出力先UAVバリア.
				p_command_list->ResourceBarrier(ray_result_.Get(), ray_result_state_, rhi::EResourceState::UnorderedAccess);
				ray_result_state_ = rhi::EResourceState::UnorderedAccess;
			}
			
			struct RaytraceInfo
			{
				// レイタイプの種類数, (== hitgroup数). ShaderTable構築時に登録されたHitgroup数.
				//	TraceRay()での multiplier_for_subgeometry_index に使用するために必要とされる.
				//		ex) Primary, Shadow の2種であれば 2.
				int num_ray_type;
			};
			auto raytrace_cbh = p_command_list->GetDevice()->GetConstantBufferPool()->Alloc(sizeof(RaytraceInfo));
			if(auto* mapped = raytrace_cbh->buffer_.MapAs<RaytraceInfo>())
			{
				mapped->num_ray_type = p_rt_scene_->NumHitGroupCountMax();

				raytrace_cbh->buffer_.Unmap();
			}
			

			// Ray Dispatch.
			{
				RtPassCore::DispatchRayParam param = {};
				param.count_x = ray_result_.Get()->GetWidth();
				param.count_y = ray_result_.Get()->GetHeight();
				// global resourceのセット.
				{
					param.cbv_slot[0] = p_rt_scene_->GetSceneViewCbv();// View.
					param.cbv_slot[1] = &raytrace_cbh->cbv_;// Raytrace.
				}
				{
					param.srv_slot;
				}
				{
					param.uav_slot[0] = ray_result_uav_.Get();//出力UAV.
				}
				{
					param.sampler_slot;
				}

				// dispatch.
				rt_pass_core_.DispatchRay(p_command_list, param);
			}

			// Resource State Transition.
			{
				// to SRV.
				p_command_list->ResourceBarrier(ray_result_.Get(), ray_result_state_, rhi::EResourceState::ShaderRead);
				ray_result_state_ = rhi::EResourceState::ShaderRead;
			}
		}
		// ------------------------------------------------------------------------------------------------------------------------------------


		RtSceneManager::RtSceneManager()
		{
		}
		RtSceneManager::~RtSceneManager()
		{
			// 内部で使用しているDescriptorのDeallocをDescriptorAllocatorInterfaceの解放より先に明示的に実行.
			dynamic_tlas_.reset();
		}
		bool RtSceneManager::Initialize(rhi::DeviceDep* p_device, int hitgroup_count_max)
		{
			if(!p_device->IsSupportDxr())
			{
				is_initialized_ = false;
				return false;
			}

			assert(0 < hitgroup_count_max);
			hitgroup_count_max_ = hitgroup_count_max;
			
			// Descriptor確保用Interface初期化.
			{
				rhi::DynamicDescriptorStackAllocatorInterface::Desc descriptor_interface_desc = {};
				desc_alloc_interface_.Initialize(p_device->GeDynamicDescriptorManager(), descriptor_interface_desc);
			}

			// SceneView定数バッファ.
			for (auto i = 0; i < std::size(cbh_scene_view); ++i)
			{
				cbh_scene_view[i] = p_device->GetConstantBufferPool()->Alloc(sizeof(CbSceneView));
			}

			is_initialized_ = true;
			return true;
		}

		void RtSceneManager::UpdateRtScene(rhi::DeviceDep* p_device, const SceneRepresentation& scene)
		{
			if(!is_initialized_)
				return;
			
			// 現在SceneでのMesh情報収集.
			std::unordered_map<const ResMeshData*, int> scene_mesh_to_id;
			std::vector<const ResMeshData*> scene_mesh_array;
			std::vector<int> scene_inst_mesh_id_array;
			auto* proxy_buffer = scene.gfx_scene_->GetEntityProxyBuffer<fwk::GfxSceneEntityMesh>();
			for (auto& e : scene.mesh_proxy_id_array_)
			{
				auto* p_mesh = proxy_buffer->proxy_buffer_[e.GetIndex()]->model_->GetResMeshData();
				//auto* p_mesh = e->GetMeshData();
				if (scene_mesh_to_id.end() == scene_mesh_to_id.find(p_mesh))
				{
					scene_mesh_to_id[p_mesh] = (int)scene_mesh_array.size();
					scene_mesh_array.push_back(p_mesh);
				}

				scene_inst_mesh_id_array.push_back(scene_mesh_to_id[p_mesh]);
			}

			// BLASを必要に応じて構築.
			std::vector<int> scene_mesh_blas_id_array;
			for (auto& e : scene_mesh_array)
			{
				auto* p_mesh = e;

				// BLASが存在しない場合.
				if (mesh_to_blas_id_.end() == mesh_to_blas_id_.find(p_mesh))
				{
					// 空きスロット.
					auto find_pos = std::find_if(dynamic_scene_blas_array_.begin(), dynamic_scene_blas_array_.end(), [](const auto& e) {return nullptr == e.get(); });
					int empty_index = -1;
					if (dynamic_scene_blas_array_.end() != find_pos)
					{
						empty_index = (int)std::distance(dynamic_scene_blas_array_.begin(), find_pos);
					}
					else
					{
						empty_index = (int)dynamic_scene_blas_array_.size();
						dynamic_scene_blas_array_.push_back({});
					}

					// New Blas.
					auto new_blas = new RtBlas();
					// データベース登録.
					dynamic_scene_blas_array_[empty_index].reset(new_blas);
					// Map登録.
					mesh_to_blas_id_[p_mesh] = empty_index;


					// BLAS Setup.
					const auto& p_data = p_mesh->data_;
					std::vector<RtBlasGeometryDesc> blas_geom_desc_arrray = {};
					blas_geom_desc_arrray.reserve(p_data.shape_array_.size());

					for (uint32_t gi = 0; gi < p_data.shape_array_.size(); ++gi)
					{
						blas_geom_desc_arrray.push_back({});
						auto& geom_desc = blas_geom_desc_arrray[blas_geom_desc_arrray.size() - 1];
						geom_desc.mesh_data = &p_data.shape_array_[gi];
					}

					// Setup.
					new_blas->Setup(p_device, blas_geom_desc_arrray);
				}

				scene_mesh_blas_id_array.push_back(mesh_to_blas_id_[p_mesh]);
			}

			// TODO. 一定期間参照されなかったBLASの破棄をする.

			// 


			// TLAS Setup.
			std::vector<RtBlas*> scene_blas_array;
			std::vector<math::Mat34> scene_inst_transform_array;
			std::vector<uint32_t> scene_inst_blas_id_array;
			for (auto e : scene_mesh_blas_id_array)
			{
				scene_blas_array.push_back(dynamic_scene_blas_array_[e].get());
			}
			for (auto i = 0; i < scene.mesh_proxy_id_array_.size(); ++i)
			{
				auto* proxy = proxy_buffer->proxy_buffer_[scene.mesh_proxy_id_array_[i].GetIndex()];
				scene_inst_transform_array.push_back(proxy->transform_);
				
				scene_inst_blas_id_array.push_back(scene_inst_mesh_id_array[i]);
			}

			// 新規TLAS. DynamicTlasSet内部のRHIオブジェクトは全てRhiRef管理で安全に遅延破棄されるはず.
			dynamic_tlas_.reset(new RtTlas());
			// TLAS Setup.
			if (!dynamic_tlas_->Setup(p_device, scene_blas_array, scene_inst_blas_id_array, scene_inst_transform_array, hitgroup_count_max_))
			{
				assert(false);
			}
		}

		void RtSceneManager::UpdateOnRender(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_command_list, const SceneRepresentation& scene)
		{
			if(!is_initialized_)
				return;
			
			++frame_count_;
			const uint32_t safe_frame_count_ = frame_count_ % 10000;

			// 動的Scene.
			{
				// ASのセットアップやShaderTable構築等.
				UpdateRtScene(p_device, scene);
				
				// BLASのビルドが必要なものをビルド.
				for (auto& e : dynamic_scene_blas_array_)
				{
					if (e.get() && e->IsSetuped() && !e->IsBuilt())
					{
						e->Build(p_device, p_command_list);
					}
				}

				// TLAS ビルド.
				if (dynamic_tlas_.get() && 
					dynamic_tlas_->IsSetuped() &&
					!dynamic_tlas_->IsBuilt()
					)
				{
					dynamic_tlas_->Build(p_device, p_command_list);
				}
			}

			math::Mat34 view_mat = math::CalcViewMatrix(camera_pos_, camera_dir_, camera_up_);

			const float fov_y = fov_y_radian_;;
			const float aspect_ratio = aspect_ratio_;
			const float near_z = 0.1f;
			const float far_z = 10000.0f;
#if 1
			// Infinite Far Reverse Perspective
			math::Mat44 proj_mat = math::CalcReverseInfiniteFarPerspectiveMatrix(fov_y, aspect_ratio, 0.1f);
#elif 0
			// Reverse Perspective
			math::Mat44 proj_mat = math::CalcReversePerspectiveMatrix(fov_y, aspect_ratio, 0.1f, far_z);
#else
			// 標準Perspective
			math::Mat44 proj_mat = math::CalcStandardPerspectiveMatrix(fov_y, aspect_ratio, 0.1f, far_z);
#endif
			math::Vec4 ndc_z_to_view_z_coef = math::CalcViewDepthReconstructCoefFromProjectionMatrix(proj_mat);

			// 定数バッファ更新.
			{
				const auto cb_index = frame_count_ % std::size(cbh_scene_view);
				if (auto* mapped = static_cast<CbSceneView*>(cbh_scene_view[cb_index]->buffer_.Map()))
				{
					mapped->cb_view_mtx = view_mat;
					mapped->cb_proj_mtx = proj_mat;
					mapped->cb_view_inv_mtx = math::Mat34::Inverse(view_mat);
					mapped->cb_proj_inv_mtx = math::Mat44::Inverse(proj_mat);

					mapped->cb_ndc_z_to_view_z_coef = ndc_z_to_view_z_coef;

					cbh_scene_view[cb_index]->buffer_.Unmap();
				}
			}
		}

		RtTlas* RtSceneManager::GetSceneTlas()
		{
			if(!is_initialized_)
				return nullptr;
			return dynamic_tlas_.get();
		}
		const RtTlas* RtSceneManager::GetSceneTlas() const
		{
			if(!is_initialized_)
				return nullptr;
			return dynamic_tlas_.get();
		}
		
		int RtSceneManager::NumHitGroupCountMax() const
		{
			return hitgroup_count_max_;
		}
		
		rhi::ConstantBufferViewDep* RtSceneManager::GetSceneViewCbv()
		{
			if(!is_initialized_)
				return nullptr;
			
			const auto cb_index = frame_count_ % std::size(cbh_scene_view);
			return (cbh_scene_view[cb_index]->buffer_.IsValid())? &cbh_scene_view[cb_index]->cbv_ : nullptr;
		}
		const rhi::ConstantBufferViewDep* RtSceneManager::GetSceneViewCbv() const
		{
			if(!is_initialized_)
				return nullptr;
			
			const auto cb_index = frame_count_ % std::size(cbh_scene_view);
			return (cbh_scene_view[cb_index]->buffer_.IsValid())? &cbh_scene_view[cb_index]->cbv_ : nullptr;
		}

		void RtSceneManager::DispatchRay(rhi::GraphicsCommandListDep* p_command_list, const DispatchRayParam& param)
		{
			if(!is_initialized_)
				return;
			
			const auto cb_index = frame_count_ % std::size(cbh_scene_view);

			rhi::DeviceDep* p_device = p_command_list->GetDevice();
			auto* d3d_device = p_device->GetD3D12Device();
			auto* d3d_command_list = p_command_list->GetD3D12GraphicsCommandListForDxr();


			auto* p_target_tlas = dynamic_tlas_.get();
			auto shader_table_head = param.p_shader_table->shader_table_->GetD3D12Resource()->GetGPUVirtualAddress();


			// Bind the root signature
			d3d_command_list->SetComputeRootSignature(param.p_state_object->GetGlobalRootSignature());
			// State.
			d3d_command_list->SetPipelineState1(param.p_state_object->GetStateObject());

			// Global Resource設定.
			{
				// CBV,SRV,UAVの3種それぞれに固定数分でframe descriptor heap確保.
				const int num_frame_descriptor_cbvsrvuav_count = k_rt_global_descriptor_cbvsrvuav_table_size * 3;
				const int num_frame_descriptor_sampler_count = k_rt_global_descriptor_sampler_table_size;

				const auto resource_descriptor_step_size = p_command_list->GetFrameDescriptorInterface()->GetManager()->GetHandleIncrementSize();
				const auto sampler_descriptor_step_size = p_command_list->GetFrameSamplerDescriptorHeapInterface()->GetHandleIncrementSize();
				auto get_descriptor_with_pos = [](const DescriptorHandleSet& base, int offset_index, u32 handle_step_size) -> DescriptorHandleSet
				{
					const auto offset_addr = handle_step_size * offset_index;
					DescriptorHandleSet ret(base);
					ret.h_cpu.ptr += offset_addr;
					ret.h_gpu.ptr += offset_addr;
					return ret;
				};

				DescriptorHandleSet res_heap_head;
				DescriptorHandleSet sampler_heap_head;
				// CbvSrvUavのFrame Heap確保.
				if (!p_command_list->GetFrameDescriptorInterface()->Allocate(num_frame_descriptor_cbvsrvuav_count, res_heap_head.h_cpu, res_heap_head.h_gpu))
				{
					assert(false);
				}
				// SamplerのFrame Heap確保. ここの確保でHeapのページが足りない場合は別のHeapが確保されて切り替わる.
				// そのためSetDescriptorHeaps用のSampler用Heapを取得する場合は確保のあとにGetD3D12DescriptorHeapをすること.
				if (!p_command_list->GetFrameSamplerDescriptorHeapInterface()->Allocate(num_frame_descriptor_sampler_count, sampler_heap_head.h_cpu, sampler_heap_head.h_gpu))
				{
					assert(false);
				}

				// frame heap 上のそれぞれの配置.
				DescriptorHandleSet descriptor_table_base_cbv = get_descriptor_with_pos(res_heap_head, k_rt_global_descriptor_cbvsrvuav_table_size * 0, resource_descriptor_step_size);
				DescriptorHandleSet descriptor_table_base_srv = get_descriptor_with_pos(res_heap_head, k_rt_global_descriptor_cbvsrvuav_table_size * 1, resource_descriptor_step_size);
				DescriptorHandleSet descriptor_table_base_uav = get_descriptor_with_pos(res_heap_head, k_rt_global_descriptor_cbvsrvuav_table_size * 2, resource_descriptor_step_size);
				DescriptorHandleSet descriptor_table_base_sampler = get_descriptor_with_pos(sampler_heap_head, k_rt_global_descriptor_sampler_table_size * 0, sampler_descriptor_step_size);
				{
					// レンダリング用にFrameHeapにコピーする.

					// Global Cbv.
					for (auto si = 0; si < param.cbv_slot.size(); ++si)
					{
						if (param.cbv_slot[si])
						{
							DescriptorHandleSet table_handle = get_descriptor_with_pos(descriptor_table_base_cbv, si, resource_descriptor_step_size);
							d3d_device->CopyDescriptorsSimple(1, table_handle.h_cpu, param.cbv_slot[si]->GetView().cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						}
					}
					// Global Srv.
					for (auto si = 0; si < param.srv_slot.size(); ++si)
					{
						if (param.srv_slot[si])
						{
							DescriptorHandleSet table_handle = get_descriptor_with_pos(descriptor_table_base_srv, si, resource_descriptor_step_size);
							d3d_device->CopyDescriptorsSimple(1, table_handle.h_cpu, param.srv_slot[si]->GetView().cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						}
					}
					// Global Uav.
					for (auto si = 0; si < param.uav_slot.size(); ++si)
					{
						if (param.uav_slot[si])
						{
							DescriptorHandleSet table_handle = get_descriptor_with_pos(descriptor_table_base_uav, si, resource_descriptor_step_size);
							d3d_device->CopyDescriptorsSimple(1, table_handle.h_cpu, param.uav_slot[si]->GetView().cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
						}
					}
					// Global Sampler.
					for (auto si = 0; si < param.sampler_slot.size(); ++si)
					{
						if (param.sampler_slot[si])
						{
							DescriptorHandleSet table_handle = get_descriptor_with_pos(descriptor_table_base_sampler, si, sampler_descriptor_step_size);
							d3d_device->CopyDescriptorsSimple(1, table_handle.h_cpu, param.sampler_slot[si]->GetView().cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
						}
					}
				}

				// Heap設定.
				// desc_alloc_interface_ はHeapとしてはCommandListと同じ巨大なHeapから切り出して利用しているため同一Heapで良い.
				{
					std::vector<ID3D12DescriptorHeap*> use_heap_array = {};
					use_heap_array.push_back(p_command_list->GetFrameDescriptorInterface()->GetManager()->GetD3D12DescriptorHeap());
					use_heap_array.push_back(p_command_list->GetFrameSamplerDescriptorHeapInterface()->GetD3D12DescriptorHeap());

					// 使用しているHeapをセット.
					d3d_command_list->SetDescriptorHeaps((uint32_t)use_heap_array.size(), use_heap_array.data());
				}

				// Descriptor, Tableを設定.
				// ASはParam0番に直接設定. CBV, SRV, UAV, Samplerはその次からTableで設定.
				d3d_command_list->SetComputeRootShaderResourceView(0, p_target_tlas->GetBuffer()->GetD3D12Resource()->GetGPUVirtualAddress());
				d3d_command_list->SetComputeRootDescriptorTable(1, descriptor_table_base_cbv.h_gpu);
				d3d_command_list->SetComputeRootDescriptorTable(2, descriptor_table_base_srv.h_gpu);
				d3d_command_list->SetComputeRootDescriptorTable(3, sampler_heap_head.h_gpu);
				d3d_command_list->SetComputeRootDescriptorTable(4, descriptor_table_base_uav.h_gpu);
			}

			// Dispatch.
			D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
			raytraceDesc.Width = param.count_x;
			raytraceDesc.Height = param.count_y;
			raytraceDesc.Depth = 1;

			// RayGeneration Shaderのテーブル位置.
			raytraceDesc.RayGenerationShaderRecord.StartAddress = shader_table_head + param.p_shader_table->table_raygen_offset_;
			raytraceDesc.RayGenerationShaderRecord.SizeInBytes = param.p_shader_table->table_entry_byte_size_;

			// Miss Shaderのテーブル位置.
			raytraceDesc.MissShaderTable.StartAddress = shader_table_head + param.p_shader_table->table_miss_offset_;
			raytraceDesc.MissShaderTable.StrideInBytes = param.p_shader_table->table_entry_byte_size_;
			raytraceDesc.MissShaderTable.SizeInBytes = param.p_shader_table->table_entry_byte_size_ * param.p_shader_table->table_miss_count_;

			// HitGroup群の先頭のテーブル位置.
			// マテリアル毎のHitGroupはここから連続領域に格納. Instanceに設定されたHitGroupIndexでアクセスされる.
			raytraceDesc.HitGroupTable.StartAddress = shader_table_head + param.p_shader_table->table_hitgroup_offset_;
			raytraceDesc.HitGroupTable.StrideInBytes = param.p_shader_table->table_entry_byte_size_;
			raytraceDesc.HitGroupTable.SizeInBytes = param.p_shader_table->table_entry_byte_size_ * param.p_shader_table->table_hitgroup_count_;

			d3d_command_list->DispatchRays(&raytraceDesc);
		}

		void  RtSceneManager::SetCameraInfo(const math::Vec3& position, const math::Vec3& dir, const math::Vec3& up, float fov_y_radian, float aspect_ratio)
		{
			if(!is_initialized_)
				return;
			
			camera_pos_ = position;
			camera_dir_ = dir;
			camera_up_ = up;

			fov_y_radian_ = fov_y_radian;
			aspect_ratio_ = aspect_ratio;
		}

	}
}