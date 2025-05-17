
#include "gfx/resource/resource_mesh.h"

#include "resource/resource_manager.h"

namespace ngl
{
namespace gfx
{
	void ResMeshData::RenderThreadInitialize(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist)
	{
		auto* p_d3d_commandlist = p_commandlist->GetD3D12GraphicsCommandList();

		for (auto& e : this->data_.shape_array_)
		{
			e.index_.InitRender(p_device, p_commandlist);
			e.position_.InitRender(p_device, p_commandlist);

			e.normal_.InitRender(p_device, p_commandlist);
			e.tangent_.InitRender(p_device, p_commandlist);
			e.binormal_.InitRender(p_device, p_commandlist);

			for (auto& ve : e.color_)
				ve.InitRender(p_device, p_commandlist);

			for (auto& ve : e.texcoord_)
				ve.InitRender(p_device, p_commandlist);
		}
	}
}
}