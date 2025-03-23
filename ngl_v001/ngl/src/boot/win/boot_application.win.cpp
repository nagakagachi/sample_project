

#define NOMINMAX
#include <Windows.h>

#include "boot/win/boot_application.win.h"

namespace ngl
{
	namespace boot
	{

		BootApplication* BootApplication::Create()
		{
			return new BootApplicationDep();
		}


		void BootApplicationDep::Run(ApplicationBase* app)
		{
			// 初期化
			if (!app->Initialize())
				return;

			// Windowsメッセージループ
			MSG msg;
			do {
				if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&msg);// for WM_CHAR.
					DispatchMessage(&msg);
				}
				else
				{
					// フレーム処理
					if (!app->Execute())
					{
						break;
					}
				}
			}
			while ( true );
		}
	}
}