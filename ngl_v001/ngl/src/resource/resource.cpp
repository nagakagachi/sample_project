

#include "resource/resource.h"

namespace ngl
{
namespace res 
{
	namespace detail
	{

		ResourcePtrHolder::ResourcePtrHolder(Resource* p, IResourceDeleter* deleter)
			: p_res_(p), p_deleter_(deleter)
		{
		}
		ResourcePtrHolder::~ResourcePtrHolder()
		{
			if (p_res_)
			{
				if (p_deleter_)
				{
					// deleterが指定されている場合は呼び出し.
					(*p_deleter_)(p_res_);
				}
				else
				{
					// 指定なしの場合はデフォルトdelete.
					delete p_res_;
				}
			}
			p_res_ = nullptr;
			p_deleter_ = nullptr;
		}
	}

	// 破棄オペレータ.
	void ResourceDeleterDefault::operator()(Resource* p_res)
	{
		// デフォルトdeleterはデフォルトdeleteするだけ.
		delete p_res;
	}




	Resource::Resource()
	{
	}
	Resource::~Resource()
	{
	}
	void Resource::SetResourceInfo(const char* filename)
	{
		filename_ = filename;
	}

}
}

