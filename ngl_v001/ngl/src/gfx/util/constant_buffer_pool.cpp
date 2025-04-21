
#include "gfx/util/constant_buffer_pool.h"

#include "rhi/d3d12/device.d3d12.h"

namespace ngl::gfx
{

    ConstantBufferPool::ConstantBufferPool()
    {
    }
    ConstantBufferPool::~ConstantBufferPool()
    {
        Finalize();
    }

    void ConstantBufferPool::Initialize(rhi::DeviceDep* p_device)
    {
        p_device_ = p_device;
    }

    void ConstantBufferPool::Finalize()
    {
    }

    ConstantBufferPoolHandle ConstantBufferPool::Alloc()
    {
        assert(false && "Not implemented yet.");
        return ConstantBufferPoolHandle{};
    }

    void ConstantBufferPool::Free(ConstantBufferPoolHandle handle)
    {
        assert(false && "Not implemented yet.");
    }
}