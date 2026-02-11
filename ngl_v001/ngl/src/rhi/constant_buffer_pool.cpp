
#include "rhi/constant_buffer_pool.h"

#include "util/bit_operation.h"

#include "rhi/d3d12/device.d3d12.h"


namespace ngl::rhi
{
    class ConstantBufferPoolImpl;

    class ConstantBufferPooledHandleDeleter
    {
    public:
        ConstantBufferPooledHandleDeleter(ConstantBufferPoolImpl* parent);
        ~ConstantBufferPooledHandleDeleter();
        void operator()(ConstantBufferPoolItem* item);
    private:
        ConstantBufferPoolImpl* parent_ = nullptr;
    };


    static ConstantBufferPoolItem* CreateConstantBufferPoolItem(rhi::DeviceDep* p_device, int byte_size)
    {
        auto* item = new ConstantBufferPoolItem();
        {
            // Bufferの初期化.
            rhi::BufferDep::Desc cb_desc{};
            cb_desc.SetupAsConstantBuffer(byte_size);
            item->buffer.Initialize(p_device, cb_desc);
            
            // ConstantBufferViewの初期化.
            rhi::ConstantBufferViewDep::Desc cbv_desc{};
            item->cbv.Initialize(&item->buffer, cbv_desc);
        }
        return item;
    }

    class ConstantBufferPoolBucket
    {
    public:
        ConstantBufferPoolBucket()
        {

        }
        ~ConstantBufferPoolBucket()
        {
            Finalize();
        }

        // 1 < log2_element_byte_size.
        bool Initialize(int log2_element_byte_size)
        {
            assert(1 < log2_element_byte_size && "1 < log2_element_byte_size");
            log2_element_byte_size_ = log2_element_byte_size;
            
            element_byte_size_ = 1 << log2_element_byte_size_;

            return true;
        }
        void Finalize()
        {
            for( auto& item : pool_)
            {
                delete item;
                item = {};
            }   
            pool_.clear();
        }

        ConstantBufferPoolItem* Alloc(rhi::DeviceDep* p_device)
        {
            // シンプルにロック.
            std::scoped_lock<std::mutex> lock(mutex_);

            if(0 < pool_.size())
            {
                // シンプルにPopBack.
                auto item = pool_.back();
                pool_.pop_back();
                return item;
            }
            else
            {
                auto* item = CreateConstantBufferPoolItem(p_device, element_byte_size_);
                return item;
            }

        }

        void Dealloc(ConstantBufferPoolItem* item)
        {
            // シンプルにロック.
            std::scoped_lock<std::mutex> lock(mutex_);

            // シンプルにPushBack.
            pool_.push_back(item);
        }


        std::mutex mutex_;
        int log2_element_byte_size_ = 0;
        int element_byte_size_ = 0;

        std::vector<ConstantBufferPoolItem*> pool_{};
    };


    class ConstantBufferPoolImpl
    {
        friend ConstantBufferPooledHandleDeleter;
    public:
        static constexpr int CalcSizeRoundupExp2(int byte_size)
        {
            // 指定サイズ以上の最小の二の冪数.
            const u32 next_power_of_2 = 1 << (MostSignificantBit32(byte_size-1) + 1);
            return next_power_of_2;
        }
        int CalcMatchBacketElementSize(int byte_size)
        {
            // 指定サイズ以上の最小の二の冪数.
            const int size_power_of_2 = CalcSizeRoundupExp2(byte_size);

            // オフセット下限でクランプ.
            const int target_size = std::max(size_power_of_2, 1 << log2_bucket_log2_min_);
            return target_size;
        }
        int CalcMatchBacketIndex(int byte_size)
        {
            const u32 target_size = CalcMatchBacketElementSize(byte_size);
            // オフセット分を除去.
            return MostSignificantBit32(target_size) - log2_bucket_log2_min_;
        }
    public:
        ConstantBufferPoolImpl()
        {
        }
        ~ConstantBufferPoolImpl()
        {
            for(auto&& bucket : bucket_)
            {
                bucket.Finalize();
            }
        }
        void Initialize(rhi::DeviceDep* p_device)
        {
            assert(p_device != nullptr && "p_device_未設定");
            p_device_ = p_device;


            for(int i = 0; i < std::size(bucket_); ++i)
            {
                const int log2_size = i + log2_bucket_log2_min_;
                const int cb_size = 1 << log2_size;
                
                assert(CalcMatchBacketIndex(cb_size) == i && "バケットインデックス計算が不正.");
                bucket_[i].Initialize(log2_size);
            }
        }
        void Finalize()
        {
            assert(p_device_ != nullptr && "p_device_未設定");
            p_device_ = nullptr;
        }

        // フレーム処理. 返却アイテムのGPU参照可能性フレームの管理など.
        void ReadyToNewFrame()
        {
            //  フレームでの返却スタックからの取り込みや, GPU参照フレーム経過アイテムのPoolへの実際の返却などをする.
            frame_return_index_ = (frame_return_index_ + 1) % frame_return_list_.size();
            // 新規フレームの返却リストにあるアイテムをPoolに移動.
            {
                // シンプルにロック.
                std::scoped_lock<std::mutex> lock(frame_return_list_[frame_return_index_].mutex_);
                
                for(auto* item : frame_return_list_[frame_return_index_].list_)
                {
                    const int bucket_index = CalcMatchBacketIndex(item->buffer.GetElementByteSize());
                    assert( 0 <= bucket_index && "バケットインデックス計算が不正.");
                    if(bucket_.size() > bucket_index)
                    {
                        // Poolから確保しているサイズであればPoolに返却.
                        bucket_[bucket_index].Dealloc(item);
                    }
                    else
                    {
                        // ある程度以上のサイズの場合はPoolしていないので直接削除.
                        delete item;
                    }
                }

                // クリア.
                frame_return_list_[frame_return_index_].list_.clear();
            }
        }

        // ConstantBufferPooledHandleを生成.
        ConstantBufferPooledHandle Alloc(int byte_size)
        {
            assert(0 < byte_size && "ConstantBufferPool::Alloc: サイズが不正.");

            // 指定サイズ以上の最小の二の冪数.
            const u32 pooled_byte_size = CalcMatchBacketElementSize(byte_size);
            const int bucket_index = CalcMatchBacketIndex(byte_size);


            ConstantBufferPoolItem* item{};
            if(bucket_.size() > bucket_index)
            {
                assert(bucket_[bucket_index].element_byte_size_ == pooled_byte_size && "Bucketの担当サイズと不一致");
                // Poolから確保しているサイズであればPoolに返却.
                item = bucket_[bucket_index].Alloc(p_device_);
            }
            else
            {
                // ある程度以上のサイズの場合はPoolしないので直接生成.
                item = CreateConstantBufferPoolItem(p_device_, pooled_byte_size);
            }

            // 共有ハンドル化して返却.
            return std::shared_ptr<ConstantBufferPoolItem>(item, ConstantBufferPooledHandleDeleter(this));
        }

        private:
            // SharedPtrの破棄で呼び出し.
            void DeleterFunc(ConstantBufferPoolItem* item)
            {
                assert(nullptr != item && "不正なポインタ");
                
                {
                    // シンプルにロック.
                    std::scoped_lock<std::mutex> lock(frame_return_list_[frame_return_index_].mutex_);
                    // Push.
                    frame_return_list_[frame_return_index_].list_.push_back(item);
                }
            }

        private:
            rhi::DeviceDep* p_device_ = nullptr;

            int     log2_bucket_log2_min_ = 4;
            std::array<ConstantBufferPoolBucket, 8> bucket_{};

            struct ReturnBuffer
            {
                std::mutex mutex_{};// シンプルにmutex.
                std::vector<ConstantBufferPoolItem*> list_{};
            };
            std::array<ReturnBuffer, 3> frame_return_list_{};
            int frame_return_index_ = {};

        private:

    };


    //-------------------------------------------------------------------
    ConstantBufferPooledHandleDeleter::ConstantBufferPooledHandleDeleter(ConstantBufferPoolImpl* parent)
    : parent_(parent)
    {
        assert(parent_ != nullptr && "ConstantBufferPooledHandleDeleter::parent_未設定");
    }
    ConstantBufferPooledHandleDeleter::~ConstantBufferPooledHandleDeleter()
    {
        assert(parent_ != nullptr && "ConstantBufferPooledHandleDeleter::parent_未設定");
    }

    inline void ConstantBufferPooledHandleDeleter::operator()(ConstantBufferPoolItem* item)
    {
        assert(parent_ != nullptr && "ConstantBufferPooledHandleDeleter::parent_未設定");
        assert(item != nullptr && "ConstantBufferPooledHandleDeleter::operator(): 不正なポインタ.");
        // Parentに破棄処理を依頼.
        parent_->DeleterFunc(item);
    }
    //-------------------------------------------------------------------


    ConstantBufferPool::ConstantBufferPool()
    {
        impl_ = new ConstantBufferPoolImpl();
    }
    ConstantBufferPool::~ConstantBufferPool()
    {
        Finalize();
    }

    void ConstantBufferPool::Initialize(rhi::DeviceDep* p_device)
    {
        assert(impl_ != nullptr && "初期化時にimplが確保されていない");

        impl_->Initialize(p_device);
    }

    void ConstantBufferPool::Finalize()
    {
        if(impl_)
        {
            delete impl_;
            impl_ = nullptr;
        }
    }
    
    void ConstantBufferPool::ReadyToNewFrame()
    {
        assert(impl_ != nullptr && "初期化時にimplが確保されていない");
        impl_->ReadyToNewFrame();
    }

    ConstantBufferPooledHandle ConstantBufferPool::Alloc(int byte_size)
    {
        assert(impl_ != nullptr && "初期化時にimplが確保されていない");
        return impl_->Alloc(byte_size);
    }
}