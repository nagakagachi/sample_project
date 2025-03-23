/*
	ウィンドウクラス環境非依存部

	// 実装部を内包し、実装部の派生クラスで環境依存実装をする
*/

#pragma once

#ifndef _NGL_GRAPHICS_WINDOW_
#define _NGL_GRAPHICS_WINDOW_
#include <tchar.h>

#if 1
namespace ngl
{
	namespace platform
	{
		class CoreWindowImplDep;

		class CoreWindowImpl
		{
		public:
			CoreWindowImpl() {};
			virtual ~CoreWindowImpl() {};
			virtual bool Initialize(const TCHAR* title, int w, int h) = 0;
			virtual void Destroy() = 0;
			virtual bool IsValid() const = 0;


			virtual void GetScreenSize( unsigned int& w, unsigned int& h ) const = 0;

		protected:
		};

		class CoreWindow
		{
		public:
			CoreWindow()
			{
			}
			virtual ~CoreWindow()
			{
				Destroy();
			}
			// 初期化
			bool Initialize(const TCHAR* title, int w, int h)
			{
				// 生成済みなら失敗
				if (nullptr != window_impl_)
					return false;
				// 実装部生成
				CreateImplement();
				// 実装部初期化
				window_impl_->Initialize(title, w, h);
				return true;
			}
			// 破棄
			void Destroy()
			{
				if (nullptr != window_impl_)
				{
					// 破棄
					window_impl_->Destroy();
					delete window_impl_;
					window_impl_ = nullptr;
				}
			}
			// 有効チェック
			bool IsValid()
			{
				if (nullptr != window_impl_ && window_impl_->IsValid())
				{
					return true;
				}
				return false;
			}

			// 実装部取得
			CoreWindowImpl* Impl()
			{
				return window_impl_;
			}
			// 実装依存部取得
			CoreWindowImplDep& Dep()
			{
				return *(CoreWindowImplDep*)(window_impl_);
			}

		protected:
			// 実装部生成
			//	環境毎の実装コード側で実装する.
			bool CreateImplement();

			// 実装部生成ヘルパー
			template<typename T>
			bool CreateImplementT()
			{
				window_impl_ = new T();
				return true;
			}
			// 実装部
			CoreWindowImpl* window_impl_ = nullptr;
		};
	}
}
#endif

#endif // _NGL_GRAPHICS_WINDOW_