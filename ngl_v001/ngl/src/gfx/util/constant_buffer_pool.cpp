
#include "gfx/util/constant_buffer_pool.h"

#include "util/bit_operation.h"

#include "rhi/d3d12/device.d3d12.h"


namespace ngl::gfx
{
    class ConstantBufferPoolImpl;

    class ConstantBufferPoolHandleDeleter
    {
    public:
        ConstantBufferPoolHandleDeleter(ConstantBufferPoolImpl* parent);
        ~ConstantBufferPoolHandleDeleter();
        void operator()(ConstantBufferPoolItem* handle);
    private:
        ConstantBufferPoolImpl* parent_ = nullptr;
    };


    class ConstantBufferPoolImpl
    {
        friend ConstantBufferPoolHandleDeleter;

    public:
        ConstantBufferPoolImpl()
        {

        }
        ~ConstantBufferPoolImpl()
        {

        }
        void Initialize(rhi::DeviceDep* p_device)
        {
            assert(p_device != nullptr && u8"p_device_未設定");
            p_device_ = p_device;
        }
        void Finalize()
        {
            assert(p_device_ != nullptr && u8"p_device_未設定");
            p_device_ = nullptr;
        }

        // ConstantBufferPoolHandleを生成.
        ConstantBufferPoolHandle Alloc(int byte_size)
        {
            assert(0 < byte_size && u8"ConstantBufferPool::Alloc: サイズが不正.");

            // 指定サイズ以上の最小の二の冪数.
            const u32 next_power_of_2 = 1 << (MostSignificantBit32(byte_size-1) + 1);

            // ハンドル実体 new
            auto new_instance = new ConstantBufferPoolItem();
            {
                // Bufferの初期化.
                rhi::BufferDep::Desc cb_desc{};
                cb_desc.SetupAsConstantBuffer(next_power_of_2);
                new_instance->buffer_.Initialize(p_device_, cb_desc);
                
                // ConstantBufferViewの初期化.
                rhi::ConstantBufferViewDep::Desc cbv_desc{};
                new_instance->cbv_.Initialize(&new_instance->buffer_, cbv_desc);
			}


            // 共有ハンドル化して返却.
            return std::shared_ptr<ConstantBufferPoolItem>(new_instance, ConstantBufferPoolHandleDeleter(this));
        }

        private:
            // 共有ハンドルのデストラクタで呼ばれる破棄処理.
            void DeleterFunc(ConstantBufferPoolItem* handle)
            {
                // ここでリソースの解放を行う.
                std::cout << "ConstantBufferPoolHandleDeleter::operator()" << std::endl;

                delete handle;
            }

        private:
            rhi::DeviceDep* p_device_ = nullptr;


    };


    //-------------------------------------------------------------------
    ConstantBufferPoolHandleDeleter::ConstantBufferPoolHandleDeleter(ConstantBufferPoolImpl* parent)
    : parent_(parent)
    {
        assert(parent_ != nullptr && u8"ConstantBufferPoolHandleDeleter::parent_未設定");
    }
    ConstantBufferPoolHandleDeleter::~ConstantBufferPoolHandleDeleter()
    {
        assert(parent_ != nullptr && u8"ConstantBufferPoolHandleDeleter::parent_未設定");
    }

    inline void ConstantBufferPoolHandleDeleter::operator()(ConstantBufferPoolItem* handle)
    {
        assert(parent_ != nullptr && u8"ConstantBufferPoolHandleDeleter::parent_未設定");
        // Parentに破棄処理を依頼.
        parent_->DeleterFunc(handle);
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
        assert(impl_ != nullptr && u8"初期化時にimplが確保されていない");

        impl_->Initialize(p_device);
    }

    void ConstantBufferPool::Finalize()
    {
        assert(impl_ != nullptr && u8"初期化時にimplが確保されていない");
        delete impl_;
        impl_ = nullptr;
    }

    ConstantBufferPoolHandle ConstantBufferPool::Alloc(int byte_size)
    {
        assert(impl_ != nullptr && u8"初期化時にimplが確保されていない");
        return impl_->Alloc(byte_size);
    }
}