
#include "rhi/rhi_ref.h"


// とりあえずD3D12用
#include "rhi/d3d12/device.d3d12.h"


namespace ngl
{
namespace rhi
{
	namespace detail
	{
		RhiObjectHolder::RhiObjectHolder()
		{
		}
		RhiObjectHolder::RhiObjectHolder(IRhiObject* p)
		{
			p_obj_ = p;
		}
		RhiObjectHolder::~RhiObjectHolder()
		{
#if 1
			// 安全なタイミングでのRHIオブジェクト破棄対応.
			if (nullptr == p_obj_)
				return;

			// Deviceの持つFrameDestroyListに積み込む等.
			if (p_obj_->GetParentDeviceInreface())
			{
				// 実際の破棄処理をDeviceに依頼.
				p_obj_->GetParentDeviceInreface()->DestroyRhiObject(p_obj_);
				p_obj_ = nullptr;
			}
			else
			{
				// そうでなければ即時破棄.
				delete p_obj_;
			}
#else
			if (p_res_)
			{
				assert(false); // 本来はRhiオブジェクト破棄を安全なタイミングで遅延実行する.
				delete p_res_;
			}
#endif
		}
	}
}
}