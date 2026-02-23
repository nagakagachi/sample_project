#pragma once

#include <memory>

#include "rhi/d3d12/device.d3d12.h"

#include "gfx/resource/resource_mesh.h"

namespace ngl
{
namespace assimp
{
	struct MaterialTextureSet
	{
		std::string tex_base_color = {};
		std::string tex_normal = {};
		std::string tex_occlusion = {};
		std::string tex_roughness = {};
		std::string tex_metalness = {};
	};

	//	out_mesh
	//	out_material_tex_set : Material情報.
	//	out_shape_material_index : out_meshの内部Shape毎のMaterialIndex (out_material_tex_setの要素に対応).
	//	out_shape_layouts : メッシュの形状レイアウト情報.
	bool LoadMeshData(gfx::MeshData& out_mesh, std::vector<MaterialTextureSet>& out_material_tex_set, std::vector<int>& out_shape_material_index, std::vector<gfx::MeshShapeLayout>& out_shape_layouts, rhi::DeviceDep* p_device, const char* filename);
}
}