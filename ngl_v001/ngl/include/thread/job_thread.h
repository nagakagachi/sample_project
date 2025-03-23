#pragma once

#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace ngl
{
namespace thread
{   
    class SingleJobThread
    {
    public:
        SingleJobThread()
        {
            thread_instance_ = std::thread([&](){Execute();});
        }
        ~SingleJobThread()
        {
            // 実行部停止通知.
            {
                std::unique_lock<std::mutex> lock(condition_mutex_);
                terminate_signal_ = true;
			
                condition_var_.notify_all();
            }
		
            // thread完了待ち.
            thread_instance_.join();
        }

        // Job実行をリクエスト.
        //	以前のJobが実行中の場合は失敗して falseを返す.
        bool Begin(std::function< void(void) > func)
        {
            std::unique_lock<std::mutex> lock(condition_mutex_);
		
            if(job_signal_)
                return false;
		
            func_ = func;
            job_signal_ = true;
		
            condition_var_.notify_all();
            return true;
        }

        // Job実行完了を待機する.
        void Wait()
        {
            std::unique_lock<std::mutex> lock(condition_mutex_);
            // すでに終了していれば即リターン.
            if(!job_signal_)
                return;
		
            // Job完了までConditionで待機.
            condition_var_.wait(lock, [&]{return (!job_signal_ || terminate_signal_);});
        }
	
        // Job実行が可能な状態か.
        bool IsReady()
        {
            std::unique_lock<std::mutex> lock(condition_mutex_);
            return !job_signal_;
        }
	
    private:
        // Job Thread 実行部.
        void Execute()
        {
            while (true)
            {
                // Job実行リクエストか終了通知が来るまで待機.
                std::unique_lock<std::mutex> lock(condition_mutex_);
                condition_var_.wait(lock, [&]{return (job_signal_ || terminate_signal_);});

                if(job_signal_)
                {
                    // Job実行.
                    func_();
                    job_signal_ = false;
                }

                // 完了待ち側への通知.
                condition_var_.notify_all();
			
                // 終了.
                if(terminate_signal_)
                    break;
            }
        }
	
        std::thread	thread_instance_;

        std::mutex				condition_mutex_;
        std::condition_variable	condition_var_;
        bool			terminate_signal_ = false;
        
        bool			job_signal_ = false;	
        std::function< void(void) > func_;
    };




    class JobSystemWorker;
    /*
        JobSystem.
     
        ngl::thread::JobSystem job_system(8);
        ngl::time::Timer::Instance().StartTimer("job_system_test");
        std::atomic_int job_system_test_cnt = 0;
        for(int i = 0; i < 100; ++i)
        {
            job_system.Add([i, &job_system_test_cnt]
            {
                job_system_test_cnt += 1;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                //std::cout <<  i << std::endl;
            });
        }
        job_system.WaitAll();// 待機.
    */
    class JobSystem
    {
        friend JobSystemWorker;
        
    public:
        JobSystem();
        ~JobSystem();

        void Init(int num_max_thread);
        
        void Add(std::function< void(void) > func);

        void WaitAll();
	
    private:
        std::mutex				condition_mutex_;
        std::condition_variable	condition_var_;

        std::list<std::function<void(void)>> job_queue_{};
        
        std::vector<JobSystemWorker*> worker_thread_{};
    };
    
}
}

