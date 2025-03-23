#pragma once
#ifndef _NGL_BOOT_APPLICATION_WIN_H_
#define _NGL_BOOT_APPLICATION_WIN_H_

#include "boot/boot_application.h"

namespace ngl
{
	namespace boot
	{
		class BootApplicationDep : public BootApplication
		{
		public:
			BootApplicationDep()
			{
			}
			~BootApplicationDep()
			{
			}
			void Run(ApplicationBase* app) override;
		};

	}
}


#endif // _NGL_BOOT_APPLICATION_WIN_H_