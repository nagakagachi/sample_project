#pragma once


#include "math/math.h"

namespace ngl
{
namespace rhi
{
    class GraphicsCommandListDep;
}

namespace gfx
{
    namespace helper
    {
        // Commandlistに指定サイズのフルスクリーンViewportとScissorを設定する.
        void SetFullscreenViewportAndScissor(rhi::GraphicsCommandListDep* p_command_list, int width, int height);
        void SetFullscreenViewportAndScissor(rhi::GraphicsCommandListDep* p_command_list, int left, int top, int width, int height);

    }
}
}
