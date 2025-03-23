/*
	ウィンドウクラス依存部実装
*/

#pragma once

#ifndef _NGL_GRAPHICS_WINDOW_WIN_
#define _NGL_GRAPHICS_WINDOW_WIN_


#define NOMINMAX
#include <Windows.h>
#undef min
#undef max

#include <iterator>
#include <tuple>

#include "platform/window.h"

#if 1
namespace ngl
{
	namespace platform
	{
		class CoreWindowImplDep : public CoreWindowImpl
		{
		public:

			CoreWindowImplDep();
			virtual ~CoreWindowImplDep();

			virtual bool Initialize(const TCHAR* title, int w, int h) override;
			virtual void Destroy() override;
			virtual bool IsValid() const override;

			virtual void GetScreenSize(unsigned int& w, unsigned int& h) const override;


		public:

			// クライアントサイズからウィンドウのサイズを計算
			void GetWindowSizeFromClientSize(unsigned int cw, unsigned int ch, unsigned int& ww, unsigned int& wh);

			// ウィンドウサイズ(実際はクライアントサイズ)を設定
			void SetWindowSize(unsigned int w, unsigned int h);

			// 有効なWindowか.
			bool IsValidWindow() const;

			HWND GetWindowHandle() const
			{
				return hwnd_;
			}

			// ウィンドウプロシージャの呼び出し
			static LRESULT CALLBACK CallProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
			// ウィンドウプロシージャの実装
			virtual LRESULT MainProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

		protected:
			// ポインタの設定
			void SetPointer(HWND hWnd, CoreWindowImplDep* ptr);


		public:
			// 入力系

			const unsigned char* GetVirtualKeyState() const
			{
				return virtual_key_state_;
			}
			int NumVirtualKeyState() const
			{
				return static_cast<int>(std::size(virtual_key_state_));
			}
			
			// Mouse Screen Position XY.
			std::tuple<int, int> GetMousePosition() const
			{
				return std::make_tuple(mouse_pos_x_, mouse_pos_y_);
			}
			// Mouse Screen Position Delta XY.
			// マウス位置固定に影響されず取得可能.
			std::tuple<int, int> GetMousePositionDelta() const
			{
				return std::make_tuple(mouse_pos_delta_x_, mouse_pos_delta_y_);
			}

			bool GetMouseLeft() const
			{
				return (0 != mouse_left_state_);
			}
			bool GetMouseRight() const
			{
				return (0 != mouse_right_state_);
			}
			bool GetMouseMiddle() const
			{
				return (0 != mouse_middle_state_);
			}

			// マウス位置固定リクエスト.
			// GetMousePosition の結果が固定されるためカーソル移動差分が必要な場合は GetMousePositionDelta を利用する.
			void SetMousePositionRequest(int mx, int my)
			{
				req_fix_mouse_pos_ = true;
				req_mouse_pos_x_ = mx;
				req_mouse_pos_y_ = my;
			}
			// マウス位置固定リクエスト解除.
			void ResetMousePositionRequest()
			{
				req_fix_mouse_pos_ = false;
			}
			//
			void SetMousePositionClipInWindow(bool clip_enable);


		private:
			void InputProc(UINT message, WPARAM wParam, LPARAM lParam);

		protected:
			WNDCLASSEX		wcex_ = {};
			HWND			hwnd_ = {};
			bool			is_valid_window_ = false;
			unsigned int	screen_w_ = {};
			unsigned int	screen_h_ = {};


			// ------------------------------------------------------
			// 入力系
			
			unsigned char	virtual_key_state_[0xff + 1] = {};

			bool			is_first_mouse_pos_sample_ = true;
			int				mouse_pos_x_ = {};
			int				mouse_pos_y_ = {};
			int				mouse_pos_delta_x_ = {};
			int				mouse_pos_delta_y_ = {};

			bool			req_fix_mouse_pos_ = false;
			int				req_mouse_pos_x_ = 0;
			int				req_mouse_pos_y_ = 0;
			bool			req_mouse_pos_clip_ = false;

			unsigned char	mouse_left_state_ = 0;
			unsigned char	mouse_right_state_ = 0;
			unsigned char	mouse_middle_state_ = 0;
			// ------------------------------------------------------
		};

	}
}
#endif

#endif // _NGL_GRAPHICS_WINDOW_WIN_