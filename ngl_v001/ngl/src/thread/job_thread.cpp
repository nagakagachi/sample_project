
#include "thread/job_thread.h"

#include <iostream>


namespace ngl
{
namespace thread
{
    class JobSystemWorker
    {
        friend JobSystem;
    public:
        JobSystemWorker();
        ~JobSystemWorker();

        void Init(JobSystem* p_system);

    private:
        // Job Thread 実行部.
        void Execute();
        
        void Wait();

        JobSystem* p_system_{};
        
        std::thread	thread_instance_;

        bool			terminate_signal_ = false;
        
        bool            job_enable_ = false;
        std::function< void(void) > func_;
    };

    JobSystemWorker::JobSystemWorker()
    {
        thread_instance_ = std::thread([&](){Execute();});
    }
    void JobSystemWorker::Init(JobSystem* p_system)
    {
        p_system_ = p_system;
    }
    JobSystemWorker::~JobSystemWorker()
    {
        // 実行部停止通知.
        {
            std::unique_lock<std::mutex> lock(p_system_->condition_mutex_);
            terminate_signal_ = true;
			
            p_system_->condition_var_.notify_all();
        }
		
        // thread完了待ち.
        thread_instance_.join();

        //std::cout << "~JobSystemWorker" << std::endl;
    }
    // Job実行完了を待機する.
    void JobSystemWorker::Wait()
    {
        std::unique_lock<std::mutex> lock(p_system_->condition_mutex_);
        // Job割当されてなければ即リターン.
        if(!job_enable_)
            return;
		
        // Job完了までConditionで待機.
        p_system_->condition_var_.wait(lock, [&]{return (!job_enable_ || terminate_signal_);});
    }
    void JobSystemWorker::Execute()
    {
        while (true)
        {
            // Job実行リクエストか終了通知が来るまで待機.
            {
                std::unique_lock<std::mutex> lock(p_system_->condition_mutex_);
                if(0 >= p_system_->job_queue_.size())
                {
                    // Jobが積まれていなければ積まれるまで待機.
                    p_system_->condition_var_.wait(lock, [&]{return (0<p_system_->job_queue_.size() || terminate_signal_);});
                }
                
                // Job取り出し.
                if(0<p_system_->job_queue_.size())
                {
                    job_enable_ = true;
                    
                    func_ = p_system_->job_queue_.front();
                    p_system_->job_queue_.pop_front();
                }

                // terminate_signal_の待機側へ通知.
                p_system_->condition_var_.notify_all();
            }
            
            // Job実行.
            if(job_enable_)
            {
                func_();
                
                std::unique_lock<std::mutex> lock(p_system_->condition_mutex_);
                job_enable_ = false;
                
                // job_enable_の待機側へ通知.
                p_system_->condition_var_.notify_all();
            }
            
            // 終了.
            if(terminate_signal_)
                break;
        }
    }

    
    JobSystem::JobSystem()
    {
    }
    JobSystem::~JobSystem()
    {
        for(auto&& j : worker_thread_)
        {
            if(j)
            {
                delete j;
                j = {};
            }
        }
        worker_thread_.clear();
    }
    void JobSystem::Init(int num_max_thread)
    {
        worker_thread_.resize(num_max_thread);
        for(auto&& j : worker_thread_)
        {
            j = new JobSystemWorker();
            j->Init(this);
        }
    }


    void JobSystem::Add(std::function< void(void) > func)
    {
        std::unique_lock<std::mutex> lock(condition_mutex_);
		
        // Jobリストに追加.
        job_queue_.push_back(func);
		
        condition_var_.notify_all();
    }
    
    void JobSystem::WaitAll()
    {
        // この時点で処理待ちのjobがある場合は待機.
        {
            std::unique_lock<std::mutex> lock(condition_mutex_);
            if(0<job_queue_.size())
            {
                // job queue が空になるまで待機.
                condition_var_.wait(lock, [&]{return (0>=job_queue_.size());});
            }
        }
        
        // 各Workerの完了待機.
        for(auto&& j : worker_thread_)
        {
            j->Wait();
        }
    }
    
}
}

