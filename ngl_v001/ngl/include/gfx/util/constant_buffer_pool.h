#pragma once


namespace ngl {
namespace gfx {


    struct ConstantBufferPoolHandle
    {
        int dummy{};
    };

    class ConstantBufferPool
    {
    public:
        ConstantBufferPool();
        ~ConstantBufferPool();

        void Initialize(rhi::DeviceDep* p_device);
        void Finalize();


        ConstantBufferPoolHandle Alloc();
        void Free(ConstantBufferPoolHandle handle);

    private:
        rhi::DeviceDep* p_device_{};

    };

    
} // namespace gfx
} // namespace ngl