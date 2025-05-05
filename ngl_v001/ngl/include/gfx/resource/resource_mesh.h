#pragma once

#include <vector>

#include "math/math.h"
#include "util/noncopyable.h"
#include "util/singleton.h"


#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/resource.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"

#include "resource/resource.h"

// Mesh用セマンティクスマッピング等.
#include "gfx/common_struct.h"

namespace ngl
{
	namespace res
	{
		class IResourceRenderUpdater;
	}


	namespace gfx
	{
		struct VertexColor
		{
			uint32_t r : 8;
			uint32_t g : 8;
			uint32_t b : 8;
			uint32_t a : 8;
		};


		class MeshShapeGeomBufferBase
		{
		public:
			MeshShapeGeomBufferBase() {}
			virtual ~MeshShapeGeomBufferBase()
			{
			}
			void InitRender(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist)
			{
				// バッファ自体が生成されていなければ終了.
				if (!ref_upload_rhibuffer_.IsValid() || !rhi_buffer_.GetD3D12Resource())
					return;

				auto* p_d3d_commandlist = p_commandlist->GetD3D12GraphicsCommandList();

				// Init Upload Buffer から DefaultHeapのBufferへコピー. StateはGeneral想定.
				const rhi::EResourceState buffer_state = rhi_init_state_;// 生成時のロジックで指定したステート. Bufferの初期ステートはDefaultHeapの場合?はCommonでないとValidationErrorとされるようになったので注意.

				p_commandlist->ResourceBarrier(&rhi_buffer_, buffer_state, rhi::EResourceState::CopyDst);
				p_d3d_commandlist->CopyResource(rhi_buffer_.GetD3D12Resource(), ref_upload_rhibuffer_->GetD3D12Resource());
				p_commandlist->ResourceBarrier(&rhi_buffer_, rhi::EResourceState::CopyDst, buffer_state);

				// upload bufferを解放.
				ref_upload_rhibuffer_.Reset();
			}

			bool IsValid() const { return rhi_buffer_.IsValid(); }

			// rhi buffer for gpu.
			rhi::BufferDep	rhi_buffer_ = {};
			rhi::EResourceState	rhi_init_state_ = rhi::EResourceState::Common;
			// rhi srv.
			rhi::ShaderResourceViewDep rhi_srv = {};

			// 初期データバッファ. InitRenderでrhi_buffer_へコピー命令を発行した後に破棄される.
			rhi::RhiRef<rhi::BufferDep>	ref_upload_rhibuffer_ = {};
			void* raw_ptr_ = nullptr;
		};

		class MeshShapeVertexDataBase : public MeshShapeGeomBufferBase
		{
		public:
			MeshShapeVertexDataBase() {}
			virtual ~MeshShapeVertexDataBase() {}

			// rhi vertex buffer view.
			rhi::VertexBufferViewDep rhi_vbv_ = {};
		};

		class MeshShapeIndexDataBase : public MeshShapeGeomBufferBase
		{
		public:
			MeshShapeIndexDataBase() {}
			virtual ~MeshShapeIndexDataBase() {}

			// rhi vertex buffer view.
			rhi::IndexBufferViewDep rhi_vbv_ = {};
		};

		template<typename T>
		class MeshShapeVertexData : public MeshShapeVertexDataBase
		{
		public:
			MeshShapeVertexData()
			{
			}
			~MeshShapeVertexData()
			{
			}
			// raw data ptr.
			T* GetTypedRawDataPtr() { return static_cast<T*>(raw_ptr_); }
			const T* GetTypedRawDataPtr() const { return static_cast<T*>(raw_ptr_); }

			// rhi vertex buffer view.
			//rhi::VertexBufferViewDep rhi_vbv_ = {};
		};

		template<typename T>
		class MeshShapeIndexData : public MeshShapeIndexDataBase
		{
		public:
			MeshShapeIndexData()
			{
			}
			~MeshShapeIndexData()
			{
			}
			// raw data ptr.
			T* GetTypedRawDataPtr() { return static_cast<T*>(raw_ptr_); }
			const T* GetTypedRawDataPtr() const { return static_cast<T*>(raw_ptr_); }

			// rhi index buffer view.
			//rhi::IndexBufferViewDep rhi_vbv_ = {};
		};

		// Mesh Shape Data.
		// 頂点属性の情報のメモリ実態は親のMeshDataが保持し, ロードや初期化でマッピングされる.
		class MeshShapePart
		{
		public:
			MeshShapePart()
			{
			}
			~MeshShapePart()
			{
			}

			int num_vertex_ = 0;
			int num_primitive_ = 0;// num primitive(triangle).

			MeshShapeIndexData<uint32_t> index_ = {};
			MeshShapeVertexData<math::Vec3> position_ = {};
			MeshShapeVertexData<math::Vec3> normal_ = {};
			MeshShapeVertexData<math::Vec3> tangent_ = {};
			MeshShapeVertexData<math::Vec3> binormal_ = {};
			std::vector<MeshShapeVertexData<VertexColor>>	color_;
			std::vector<MeshShapeVertexData<math::Vec2>>	texcoord_;


			// バインド時等に効率的に設定するためのポインタ配列.
			std::array<MeshShapeVertexDataBase*, MeshVertexSemantic::SemanticSlotMaxCount()> p_vtx_attr_mapping_ = {};
			MeshVertexSemanticSlotMask	vtx_attr_mask_ = {};

		};

		// Mesh Shape Data.
		class MeshData
		{
		public:
			MeshData()
			{
			}
			~MeshData()
			{
			}

			// ジオメトリ情報のRawDataメモリ. 個々でメモリ確保してマッピングする場合に利用.
			std::vector<uint8_t> raw_data_mem_;
			// RawData以外にもRHIBufferも一纏めにする場合はここで管理してshapeのviewが参照することもできるかもしれない.

			std::vector<MeshShapePart> shape_array_;
		};


		// Meshデータにマテリアル情報が含まれる場合の取り出し用.
		class SurfaceMaterialInfo
		{
		public:
			using TexturePath = text::HashText<256>;
			TexturePath tex_basecolor = {};
			TexturePath tex_normal = {};
			TexturePath tex_occlusion = {};
			TexturePath tex_roughness = {};
			TexturePath tex_metalness = {};
		};

		// Mesh Resource 実装.
		class ResMeshData : public res::Resource
		{
			NGL_RES_MEMBER_DECLARE(ResMeshData)

		public:
			struct LoadDesc
			{
				int dummy;
			};


			ResMeshData()
			{
			}
			~ResMeshData()
			{
			}


			bool IsNeedRenderUpdate() const override { return true; }
			void OnResourceRenderUpdate(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist) override;

			MeshData data_ = {};

			std::vector<SurfaceMaterialInfo> material_data_array_;
			std::vector<int> shape_material_index_array_;
		};
	}
}
