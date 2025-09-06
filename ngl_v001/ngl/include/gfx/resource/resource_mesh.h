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
			MeshShapeGeomBufferBase() 
            {
                rhi_buffer_.Reset(new rhi::BufferDep());
                rhi_srv.Reset(new rhi::ShaderResourceViewDep());
            }
			virtual ~MeshShapeGeomBufferBase()
			{
			}
            
			bool IsValid() const { return rhi_buffer_.IsValid(); }

			rhi::EResourceState	rhi_init_state_ = rhi::EResourceState::Common;
			// rhi buffer for gpu.
			rhi::RefBufferDep	rhi_buffer_ = {};
			// rhi srv.
			rhi::RefSrvDep rhi_srv = {};

            // CPUアクセス用元データ.
			void* raw_ptr_ = nullptr;
		};

		class MeshShapeVertexDataBase : public MeshShapeGeomBufferBase
		{
		public:
			MeshShapeVertexDataBase() 
            {
                rhi_vbv_.Reset(new rhi::VertexBufferViewDep());
            }
			virtual ~MeshShapeVertexDataBase() {}

			// rhi vertex buffer view.
			rhi::RefVbvDep rhi_vbv_ = {};
		};

		class MeshShapeIndexDataBase : public MeshShapeGeomBufferBase
		{
		public:
			MeshShapeIndexDataBase() 
            {
                rhi_vbv_.Reset(new rhi::IndexBufferViewDep());
            }
			virtual ~MeshShapeIndexDataBase() {}

			// rhi vertex buffer view.
			rhi::RefIbvDep rhi_vbv_ = {};
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
		};
        

        class MeshShapeInitializeSourceData
		{
		public:
			MeshShapeInitializeSourceData()
			{
			}
			~MeshShapeInitializeSourceData()
			{
			}

			int num_vertex_ = 0;
			int num_primitive_ = 0;// triangle.

			uint32_t* index_ = {};
			math::Vec3* position_ = {};
			math::Vec3* normal_ = {};
			math::Vec3* tangent_ = {};
			math::Vec3* binormal_ = {};
			std::vector<VertexColor*>	color_{};
			std::vector<math::Vec2*>	texcoord_{};
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

            void Initialize(rhi::DeviceDep* p_device, const MeshShapeInitializeSourceData& init_source_data);


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

            // 各Shape情報.
			std::vector<MeshShapePart> shape_array_;
		};

        // MeshShapeInitializeSourceDataからMeshDataを生成する. 内部に必要なメモリを別途確保する.
        // リソースではなくプログラムからメッシュ生成し, ResMeshのシェイプ部分のみオーバーライドすることが可能.
        void GenerateMeshDataProcedural(MeshData& out_mesh, rhi::DeviceDep* p_device, const MeshShapeInitializeSourceData& init_source_data);


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

            // メッシュのBuffer生成(CreateShapeDataRhiBuffer)でそれぞれのバッファが自身のRenderThread初期化タスクを発行するようになったためここは不要になった.
			//bool IsNeedRenderThreadInitialize() const override { return true; }
			//void RenderThreadInitialize(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist) override;

			MeshData data_ = {};
			std::vector<SurfaceMaterialInfo> material_data_array_;
			std::vector<int> shape_material_index_array_;
		};
	}
}
