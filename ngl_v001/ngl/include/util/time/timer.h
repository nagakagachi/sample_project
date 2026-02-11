#pragma once

#ifndef _NGL_UTIL_TIMER_
#define _NGL_UTIL_TIMER_

/*
	Windowsで動作するタイマー管理です

	名前をつけてタイマーをスタート（スタートメソッド実行毎に経過時間をリセットします）
	名前をつけたタイマーの経過時間取得
	名前をつけたタイマーを削除

	名前を指定しないとデフォルトタイマーを使います

	map をstd::stringからhashTextに変更したいかも

*/


#define NOMINMAX
#include <windows.h>

#include <string>
//#include <map>
#include <unordered_map>

#include "util/types.h"
#include "util/singleton.h"
#include "text/hash_text.h"

namespace ngl
{
	namespace time
	{
		//typedef ngl::text::HashText<64>	TimerName;
		using TimerName = ngl::text::HashText<64>;


		class Timer : public Singleton< Timer >
		{
		public:
			Timer();
			~Timer();

			// タイマー開始
			void	StartTimer(const char* str = "default");
			// 経過時間[秒]取得
			double	GetElapsedSec(const char* str = "default");
			// タイマー削除
			void	RemoveTimer(const char* str);
			// 経過時間をパフォーマンスカウンタのまま取得
			ngl::s64 GetPerformanceCounter(const char* str = "default");

			// タイマーを一時停止
			void	SuspendTimer(const char* str = "default");
			// タイマーを再開
			void	ResumeTimer(const char* str = "default");


			// 経過時間をパフォーマンスカウンタのまま取得
			ngl::s64 GetSuspendTotalPerformanceCounter(const char* str = "default");

		private:
			struct TimerEntity
			{
				struct TimerConstruct
				{
					TimerConstruct() {
						static LARGE_INTEGER freq = {};
						QueryPerformanceFrequency(&freq);
						freq_inv = 1.0 / (double)(freq.QuadPart);
					}
					double			freq_inv;
				};
				static double GetFreqInv() {
					static TimerConstruct c;
					return c.freq_inv;
				}
				TimerEntity()
				{
				}

				LARGE_INTEGER time_begin = {};
				LARGE_INTEGER time_end = {};
				// 一時停止開始の時間
				LARGE_INTEGER time_stop_start = {};
				// 一時停止の総時間 時間の取得時にこの時間を引いたものを返すように
				LARGE_INTEGER time_suspend_total = {};
			};

			//using TimerMapType = std::map<TimerName, Timer::TimerEntity>;
			using TimerMapType = std::unordered_map<TimerName, TimerEntity>;

			TimerMapType timers_ = {};
		};
	}
}

#endif // _NGL_UTIL_TIMER_