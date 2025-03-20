#pragma once

#include "ngl/util/noncopyable.h"
#include "ngl/math/math.h"
#include "ngl/text/hash_text.h"


namespace ngl
{
namespace gfx
{

	// Mesh Instance Buffer.
	struct InstanceInfo
	{
		ngl::math::Mat34 mtx;
		ngl::math::Mat34 mtx_cofactor;// 法線変換用余因子行列. Align用にvec4*3としている. https://github.com/graphitemaster/normals_revisited .
	};

	struct CbSceneView
	{
		ngl::math::Mat34 cb_view_mtx;
		ngl::math::Mat34 cb_view_inv_mtx;
		ngl::math::Mat44 cb_proj_mtx;
		ngl::math::Mat44 cb_proj_inv_mtx;

		// 正規化デバイス座標(NDC)のZ値からView空間Z値を計算するための係数. PerspectiveProjectionMatrixの方式によってCPU側で計算される値を変えることでシェーダ側は同一コード化.
		//	view_z = cb_ndc_z_to_view_z_coef.x / ( ndc_z * cb_ndc_z_to_view_z_coef.y + cb_ndc_z_to_view_z_coef.z )
		//
		//		cb_ndc_z_to_view_z_coef = 
		//			Standard RH: (-far_z * near_z, near_z - far_z, far_z, 0.0)
		//			Standard LH: ( far_z * near_z, near_z - far_z, far_z, 0.0)
		//			Reverse RH: (-far_z * near_z, far_z - near_z, near_z, 0.0)
		//			Reverse LH: ( far_z * near_z, far_z - near_z, near_z, 0.0)
		//			Infinite Far Reverse RH: (-near_z, 1.0, 0.0, 0.0)
		//			Infinite Far Reverse RH: ( near_z, 1.0, 0.0, 0.0)
		ngl::math::Vec4	cb_ndc_z_to_view_z_coef;
		
		float			cb_time_sec;
	};


	// セマンティクス名.
	using SemanticNameType = text::HashText<32>;
	
	static constexpr int k_mesh_vertex_semantic_texcoord_max_count = 4;
	static constexpr int k_mesh_vertex_semantic_color_max_count = 4;
	// Meshでサポートしているセマンティクス種別.
	struct EMeshVertexSemanticKind
	{
		enum Type : int
		{
			POSITION,
			NORMAL,
			TANGENT,
			BINORMAL,
			COLOR,
			TEXCOORD,

			_MAX,
		};
		static constexpr SemanticNameType Name[] =
		{
			"POSITION",
			"NORMAL",
			"TANGENT",
			"BINORMAL",
			"COLOR",
			"TEXCOORD",
		};
		// 各Semanticの許可される個数.
		static constexpr int Count[] =
		{
			1,
			1,
			1,
			1,
			k_mesh_vertex_semantic_color_max_count,
			k_mesh_vertex_semantic_texcoord_max_count,
		};
	};

	// セマンティクス種別毎の数とオフセット等.
	struct MeshVertexSemanticSlotOffset
	{
		int offset[EMeshVertexSemanticKind::_MAX];
		int max_slot_count;

		constexpr MeshVertexSemanticSlotOffset()
			: offset()
			, max_slot_count()
		{
			// スロットオフセット計算.
			// メッシュ描画時の各セマンティクスに対応するスロットは固定(TEXCOORD1 -> 5).
			offset[0] = 0;
			for (auto i = 1; i < std::size(offset); ++i)
			{
				offset[i] = EMeshVertexSemanticKind::Count[i - 1] + offset[i - 1];
			}
			max_slot_count = offset[EMeshVertexSemanticKind::_MAX-1] + EMeshVertexSemanticKind::Count[EMeshVertexSemanticKind::_MAX - 1];
		}
	};
	// Slot毎の情報.
	struct MeshVertexSemanticSlotInfo
	{
		static constexpr MeshVertexSemanticSlotOffset k_semantic_slot_offset{};
		// MeshVertexSemanticSlotMaskで保持するため32以下チェック.
		static_assert(k_semantic_slot_offset.max_slot_count <= 32);
		
		EMeshVertexSemanticKind::Type slot_semantic_type[k_semantic_slot_offset.max_slot_count];// slotに対応するSemanticのType. 例 slot[N] -> Normal[M] -> Normal .
		int slot_semantic_index[k_semantic_slot_offset.max_slot_count];// slotに対応するSemanticのIndex. 例 slot[N] -> [M].

		
		constexpr MeshVertexSemanticSlotInfo()
			: slot_semantic_type()
			, slot_semantic_index()
		{
			for(int s = 0; s < EMeshVertexSemanticKind::_MAX; ++s)
			{
				for(int local_offset = 0; local_offset < EMeshVertexSemanticKind::Count[s]; ++local_offset)
				{
					slot_semantic_type[k_semantic_slot_offset.offset[s] + local_offset] = (EMeshVertexSemanticKind::Type)s;
					slot_semantic_index[k_semantic_slot_offset.offset[s] + local_offset] = local_offset;
				}
			}
		}
	};

	// セマンティクス種別とインデックスからマッピングされたSlotインデックスの計算等を提供.
	struct MeshVertexSemantic
	{
		static constexpr MeshVertexSemanticSlotInfo k_semantic_slot_info{};
		
		// サポートしている最大スロット数.
		static constexpr int SemanticSlotMaxCount()
		{
			return MeshVertexSemanticSlotInfo::k_semantic_slot_offset.max_slot_count;
		}

		// セマンティクスとそのインデックスからスロット番号を返す.
		//	TEXCOORD, 1 -> 5
		static constexpr int SemanticSlot(EMeshVertexSemanticKind::Type semantic, int semantic_index = 0)
		{
			assert(EMeshVertexSemanticKind::Count[semantic] > semantic_index);
			return MeshVertexSemanticSlotInfo::k_semantic_slot_offset.offset[semantic] + semantic_index;
		}
		// 各セマンティクスの最大スロット数を返す.
		static constexpr int SemanticCount(EMeshVertexSemanticKind::Type semantic)
		{
			return EMeshVertexSemanticKind::Count[semantic];
		}

		// セマンティクス名.
		static const SemanticNameType& SemanticName(EMeshVertexSemanticKind::Type semantic)
		{
			return EMeshVertexSemanticKind::Name[semantic];
		}
		// セマンティクス名.
		static const char* SemanticNameStr(EMeshVertexSemanticKind::Type semantic)
		{
			return SemanticName(semantic).Get();
		}
		
		// セマンティクス名->Type.
		static const EMeshVertexSemanticKind::Type ConvertSemanticNameToType(const SemanticNameType& name)
		{
			for(int i = 0; i < EMeshVertexSemanticKind::_MAX; ++i)
			{
				if(name == EMeshVertexSemanticKind::Name[i])
					return (EMeshVertexSemanticKind::Type)i;
			}
			return EMeshVertexSemanticKind::Type::_MAX;
		}
	};

	// Meshの保持頂点Attrを表現するBitMask. max 32.
	//	Meshが保持する頂点Attrは固定のSemanticと個数で管理されるため, フラット化したIndexでbitmask化している.
	//		POSITION -> bit 0
	//		COLOR1 -> bit 5
	struct MeshVertexSemanticSlotMask
	{
		void Clear()
		{
			mask = 0;
		}
		void AddSlot(EMeshVertexSemanticKind::Type semantic, int semantic_index = 0)
		{
			mask |= (1 << MeshVertexSemantic::SemanticSlot(semantic, semantic_index));
		}
		void RemoveSlot(EMeshVertexSemanticKind::Type semantic, int semantic_index = 0)
		{
			mask &= ~(1 << MeshVertexSemantic::SemanticSlot(semantic, semantic_index));
		}

		const uint32_t& operator()()
		{
			return mask;
		}
		uint32_t mask = 0;
	};

}
}
