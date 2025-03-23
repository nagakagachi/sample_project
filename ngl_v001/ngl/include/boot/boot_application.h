#pragma once
#ifndef _NGL_BOOT_APPLICATION_H_
#define _NGL_BOOT_APPLICATION_H_

#include "util/noncopyable.h"

namespace ngl
{
	namespace boot
	{
		/*
			このクラスを継承して execute() で任意のゲームループを実装する
			BootApplication実体の run() に引数として渡すことでプラットフォーム毎のメインループでコールされる
			
			ngl::unique_ptr<ngl::boot::BootApplication> boot(ngl::boot::BootApplication::Create());
			AppGame app;
			boot->Run(&app);
		*/
		class ApplicationBase
		{
		public:
			virtual ~ApplicationBase() {}

			// アプリケーションの初期化
			virtual bool Initialize() = 0;

			// アプリケーションのフレーム処理の実行
			// false で終了
			virtual bool Execute() = 0;
		};


		// アプリケーションの実行を担当する
		// プラットフォーム別でメインループを実装し、run()でアプリケーションのフレーム処理をコールする
		class BootApplication : public NonCopyableTp<BootApplication>
		{
		public:
			// プラットフォーム依存派生クラス生成
			static class BootApplication* Create();
		public:
			BootApplication()
			{
			}

			virtual ~BootApplication()
			{
			}

			// BootApplicationのプラットフォーム依存派生クラスで実装
			// app->execute()が偽を返すまで無限ループを実行する
			// Windowsならこの中でメッセージループを回してメッセージが無いループで app->execute() を呼ぶ
			virtual void Run(ApplicationBase* app) = 0;

		protected:
		};
	}
}

#endif //_NGL_BOOT_APPLICATION_H_