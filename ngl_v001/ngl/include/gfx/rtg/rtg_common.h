#pragma once

#include "text/hash_text.h"
#include "rhi/rhi.h"

namespace ngl
{
	
	// NGL側のディレクトリのシェーダファイルパスを有効なパスにする.
	static constexpr char k_shader_path_base[] = "../ngl/shader/";
	#define NGL_RENDER_SHADER_PATH(shader_file) text::FixedString<128>("%s/%s", ngl::k_shader_path_base, shader_file)
	static constexpr char k_shader_model[] = "6_3";
	
}

namespace ngl::rtg
{
	class RenderTaskGraphBuilder;
	class RenderTaskGraphManager;
	using RtgNameType = text::HashText<64>;

	
	enum class ETaskType : int
	{
		GRAPHICS = 0,
		COMPUTE,
	};

	// リソースアクセス時のリソース解釈.
	// ひとまず用途のみ指定してそこから書き込みや読み取りなどは自明ということにする. 必要になったら情報追加するなど.
	using AccessTypeValue = int;
	struct AccessType
	{
		static constexpr AccessTypeValue INVALID		= {0};
		
		static constexpr AccessTypeValue RENDER_TARGET	= {1};
		static constexpr AccessTypeValue DEPTH_TARGET	= {2};
		static constexpr AccessTypeValue SHADER_READ	= 3;
		static constexpr AccessTypeValue UAV			= 4;
		
		static constexpr AccessTypeValue _MAX			= 5;
	};

	using AccessTypeMaskValue = int;
	struct AccessTypeMask
	{
		static constexpr AccessTypeMaskValue RENDER_TARGET	= 1 << (AccessType::RENDER_TARGET);
		static constexpr AccessTypeMaskValue DEPTH_TARGET		= 1 << (AccessType::DEPTH_TARGET);
		static constexpr AccessTypeMaskValue SHADER_READ		= 1 << (AccessType::SHADER_READ);
		static constexpr AccessTypeMaskValue UAV				= 1 << (AccessType::UAV);
	};
	inline bool RtgIsWriteAccess(AccessTypeValue type)
	{
		constexpr auto k_write_assecc_mask = AccessTypeMask::RENDER_TARGET | AccessTypeMask::DEPTH_TARGET | AccessTypeMask::UAV;
		const AccessTypeMaskValue mask = 1 << type;
		return 0 != (k_write_assecc_mask & mask);
	}
	

	// Passがリソース(Texture)の定義.
	struct RtgResourceDesc2D
	{
		struct Desc
		{
			struct AbsSize
			{
				int w;// 要求するバッファのWidth (例 1920).
				int h;// 要求するバッファのHeight (例 1080).
			} abs_size;
			rhi::EResourceFormat format {};
			
			// サイズ直接指定. その他データはEmpty.
			static constexpr RtgResourceDesc2D CreateAsAbsoluteSize(int w, int h)
			{
				RtgResourceDesc2D v{};
				v.desc.abs_size = { w, h };
				return v;
			}
		};

		// オブジェクトのHashKey用全域包括Storage.
		// unionでこのオブジェクトがResourceDesc2D全体を包括する.
		struct Storage
		{
			uint64_t a{};
			uint64_t b{};
		};
		
		// データ部.
		union
		{
			Storage storage{};// HashKey用.
			Desc	desc; // 実際のデータ用.
		};

		// サイズ直接指定.
		static constexpr RtgResourceDesc2D CreateAsAbsoluteSize(int w, int h, rhi::EResourceFormat format)
		{
			RtgResourceDesc2D v = Desc::CreateAsAbsoluteSize(w, h);
			v.desc.format = format;
			return v;
		}
		void SetupAsAbsoluteSize(int w, int h, rhi::EResourceFormat format)
		{
			*this = CreateAsAbsoluteSize(w, h, format);
		}

		// 具体的なサイズ(Width, Height)を計算して返す.
		void GetConcreteTextureSize(int work_width, int work_height, int& out_width, int& out_height) const
		{
			{
				out_width = desc.abs_size.w;
				out_height = desc.abs_size.h;
			}
		}
	};
	// StorageをHashKey扱いするためStorageがオブジェクト全体を包括するサイズである必要がある.
	static_assert(sizeof(RtgResourceDesc2D) == sizeof(RtgResourceDesc2D::Storage));
	static constexpr auto sizeof_ResourceDesc2D_Desc = sizeof(RtgResourceDesc2D::Desc);
	static constexpr auto sizeof_ResourceDesc2D_Storage = sizeof(RtgResourceDesc2D::Storage);
	static constexpr auto sizeof_ResourceDesc2D = sizeof(RtgResourceDesc2D);

	
	using RtgResourceHandleKeyType = uint64_t;
	// RTGのノードが利用するリソースハンドル. 識別IDやSwapchain識別等の情報を保持.
	// このままMapのキーとして利用するためuint64扱いできるようにしている(もっと整理できそう).
	struct RtgResourceHandle
	{
		constexpr RtgResourceHandle() = default;
		constexpr RtgResourceHandle(RtgResourceHandleKeyType data)
		{
			this->data = data;
		}
		
		union
		{
			// (u64)0は特殊IDで無効扱い. unique_idが0でもswapchainビットが1であれば有効.
			RtgResourceHandleKeyType data = 0;
			struct Detail
			{
				uint32_t unique_id;

				uint32_t is_external	: 1; // 一般の外部リソース.
				uint32_t is_swapchain	: 1; // 外部リソースとしてSwapchainを特別扱い. とりあえず簡易にアクセスするため.
				uint32_t dummy			: 30;
			}detail;
		};

		static constexpr RtgResourceHandle InvalidHandle()
		{
			return RtgResourceHandle({});
		}
		// 無効はHandleか.
		bool IsInvalid() const
		{
			return detail.unique_id == InvalidHandle().detail.unique_id;
		}
		operator RtgResourceHandleKeyType() const
		{
			return data;
		}
	};
	static constexpr auto sizeof_ResourceHandle = sizeof(RtgResourceHandle);


	
	/*
	Taskの基底.
	直接このクラスを継承することは出来ない.
	IGraphicsTaskBase または IAsyncComputeTaskBase を継承すること.

	TaskNode派生クラスは自身のRender処理LambdaをBuilderに登録することでRTGから呼び出しをうける.
		builder.RegisterTaskNodeRenderFunction(this,
					[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator commandlist_allocator)
					{
						Do Render
					});
	*/
	class ITaskNode
	{
	public:
		virtual ~ITaskNode() {}
		// Type.
		virtual ETaskType TaskType() const = 0;
	public:
		const RtgNameType& GetDebugNodeName() const { return debug_node_name_; }
	protected:
		void SetDebugNodeName(const char* name){ debug_node_name_ = name; }
		RtgNameType debug_node_name_{};
	};


	
	// -------------------------------------------------------------------------------------------
	// Compileで割り当てられたHandleのリソース情報.
	//	TaskNode派生クラスのRender処理内からハンドル経由で取得できるリソース情報.
	struct RtgAllocatedResourceInfo
	{
		RtgAllocatedResourceInfo() = default;

		rhi::EResourceState	prev_state_ = rhi::EResourceState::Common;// NodeからResourceHandleでアクセスした際の直前のリソースステート.
		rhi::EResourceState	curr_state_ = rhi::EResourceState::Common;// NodeからResourceHandleでアクセスした際の現在のリソースステート. RTGによって自動的にステート遷移コマンドが発行される.

		rhi::RefTextureDep				tex_ = {};
		rhi::RhiRef<rhi::SwapChainDep>	swapchain_ = {};// Swapchainの場合はこちらに参照が設定される.

		rhi::RefRtvDep		rtv_ = {};
		rhi::RefDsvDep		dsv_ = {};
		rhi::RefUavDep		uav_ = {};
		rhi::RefSrvDep		srv_ = {};
	};


	// ハンドル毎のタイムライン上での位置を示す情報を生成.
	struct TaskStage
	{
		int step_ = 0;
		
		constexpr TaskStage() = default;
        constexpr TaskStage(int step)
		: step_(step){
        };

		// Stage 0 に対して常に前となるようなStage. リソースプール側リソースのリセットや新規生成リソースのステージとして利用.
		static constexpr TaskStage k_frontmost_stage()
		{
			return TaskStage{std::numeric_limits<int>::min()};
		}
		// 最終端のStage.
		static constexpr TaskStage k_endmost_stage()
		{
			return TaskStage{std::numeric_limits<int>::max()};
		}
		
		// オペレータ.
		constexpr bool operator<(const TaskStage arg) const;
		constexpr bool operator>(const TaskStage arg) const;
		constexpr bool operator<=(const TaskStage arg) const;
		constexpr bool operator>=(const TaskStage arg) const;
	};
	
	// リソースの検索キー.
	struct ResourceSearchKey
	{
		rhi::EResourceFormat format = {};
		int require_width_ = {};
		int require_height_ = {};
		AccessTypeMaskValue	usage_ = {};// 要求する RenderTarget, DepthStencil, UAV等の用途.
	};
	
	// 内部リソースプール用.
	struct InternalResourceInstanceInfo
	{
		// 未使用フレームカウンタ. 一定フレーム未使用だった内部リソースはPoolからの破棄をする.
		int			unused_frame_counter_ = 0;
		
		TaskStage last_access_stage_ = {};// Compile中のシーケンス上でのこのリソースへ最後にアクセスしたタスクの情報. Compile完了後にリセットされる.
			
		rhi::EResourceState	cached_state_ = rhi::EResourceState::Common;// Compileで確定したGraph終端でのステート.
		rhi::EResourceState	prev_cached_state_ = rhi::EResourceState::Common;// 前回情報. Compileで確定したGraph終端でのステート.
			
		rhi::RefTextureDep	tex_ = {};
		
		rhi::RefRtvDep		rtv_ = {};
		rhi::RefDsvDep		dsv_ = {};
		rhi::RefUavDep		uav_ = {};
		rhi::RefSrvDep		srv_ = {};

		bool IsValid() const
		{
			return tex_.IsValid();// 元リソースがあれば有効.
		}
	};
	// 外部リソース登録用. 内部リソース管理クラスを継承して追加情報.
	struct ExternalResourceInfo : public InternalResourceInstanceInfo
	{
		rhi::RhiRef<rhi::SwapChainDep>	swapchain_ = {}; // 外部リソースの場合はSwapchainもあり得るため追加.
		
		rhi::EResourceState	require_begin_state_ = rhi::EResourceState::Common;// 外部登録で指定された開始ステート.
		rhi::EResourceState	require_end_state_ = rhi::EResourceState::Common;// 外部登録で指定された終了ステート. Executeの終端で遷移しているべきステート.
	};
	
	// Execute結果のCommandSequence要素のタイプ.
	enum ERtgSubmitCommandType
	{
		CommandList,
		Signal,
		Wait
	};
	// Execute結果のCommandSequence要素.
	struct RtgSubmitCommandSequenceElem
	{
		ERtgSubmitCommandType type = ERtgSubmitCommandType::CommandList;

		// ERtgSubmitCommandType == CommandList
		//	Rtg管理化のPoolから現在フレームのみの寿命として割り当てられたCommandList. 内部プール利用の関係でRefではなくポインタ.
		rhi::CommandListBaseDep* command_list = {};

		// ERtgSubmitCommandType == Signal or Wait
		//	Rtg管理化のPoolから現在フレームのみの寿命として割り当てられたFence.
		rhi::RhiRef<rhi::FenceDep>	fence = {};
		//	Rtgがスケジュールした同期のためのSignalまたはWaitで使用するFenceValue.
		u64							fence_value = 0;
	};
	// 単一のRtgBuilderによるCommand.
	struct RtgSubmitCommandSet
	{
		std::vector<ngl::rtg::RtgSubmitCommandSequenceElem> graphics{};
		std::vector<ngl::rtg::RtgSubmitCommandSequenceElem> compute{};
	};

	// Taskが利用するCommandListを取得するためのインターフェイス.
	//	COMMAND_LIST_TYPE = rhi::GraphicsCommandListDep or rhi::ComputeCommandListDep
	template<typename COMMAND_LIST_TYPE>
	class TaskCommandListAllocator
	{
	public:
		// Taskが利用するCommandListを必要分確保する. 既定で 1 確保されている状態.
		//	2以上でAllocした場合でも実際にGetOrCreateでアクセスするまでその要素は生成されない.
		void Alloc(int num_command_list);
		// Allocで確保されたindex番目のCommandListを取得, 未生成ならば生成して取得.
		//	command_listのBegin()及びEnd()は利用者側で呼び出す必要はない.
		COMMAND_LIST_TYPE* GetOrCreate(int index);
		// 先頭CommandList取得or生成取得.
		//	command_listのBegin()及びEnd()は利用者側で呼び出す必要はない.
		COMMAND_LIST_TYPE* GetOrCreate_Front();
		// 末尾CommandList取得or生成取得.
		//	command_listのBegin()及びEnd()は利用者側で呼び出す必要はない.
		COMMAND_LIST_TYPE* GetOrCreate_Back();
		// 確保されているCommandList数.
		int NumAllocatedCommandList() const;
		
	public:
		TaskCommandListAllocator(std::vector<rhi::CommandListBaseDep*>* task_command_list_buffer, int user_command_list_offset, RenderTaskGraphManager* manager);
	private:
		//	Task単位で確保するCommandListの登録先vector. 初期化時に自動解決ステート遷移コマンドを積み込んだCommandlistが一つ登録済みになる.
		std::vector<rhi::CommandListBaseDep*>* command_list_array_{};
		int user_command_list_array_offset_ = 0;
		// 追加CommandList確保用にマネージャ参照.
		RenderTaskGraphManager* manager_{};
	};
	using TaskGraphicsCommandListAllocator = TaskCommandListAllocator<rhi::GraphicsCommandListDep>;
	using TaskComputeCommandListAllocator = TaskCommandListAllocator<rhi::ComputeCommandListDep>;

}