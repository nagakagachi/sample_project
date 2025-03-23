
#include "platform/win/window.win.h"

#include "imgui/imgui_interface.h"


namespace ngl
{
	namespace platform
	{
		// 生成部を実装
		bool CoreWindow::CreateImplement()
		{
			return CreateImplementT<CoreWindowImplDep>();
		}


		CoreWindowImplDep::CoreWindowImplDep()
		{
		}
		CoreWindowImplDep::~CoreWindowImplDep()
		{
			Destroy();
		}

		// ウィンドウ生成
		bool CoreWindowImplDep::Initialize(const TCHAR* title, int w, int h)
		{
			// 初期化済みなら帰る
			if (IsValidWindow())
				return false;

			HINSTANCE hinstance = GetModuleHandle(nullptr);

			wcex_ = {};
			wcex_.cbSize		= sizeof(WNDCLASSEX);
			wcex_.style			= CS_HREDRAW | CS_VREDRAW;
			wcex_.lpfnWndProc	= CoreWindowImplDep::CallProc;
			wcex_.cbClsExtra	= 0;
			wcex_.cbWndExtra	= 0;
			wcex_.hInstance		= hinstance;
			wcex_.hIcon			= nullptr;
			wcex_.hCursor		= nullptr;
			wcex_.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
			wcex_.lpszMenuName	= nullptr;
			wcex_.lpszClassName = (_TCHAR*)title;
			wcex_.hIconSm		= nullptr;


			if (!RegisterClassEx(&wcex_)) {
				return false;
			}

			// 自身のポインタを指定してウィンドウ生成
			if (!(hwnd_ = CreateWindow(title, title, WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_SIZEBOX),
				CW_USEDEFAULT, 0, w, h, 0, 0, GetModuleHandle(NULL), (LPVOID)this))) {
				return false;
			}

			// 有効化
			is_valid_window_ = true;

			SetWindowSize(w, h);
			ShowWindow(hwnd_, SW_SHOW);
			return true;
		}
		void CoreWindowImplDep::Destroy()
		{
			if (IsValidWindow())
			{
				is_valid_window_ = false;
				// 破棄時にはセットしておいた自分自身をクリア
				SetPointer(hwnd_, NULL);
				DestroyWindow(hwnd_);
				UnregisterClassW(wcex_.lpszClassName, wcex_.hInstance);
			}

			wcex_ = {};
			hwnd_ = {};
		}

		// 有効か？
		bool CoreWindowImplDep::IsValid() const
		{
			return IsValidWindow();
		}

		bool CoreWindowImplDep::IsValidWindow() const
		{
			//return TRUE == IsWindowEnabled(hwnd_);
			return is_valid_window_;
		}

		void CoreWindowImplDep::GetScreenSize(unsigned int& w, unsigned int& h) const
		{
			w = screen_w_;
			h = screen_h_;
		}


		void CoreWindowImplDep::SetWindowSize(unsigned int w, unsigned int h)
		{
			screen_w_ = w;
			screen_h_ = h;
			unsigned int ww, wh;
			GetWindowSizeFromClientSize(screen_w_, screen_h_, ww, wh);
			::SetWindowPos(hwnd_, NULL, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
		}

		void CoreWindowImplDep::GetWindowSizeFromClientSize(unsigned int cw, unsigned int ch, unsigned int& ww, unsigned int& wh)
		{
			RECT rw, rc;
			GetWindowRect(hwnd_, &rw);
			GetClientRect(hwnd_, &rc);

			ww = (rw.right - rw.left) - (rc.right - rc.left) + cw;
			wh = (rw.bottom - rw.top) - (rc.bottom - rc.top) + ch;
		}


		// ウィンドウに実体を登録
		void CoreWindowImplDep::SetPointer(HWND hWnd, CoreWindowImplDep* ptr)
		{
#ifdef _WIN64
			//64bit
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ptr);
#else
			// 32bit
			SetWindowLong(hWnd, GWL_USERDATA, (LONG)ptr);
#endif
		}

		//===== ウィンドウプロシージャの呼び出し =====//
		LRESULT CALLBACK CoreWindowImplDep::CallProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			// プロパティリストからthisポインタを取得
#ifdef _WIN64
			//64bit
			CoreWindowImplDep* thisPtr = (CoreWindowImplDep*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
#else
			// 32bit
			CoreWindowImplDep* thisPtr = (CoreWindowImplDep*)GetWindowLong(hWnd, GWL_USERDATA);
#endif
			// まだポインタが設定されてない場合
			if (!thisPtr)
			{
				// 生成時の場合は生成時パラメータから取得
				if (message == WM_CREATE)
					thisPtr = (CoreWindowImplDep*)((LPCREATESTRUCT)lParam)->lpCreateParams;

				//　実体にウィンドウハンドルをセット
				if (thisPtr)
				{
					//＿プロパティリストにオブジェクトハンドル(thisポインタ)を設定する
					thisPtr->SetPointer(hWnd, thisPtr);
				}
			}
			// 個別のWindowプロシージャのコール
			if (thisPtr)
			{
				LRESULT lResult = thisPtr->MainProc(hWnd, message, wParam, lParam);
				return lResult;
			}
			// 実体がなければデフォルト
			return DefWindowProc(hWnd, message, wParam, lParam);
		}


		//===== ウィンドウプロシージャの実装(継承可能) =====//
		// ここでの記述はデフォルトの処理
		LRESULT CoreWindowImplDep::MainProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
		{	
			switch (message)
			{
				// ウィンドウが破棄された場合
			case WM_DESTROY:
				{
					PostQuitMessage(0);

					// フラグのリセットなどのために破棄処理呼び出し.
					Destroy();
					break;
				}
				
			default:
				{
					break;
				}
			}

			InputProc(message, wParam, lParam);
			
			// Imgui対応.
			{
				if (imgui::ImguiInterface::Instance().WindProc(hWnd, message, wParam, lParam))
				{
					return true;
				}
			}

			return DefWindowProc(hWnd, message, wParam, lParam);
		}


		void CoreWindowImplDep::SetMousePositionClipInWindow(bool clip_enable)
		{
			if (clip_enable)
			{
				// GetWindowRect等でクライアント領域のスクリーン座標が取れないのでClientToScreenで取得. DPI対応とかがこわいかも.

				POINT lefttop;
				POINT rightbottom;
				lefttop.x = 0;
				lefttop.y = 0;
				rightbottom.x = screen_w_ - 1;
				rightbottom.y = screen_h_ - 1;
				
				ClientToScreen(hwnd_, &lefttop);
				ClientToScreen(hwnd_, &rightbottom);

				RECT win_rect = {};
				win_rect.left = lefttop.x;
				win_rect.top = lefttop.y;
				win_rect.right = rightbottom.x;
				win_rect.bottom = rightbottom.y;

				ClipCursor(&win_rect);
			}
			else
			{
				ClipCursor(nullptr);
			}
		}
		void CoreWindowImplDep::InputProc(UINT message, WPARAM wParam, LPARAM lParam)
		{	
			if (message == WM_KEYDOWN || message == WM_KEYUP)
			{
				// keyboard virtual key state. VK_LEFT, VK_0, VK_A, VK_Z, VK_OEM_PLUS.
				int keyboard_key = -1;
				bool keyboard_down = (message == WM_KEYDOWN);
				if (0 < wParam && 0xff > wParam)
				{
					keyboard_key = (int)wParam;
					virtual_key_state_[keyboard_key] = (keyboard_down) ? 1 : 0;
				}
			}
			else if (message == WM_MOUSEMOVE)
			{
				// position.
				auto mx = LOWORD(lParam);
				auto my = HIWORD(lParam);

				bool is_first = is_first_mouse_pos_sample_;
				if (is_first_mouse_pos_sample_)
				{
					is_first_mouse_pos_sample_ = false;
					mouse_pos_delta_x_ = 0;
					mouse_pos_delta_y_ = 0;
				}
				else
				{
					mouse_pos_delta_x_ = mx - mouse_pos_x_;
					mouse_pos_delta_y_ = my - mouse_pos_y_;
				}

				if (req_fix_mouse_pos_)
				{
					// マウス位置固定リクエスト処理. Deltaについては適切な値が取れるようにしている.
					mx = req_mouse_pos_x_;
					my = req_mouse_pos_y_;

					POINT lefttop;
					lefttop.x = 0;
					lefttop.y = 0;
					ClientToScreen(hwnd_, &lefttop);

					SetCursorPos(lefttop.x + mx, lefttop.y + my);
				}

				mouse_pos_x_ = mx;
				mouse_pos_y_ = my;
			}
			else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
			{
				// mouse left button.
				mouse_left_state_ = (message == WM_LBUTTONDOWN);
			}
			else if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
			{
				// mouse right button.
				mouse_right_state_ = (message == WM_RBUTTONDOWN);
			}
			else if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP)
			{
				// mouse middl button.
				mouse_middle_state_ = (message == WM_MBUTTONDOWN);
			}

		}

	}
}