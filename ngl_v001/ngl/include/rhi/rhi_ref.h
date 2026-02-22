#pragma once

#include "rhi/rhi.h"

#include "thread/lockfree_stack_intrusive.h"

namespace ngl
{
	namespace rhi
	{
		class IRhiObject;

		class IDevice
		{
		public:
			IDevice() {}
			virtual ~IDevice() {}

			// 派生Deviceクラスで実装.
			virtual void DestroyRhiObject(IRhiObject* p) = 0;
		};



		// BufferやTexture等はこのクラスを継承することで, RhiRefによる参照管理と破棄時の安全な遅延破棄がサポートされる.
		using RhiObjectGabageCollectStack = ngl::thread::LockFreeStackIntrusive<class IRhiObject>;
		class IRhiObject : public RhiObjectGabageCollectStack::Node
		{
		public:
			static constexpr bool k_is_RhiObjectBase = true;

			IRhiObject() {}
			virtual ~IRhiObject() {}


			// 派生クラスで親Deviceを返す実装.
			virtual IDevice* GetParentDeviceInreface() = 0;
			virtual const IDevice* GetParentDeviceInreface() const = 0;

		};

		namespace detail
		{
			class RhiObjectHolder
			{
			public:
				RhiObjectHolder();
				RhiObjectHolder(IRhiObject* p);

				// IRhiObject実体の破棄.
				~RhiObjectHolder();


				IRhiObject* p_obj_ = nullptr;
			};

			using RhiObjectHolderHandle = std::shared_ptr<const RhiObjectHolder>;
		}

		// IRhiObject継承クラスオブジェクトの安全な遅延破棄を提供するSharedPtr.
		template<typename RHI_CLASS>
		class RhiRef
		{
		public:
			// IRhiObject継承チェック.
			static_assert(RHI_CLASS::k_is_RhiObjectBase, "RhiRefで保持するクラスはIRhiObjectを継承する必要があります");

			RhiRef()
			{}
			RhiRef(RhiRef& ref)
			{
				raw_handle_ = ref.raw_handle_;
			}
			RhiRef(const RhiRef& ref)
			{
				raw_handle_ = ref.raw_handle_;
			}
			RhiRef(detail::RhiObjectHolderHandle& h)
			{
				raw_handle_ = h;
			}
            // カウンタの誤制御を防ぐためexplicitにしたい.
			explicit RhiRef(RHI_CLASS* p)
			{
				Reset(p);
			}
			~RhiRef() {}


			void Reset(RHI_CLASS* p = nullptr)
			{
				if (p)
				{
					// 新規リソース実体のハンドルとなるため, 取り扱い注意.
					raw_handle_.reset(new detail::RhiObjectHolder(p));
				}
				else
				{
					raw_handle_.reset();
				}
			}

			bool IsValid() const
			{
				return nullptr != raw_handle_.get() && nullptr != raw_handle_.get()->p_obj_;
			}

			RHI_CLASS* Get()
			{
				return static_cast<RHI_CLASS*>(raw_handle_.get()->p_obj_);
			}
			const RHI_CLASS* Get() const
			{
				return static_cast<const RHI_CLASS*>(raw_handle_.get()->p_obj_);
			}
			RHI_CLASS* operator->()
			{
				return static_cast<RHI_CLASS*>(raw_handle_.get()->p_obj_);
			}
			const RHI_CLASS* operator->() const
			{
				return static_cast<const RHI_CLASS*>(raw_handle_.get()->p_obj_);
			}
			RHI_CLASS& operator*()
			{
				return *static_cast<RHI_CLASS*>(raw_handle_.get()->p_obj_);
			}
			const RHI_CLASS& operator*() const
			{
				return *static_cast<const RHI_CLASS*>(raw_handle_.get()->p_obj_);
			}


		private:
			// Resource基底ポインタを保持するオブジェクトの参照カウンタ.
			//	これにより実体をハンドルで共有しつつ, ハンドルの参照が無くなった際の保持オブジェクトのデストラクタで実体の破棄をカスタマイズする.
			detail::RhiObjectHolderHandle raw_handle_;

		};

	}
}