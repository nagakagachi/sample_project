#pragma once

//  rtg_command_list_pool.h
//  RenderTaskGraphが利用するフレーム寿命CommandListPoolの定義.

#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"

namespace ngl::rtg
{
    namespace
    {
        // tupleの全要素を走査してLambdaを実行するtemplate.
        template <typename F, typename T, std::size_t... indices>
        void ApplyTuple(const F& func, const T& tpl, std::index_sequence<indices...>) {
            using swallow = int[];
            (void)swallow {
                (func(std::get<indices>(tpl)), 0)... 
              };
        };
    }
    
    namespace pool
    {
        using GraphicsCommandListType = rhi::GraphicsCommandListDep;
        using ComputeCommandListType = rhi::ComputeCommandListDep;
        
        template<typename T>
        struct CommandListTypeTraits;
        
        // Graphics.
        template<> struct CommandListTypeTraits<GraphicsCommandListType>
        {
            static constexpr int TypeIndex = 0; // GraphicsのIndex定義.

            static bool Create(rhi::DeviceDep* p_device, rhi::RhiRef<GraphicsCommandListType>& out_ref)
            {
                out_ref = new GraphicsCommandListType();
                if (!out_ref->Initialize(p_device))
                {
                    std::cout << "[ERROR] Graphics CommandList Initialize" << std::endl;
                    assert(false);
                    return false;
                }
                return true;
            }
        };
        // Compute.
        template<> struct CommandListTypeTraits<ComputeCommandListType>
        {
            static constexpr int TypeIndex = 1; // ComputeのIndex定義.
            
            static bool Create(rhi::DeviceDep* p_device, rhi::RhiRef<ComputeCommandListType>& out_ref)
            {
                out_ref = new ComputeCommandListType();
                if (!out_ref->Initialize(p_device))
                {
                    std::cout << "[ERROR] Compute CommandList Initialize" << std::endl;
                    assert(false);
                    return false;
                }
                return true;
            }
        };

        template<typename T>
        struct PooledCommandListElem
        {
            static_assert(0 <= CommandListTypeTraits<T>::TypeIndex);// Indexが定義されているか一応チェック.
            
            rhi::RhiRef<T>  ref_commandlist  = {};      // rhi::RhiRef<CommandListType>.

            // Poolから貸出可能になるまでのフレーム数. 0>=x で貸出可能.
            //  tuple要素イテレートで処理するためにconstで変更可能とするmutable.
            mutable  int    reusable_frame_count = 0;
        };

        // CommandListPoolの管理.
        //  Graphics, Compute等のタイプ別に内部にPoolしたCommandListを貸し出す, フレームカウントで再利用管理する.
        class CommandListPool
        {
        public:
            CommandListPool() = default;
            ~CommandListPool() = default;

            void Initialize(rhi::DeviceDep* p_device);

            void BeginFrame();

            template<typename TCommandList>
            bool GetFrameCommandList(TCommandList*& ref_out);
            
        private:
            using GraphicsCommandListPoolBuffer = std::vector<PooledCommandListElem<GraphicsCommandListType>>;
            using ComputeCommandListPoolBuffer = std::vector<PooledCommandListElem<ComputeCommandListType>>;

            // Tupleでタイプ毎のBufferまとめて管理.
            std::tuple<GraphicsCommandListPoolBuffer, ComputeCommandListPoolBuffer> typed_pool_list_;

        private:
            rhi::DeviceDep* p_device_ = {};
            
            std::mutex	mutex_ = {};
        };

        
        inline void CommandListPool::Initialize(rhi::DeviceDep* p_device)
        {
            assert(p_device);
            p_device_ = p_device;
        }

        inline void CommandListPool::BeginFrame()
        {
            assert(p_device_);

            // TYpe別CommandListPoolの各要素を更新するLambda.
            auto iterate_pool_elem = [](auto& typed_pool)
            {
                for(size_t i = 0; i < typed_pool.size(); ++i)
                {
                    // 再利用可能カウンタ更新.
                    if(0 < typed_pool[i].reusable_frame_count)
                    {
                        --typed_pool[i].reusable_frame_count;
                    }
                }
            };
            constexpr std::size_t tuple_size = std::tuple_size<decltype(typed_pool_list_)>::value;
            // Tupleに格納されたType別CommandListPoolを巡回.
            ApplyTuple( iterate_pool_elem, typed_pool_list_, std::make_index_sequence<tuple_size>()
            );
        }

        // 対応したTypeのCommandListをPoolから取得.
        // Mutex Lock.
        template<typename TCommandList>
        inline bool CommandListPool::GetFrameCommandList(TCommandList*& ref_out)
        {
            constexpr  int type_index = CommandListTypeTraits<TCommandList>::TypeIndex;

            // 素直にMutexLock.
			std::scoped_lock<std::mutex> lock(mutex_);

            auto& target_pool = std::get<type_index>(typed_pool_list_);
            auto cur_size = target_pool.size();
            // 再利用可能なアイテムを検索.
            int lent_index = -1;
            {
                auto it_find = std::find_if(target_pool.begin(), target_pool.end(),
                    [](auto& e)
                    {
                        return 0 >= e.reusable_frame_count;
                    }
                );
                if(target_pool.end() != it_find)
                {
                    lent_index = static_cast<int>(std::distance(target_pool.begin(), it_find));
                }
            }
            // 再利用可能アイテムがない場合は新規生成と登録.
            if(0 > lent_index)
            {
                rhi::RhiRef<TCommandList> new_commandlist = {};
                if(!CommandListTypeTraits<TCommandList>::Create(p_device_, new_commandlist))
                {
                    assert(false);
                    return false;
                }

                PooledCommandListElem<TCommandList> new_item = {};
                new_item.ref_commandlist = new_commandlist;

                target_pool.push_back(new_item);// 追加.

                lent_index = static_cast<int>(target_pool.size()) - 1;// 追加した終端を使用.
            }
            assert(lent_index < target_pool.size());

            // BeginFrameでカウントダウンされて0で再利用可能. Render0->GPU1->Render2 で2F想定.
            target_pool[lent_index].reusable_frame_count = 2;
            // 返却.
            ref_out = target_pool[lent_index].ref_commandlist.Get();
            
            return true;
        }
    
    }
}