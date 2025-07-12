/*
    gfx_render_command_manager.cpp
*/
#include "framework/gfx_render_command_manager.h"



namespace ngl::fwk
{

    void PushCommonRenderCommand(const CommonRenderCommandType& func)
    {
        GfxRenderCommandManager::Instance().PushCommonRenderCommand(func);
    }

    // 任意のRenderCommandをLambdaで登録可能.
    void GfxRenderCommandManager::PushCommonRenderCommand(const CommonRenderCommandType& func)
    {
        std::scoped_lock<std::mutex> lock(m_mutex);
        command_buffer[flip_].push_back(func);
    }

    // GfxFrameworkのRenderThreadから実行される.
    void GfxRenderCommandManager::Execute(rhi::GraphicsCommandListDep* command_list)
    {
        const auto execute_flip_index = flip_;
        {
            std::scoped_lock<std::mutex> lock(m_mutex);
            // Mutex保護でフリップと次の積み込みバッファクリア.
            flip_ = 1 - flip_;
            command_buffer[flip_].clear();
        }

        // フリップで退避されたバッファを実行.
        _CommonRenderCommandArg command_arg{};
        {
            command_arg.command_list = command_list;
        }
        for (auto&& func : command_buffer[execute_flip_index])
        {
            func(command_arg);
        }
    }
    
}