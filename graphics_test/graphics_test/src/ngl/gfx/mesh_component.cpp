#include "mesh_component.h"

#include "common_struct.h"

namespace ngl
{
namespace gfx
{
	StaticMeshComponent::StaticMeshComponent()
	{
	}
	StaticMeshComponent::~StaticMeshComponent()
	{
	}

	void StaticMeshComponent::UpdateCbInstanceInfo(int cb_index)
	{
		if (auto* mapped = cb_instance_[cb_index]->MapAs<InstanceInfo>())
		{
			mapped->mtx = transform_;
			mapped->mtx_cofactor = math::Mat34(math::Mat33::Cofactor(transform_.GetMat33()));// 余因子行列.
				
			cb_instance_[cb_index]->Unmap();
		}
	}

	bool StaticMeshComponent::Initialize(rhi::DeviceDep* p_device, const res::ResourceHandle<ResMeshData>& res_mesh)
	{
		model_.Initialize(p_device,res_mesh);
		
		for (int i = 0; i < cb_instance_.size(); ++i)
		{
			cb_instance_[i] = new rhi::BufferDep();
			rhi::BufferDep::Desc desc = {};
			desc.SetupAsConstantBuffer(sizeof(InstanceInfo));
			cb_instance_[i]->Initialize(p_device, desc);


			cbv_instance_[i] = new rhi::ConstantBufferViewDep();
			rhi::ConstantBufferViewDep::Desc cbv_desc = {};
			cbv_instance_[i]->Initialize(cb_instance_[i].Get(), cbv_desc);

			UpdateCbInstanceInfo(i);
		}

		return true;
	}

	const ResMeshData* StaticMeshComponent::GetMeshData() const
	{
		return model_.res_mesh_.Get();
	}

	void StaticMeshComponent::UpdateRenderData()
	{
		// 変更チェック.
		if (transform_ == transform_prev_)
			return;

		// 内部更新
		transform_prev_ = transform_;

		// バッファFlipと更新.
		flip_index_ = (flip_index_ + 1) % 2;
		
		UpdateCbInstanceInfo(flip_index_);
	}
	rhi::RhiRef<rhi::ConstantBufferViewDep> StaticMeshComponent::GetInstanceBufferView() const
	{
		return cbv_instance_[flip_index_];
	}
}
}
