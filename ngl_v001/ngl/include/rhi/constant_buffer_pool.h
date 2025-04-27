#pragma once

#include <memory>

#include "rhi/d3d12/resource.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

namespace ngl {

namespace rhi {
    class DeviceDep;

} // namespace rhi

namespace rhi {

    class ConstantBufferPoolImpl;

    // ConstantBufferPoolの要素.
    struct ConstantBufferPoolItem
    {
        rhi::BufferDep buffer_{};
        rhi::ConstantBufferViewDep cbv_{};
    };
    using ConstantBufferPooledHandle = std::shared_ptr<ConstantBufferPoolItem>;


    // ConstantBufferのPool.
    //  TODO 現在はシンプルなmutex lock方式によるスレッドセーフ実装86453210..
    class ConstantBufferPool
    {
    public:
        ConstantBufferPool();
        ~ConstantBufferPool();

        void Initialize(rhi::DeviceDep* p_device);
        void Finalize();
        void ReadyToNewFrame();

    public:
        ConstantBufferPooledHandle Alloc(int byte_size);

    private:
        ConstantBufferPoolImpl* impl_{};
    };

    
} // namespace gfx
} // namespace ngl