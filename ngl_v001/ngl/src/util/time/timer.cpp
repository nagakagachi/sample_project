
#include "util/time/timer.h"


namespace ngl
{
namespace time
{
	Timer::Timer()
	{
		timers_.clear();
		timers_.insert(std::pair<TimerName, Timer::TimerEntity>("default", Timer::TimerEntity()));
	}

	Timer::~Timer()
	{
		timers_.clear();
	}

	void	Timer::StartTimer(const char* name)
	{
		TimerMapType::iterator it = timers_.find(name);
		if (timers_.end() == it)
		{
			timers_.insert(std::pair<TimerName, Timer::TimerEntity >(name, Timer::TimerEntity()));
			it = timers_.find(name);
		}
		QueryPerformanceCounter(&it->second.time_begin);
		
		it->second.time_stop_start.QuadPart = -1;
		memset(&it->second.time_suspend_total, 0x00, sizeof(LARGE_INTEGER));
	}
	double	Timer::GetElapsedSec(const char* name)
	{
		return GetPerformanceCounter(name) * TimerEntity::GetFreqInv();
	}
	// 経過時間をパフォーマンスカウンタのまま取得
	ngl::s64 Timer::GetPerformanceCounter(const char* name)
	{
		TimerMapType::iterator it = timers_.find(name);
		if (timers_.end() == it)
		{
			return 0;
		}
		QueryPerformanceCounter(&it->second.time_end);
		return (it->second.time_end.QuadPart - it->second.time_begin.QuadPart - GetSuspendTotalPerformanceCounter(name));
	}
	void	Timer::RemoveTimer(const char* name)
	{
		TimerMapType::iterator it = timers_.find(name);
		if (timers_.end() == it)
		{
			return;
		}
		timers_.erase(it);
	}

	// タイマーを一時停止
	void	Timer::SuspendTimer(const char* name)
	{
		TimerMapType::iterator it = timers_.find(name);
		if (timers_.end() == it)
			return;
		if (0 <= it->second.time_stop_start.QuadPart)
			return;
		QueryPerformanceCounter(&it->second.time_stop_start);
	}
	// タイマーを再開
	void	Timer::ResumeTimer(const char* name)
	{
		TimerMapType::iterator it = timers_.find(name);
		if (timers_.end() == it)
			return;
		if (0 > it->second.time_stop_start.QuadPart)
			return;
		LARGE_INTEGER time;
		QueryPerformanceCounter(&time);
		it->second.time_suspend_total.QuadPart += time.QuadPart - it->second.time_stop_start.QuadPart;
		it->second.time_stop_start.QuadPart = -1;
	}
	// 
	ngl::s64 Timer::GetSuspendTotalPerformanceCounter(const char* name)
	{
		TimerMapType::iterator it = timers_.find(name);
		if (timers_.end() == it)
			return 0;
		LARGE_INTEGER time;
		QueryPerformanceCounter(&time);

		s64 pc = it->second.time_suspend_total.QuadPart;
		pc += (0 <= it->second.time_stop_start.QuadPart) ? time.QuadPart - it->second.time_stop_start.QuadPart : 0;
		return pc;
	}
}
}
