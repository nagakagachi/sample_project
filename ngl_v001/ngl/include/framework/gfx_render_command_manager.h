#pragma once

#include <mutex>
#include <vector>

#include "util/singleton.h"

namespace ngl::fwk
{
    // RenderCommand Lambda定義の引数簡易化のための定義.
    struct _CommonRenderCommandArg
    {
        rhi::GraphicsCommandListDep* command_list;
    };
    using ComonRenderCommandArg = const _CommonRenderCommandArg&;
    // RenderCommand登録Lambda型.
    using CommonRenderCommandType = std::function<void(ComonRenderCommandArg)>;

    // 次のRenderThreadの先頭で実行される標準的なRenderComandをLambdaで登録.
    void PushCommonRenderCommand(const CommonRenderCommandType& func);


    // Frame毎のRenderCommandをバッファリングし, システムから実行するためのクラス.
    class GfxRenderCommandManager : public Singleton<GfxRenderCommandManager>
    {
    public:
        GfxRenderCommandManager() = default;
        ~GfxRenderCommandManager() = default;

    public:
        // RenderComamndの処理を登録.
        //  LambdaはRenderThreadの先頭で実行され, またフレームのGPUタスクとして先頭でSubmitされるGraphicsCommandListを引数にうける.
        void PushCommonRenderCommand(const CommonRenderCommandType& func);
        
    public:
        void Execute(rhi::GraphicsCommandListDep* command_list);
        
    private:
        std::mutex m_mutex;
        int flip_{};
        std::array<std::vector<CommonRenderCommandType>, 2> command_buffer;
    };

    
}