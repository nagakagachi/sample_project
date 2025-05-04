#pragma once
/*

	// IGraphicsTaskNode を継承してPreZPassを実装する例.
	struct TaskDepthPass : public rtg::IGraphicsTaskNode
	{
		rtg::ResourceHandle h_depth_{};// RTGリソースハンドル保持.
		
		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, const SetupDesc& desc)
		{
			// リソース定義.
			rtg::ResourceDesc2D depth_desc = rtg::ResourceDesc2D::CreateAsAbsoluteSize(1920, 1080, gfx::MaterialPassPsoCreator_depth::k_depth_format);
			// 新規作成したDepthBufferリソースをDepthTarget使用としてレコード.
			h_depth_ = builder.RecordResourceAccess(*this, builder.CreateResource(depth_desc), rtg::access_type::DEPTH_TARGET);

			// 実際のレンダリング処理をLambda登録. RTGのCompile後ExecuteでTaskNode毎のLambdaが並列実行されCommandList生成される.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator commandlist_allocator)
				{
					command_list_allocator.Alloc(1);
					auto* commandlist = command_list_allocator.GetOrCreate(0);
					
					// ハンドルからリソース取得. 必要なBarrier/ステート遷移はRTGシステムが担当するため, 個々のTaskは必要な状態になっているリソースを使用できる.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					assert(res_depth.tex_.IsValid() && res_depth.dsv_.IsValid());

					// 例:クリア
					commandlist->ClearDepthTarget(res_depth.dsv_.Get(), 0.0f, 0, true, true);// とりあえずクリアだけ.ReverseZなので0クリア.

					// 例:DepthRenderTagetとして設定してレンダリング.
					commandlist->SetRenderTargets(nullptr, 0, res_depth.dsv_.Get());
					ngl::gfx::helper::SetFullscreenViewportAndScissor(commandlist, res_depth.tex_->GetWidth(), res_depth.tex_->GetHeight());
					// TODO. 描画.
					// ....
				});
		}
	};

	// IGraphicsTaskNode を継承してHardwareDepthからLinearDepthを計算するPass例.
	//	先行するPreZPass(DepthPass)の書き込み先リソースハンドルを読み取り使用し, LinearDepthを出力する.
	//	別のTaskのリソースハンドルを利用する例.
	struct TaskLinearDepthPass : public rtg::IGraphicsTaskNode
	{
		rtg::ResourceHandle h_depth_{};
		rtg::ResourceHandle h_linear_depth_{};

		// リソースとアクセスを定義するプリプロセス.
		void Setup(rtg::RenderTaskGraphBuilder& builder, rhi::DeviceDep* p_device, rtg::ResourceHandle h_depth, rtg::ResourceHandle h_tex_compute, const SetupDesc& desc)
		{
			// LinearDepth出力用のバッファを新規に作成してUAV使用としてレコード.
			rtg::ResourceDesc2D linear_depth_desc = rtg::ResourceDesc2D::CreateAsAbsoluteSize(1920, 1080, rhi::EResourceFormat::Format_R32_FLOAT);
			h_linear_depth_ = builder.RecordResourceAccess(*this, builder.CreateResource(linear_depth_desc), rtg::access_type::UAV);
			
			// 先行するDepth書き込みTaskの出力先リソースハンドルを利用し, 読み取り使用としてレコード.
			h_depth_ = builder.RecordResourceAccess(*this, h_depth, rtg::access_type::SHADER_READ);

			// 実際のレンダリング処理をLambda登録. RTGのCompile後ExecuteでTaskNode毎のLambdaが並列実行されCommandList生成される.
			builder.RegisterTaskNodeRenderFunction(this,
				[this](rtg::RenderTaskGraphBuilder& builder, rtg::TaskGraphicsCommandListAllocator commandlist_allocator)
				{
					command_list_allocator.Alloc(1);
					auto* commandlist = command_list_allocator.GetOrCreate(0);
					
					// ハンドルからリソース取得. 必要なBarrierコマンドは外部で発行済である.
					auto res_depth = builder.GetAllocatedResource(this, h_depth_);
					auto res_linear_depth = builder.GetAllocatedResource(this, h_linear_depth_);
					// TODO. depthからlinear_depthを生成するシェーダディスパッチ.
					// ...
				});
		}
	};


	// ------------------------------------------
	// BuilderによるPassグラフの定義例.
	// 最初にDepthPass
	auto* task_depth = rtg_builder.AppendTaskNode<ngl::render::task::TaskDepthPass>();
	// ...

	auto* task_linear_depth = rtg_builder.AppendTaskNode<ngl::render::task::TaskLinearDepthPass>();
	// 先行するDepthPassの出力Depthハンドルを読み取り使用でレコードするため引数にとる.
	task_linear_depth->Setup(..., task_depth->h_depth_, ...);


	// 次回フレームへの伝搬. このGraphで生成されたハンドルとそのリソースを次フレームでも利用できるようにする. ヒストリバッファ用の機能.
	h_propagate_lit = rtg_builder.PropagateResourceToNextFrame(task_light->h_light_);


	// Compile.
	//	ManagerでCompileを実行する. ここで内部リソースプールからのリソース割り当てやTask間のリソースステート遷移スケジュールを確定.
	//	Compileによって各種スケジューリングが決定されるため, 必ずExecuteとGPU-Submitをして実行をしなければリソース整合性が破綻する点に注意.
	//		また複数のGraphをCompileした場合はその順番でGpu-Submitしなければならない
	rtg_manager.Compile(rtg_builder);
		
	// Graphを構成するTaskの Render処理Lambda を実行し, CommandListを生成する.
	rtg_builder.Execute(out_graphics_cmd, out_compute_cmd);

	// out_graphics_cmd と out_compute_cmd は非同期コンピュートを考慮したコマンドリスト列.
	// Managerのヘルパー関数でGPUへSubmitする.
	ngl::rtg::RenderTaskGraphBuilder::SubmitCommand(graphics_queue_, compute_queue_, out_graphics_cmd, out_compute_cmd);
 */


#include <unordered_map>
#include <mutex>

#include "rtg_common.h"

#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/shader.d3d12.h"
#include "rhi/d3d12/command_list.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

#include "resource/resource_manager.h"

#include "rtg_command_list_pool.h"

#include "thread/job_thread.h"

namespace ngl
{
	// Render Task Graph.
	namespace rtg
	{	
		// 生成はRenderTaskGraphBuilder経由.
		// GraphicsTaskの基底クラス.
		struct IGraphicsTaskNode : public ITaskNode
		{
		public:
			virtual ~IGraphicsTaskNode() = default;
			// Type Graphics.
			ETASK_TYPE TaskType() const final
			{ return ETASK_TYPE::GRAPHICS; }
		};
		// ComputeTaskの基底クラス.
		// GraphicsでもAsyncComputeでも実行可能なもの. UAVバリア以外のバリアは出来ないようにComputeCommandListのみ利用可能とする.
		// このTaskで利用するリソースのためのState遷移はRtg側の責任とする.
		struct IComputeTaskNode : public ITaskNode
		{
		public:
			virtual ~IComputeTaskNode() = default;
			// Type AsyncCompute.
			ETASK_TYPE TaskType() const final
			{ return ETASK_TYPE::COMPUTE; }
		};

		
		// レンダリングパスのシーケンスとそれらのリソース依存関係解決.
		//	このクラスのインスタンスは　TaskNodeのRecord, Compile, Execute の一連の処理の後に使い捨てとなる. これは使いまわしのための状態リセットの実装ミスを避けるため.
		//  TaskNode内部の一時リソースやTaskNode間のリソースフローはHandleを介して記録し, Compileによって実際のリソース割当や状態遷移の解決をする.
		// 	CommandListはCreateされたTaskNodeの順序となる.
		//	AsyncComputeのTaskNodeもサポート予定で, Fence風のSignalとWeightによってGPU側での同期を記述することを検討中.
		class RenderTaskGraphBuilder
		{
			friend class RenderTaskGraphManager;
			using TaskNodeRenderFunctionType_Graphics =
				const std::function<void(rtg::RenderTaskGraphBuilder& builder, TaskGraphicsCommandListAllocator command_list_allocator)>;
			using TaskNodeRenderFunctionType_Compute =
				const std::function<void(rtg::RenderTaskGraphBuilder& builder, TaskComputeCommandListAllocator command_list_allocator)>;
			
		public:
			RenderTaskGraphBuilder() = default;
			RenderTaskGraphBuilder(int base_resolution_width, int base_resolution_height)
			{
				res_base_height_ = base_resolution_height;
				res_base_width_ = base_resolution_width;
			}
			
			~RenderTaskGraphBuilder();

			// ITaskBase派生クラスをシーケンスの末尾に新規生成する.
			// GraphicsおよびAsyncCompute両方を登録する. それぞれのタイプ毎での実行順は登録順となる. GraphicsとAsyncComputeの同期ポイントは別途指示する予定.
			template<typename TTaskNode>
			TTaskNode* AppendTaskNode()
			{
				// Compile前のRecordフェーズでのみ許可.
				assert(IsRecordable());
				
				auto new_node = new TTaskNode();
				node_sequence_.push_back(new_node);
				return new_node;
			}

			// GraphicsTask用のRender処理登録. IGraphicsTaskNode派生Taskはこの関数で自身のRender処理を登録する.
			//	void(rtg::RenderTaskGraphBuilder& builder, TaskGraphicsCommandListAllocator command_list_allocator)>
			void RegisterTaskNodeRenderFunction(const IGraphicsTaskNode* node, const TaskNodeRenderFunctionType_Graphics& render_function);
			// AsyncComputeTask用のRender処理登録. IComputeTaskNode派生Taskはこの関数で自身のAsyncCompute Render処理を登録する.
			//	void(rtg::RenderTaskGraphBuilder& builder, TaskComputeCommandListAllocator command_list_allocator)>
			void RegisterTaskNodeRenderFunction(const IComputeTaskNode* node, const TaskNodeRenderFunctionType_Compute& render_function);

		public:
			// リソースハンドルを生成.
			//	Graph内リソースを確保してハンドルを取得する.
			RtgResourceHandle CreateResource(RtgResourceDesc2D res_desc);

			// Nodeからのリソースアクセスを記録.
			// NodeのRender実行順と一致する順序で登録をする必要がある. この順序によってリソースステート遷移の確定や実リソースの割当等をする.
			RtgResourceHandle RecordResourceAccess(const ITaskNode& node, const RtgResourceHandle res_handle, const ACCESS_TYPE access_type);
			
			// 次のフレームへ寿命を延長する.
			//	TAA等で前回フレームのリソースを利用したい場合に, この関数で寿命を次回フレームまで延長することで同じハンドルで同じリソースを利用できる.
			RtgResourceHandle PropagateResourceToNextFrame(RtgResourceHandle handle);

			// 外部リソースを登録してハンドルを生成. 一般.
			//	rtv,dsv,srv,uavはそれぞれ登録するものだけ有効な参照を指定する.
			// curr_state			: 外部リソースのGraph開始時点のステート.
			// nesesary_end_state	: 外部リソースのGraph実行完了時点で遷移しているべきステート. 外部から要求する最終ステート遷移.
			RtgResourceHandle RegisterExternalResource(rhi::RefTextureDep tex, rhi::RefRtvDep rtv, rhi::RefDsvDep dsv, rhi::RefSrvDep srv, rhi::RefUavDep uav,
				rhi::EResourceState curr_state, rhi::EResourceState nesesary_end_state);
			
			// 外部リソースを登録してハンドルを生成. Swapchain用.
			// curr_state			: 外部リソースのGraph開始時点のステート.
			// nesesary_end_state	: 外部リソースのGraph実行完了時点で遷移しているべきステート. 外部から要求する最終ステート遷移.
			RtgResourceHandle RegisterSwapchainResource(rhi::RhiRef<rhi::SwapChainDep> swapchain, rhi::RefRtvDep swapchain_rtv,
				rhi::EResourceState curr_state, rhi::EResourceState nesesary_end_state);
			
			// Swapchainリソースハンドルを取得. 外部リソースとしてSwapchainは特別扱い.
			RtgResourceHandle GetSwapchainResourceHandle() const;
			
			// Handleのリソース定義情報を取得.
			RtgResourceDesc2D GetResourceHandleDesc(RtgResourceHandle handle) const;

		public:
			// Graph実行.
			// Compile済みのGraphを実行しCommandListを構築する. JobSystemが指定された場合は利用して並列実行する.
			// 結果はQueueへSubmitするCommandListとFenceのSequence.
			// 結果のSequenceは外部でQueueに直接Submitすることも可能であるが, ヘルパ関数SubmitCommand()を利用することを推奨する.
			void Execute(
				std::vector<RtgSubmitCommandSequenceElem>& out_graphics_commands, std::vector<RtgSubmitCommandSequenceElem>& out_compute_commands,
				thread::JobSystem* p_job_system = nullptr
				);

			// RtgのExecute() で構築して生成したComandListのSequenceをGPUへSubmitするヘルパー関数.
			static void SubmitCommand(
				rhi::GraphicsCommandQueueDep& graphics_queue, rhi::ComputeCommandQueueDep& compute_queue,
				std::vector<RtgSubmitCommandSequenceElem>& graphics_commands, std::vector<RtgSubmitCommandSequenceElem>& compute_commands);
			
		public:
			// NodeのHandleに対して割り当て済みリソースを取得する.
			// Graphシステム側で必要なBarrierコマンドを発効するため基本的にNode実装側ではBarrierコマンドは不要.
			RtgAllocatedResourceInfo GetAllocatedResource(const ITaskNode* node, RtgResourceHandle res_handle) const;
			// -------------------------------------------------------------------------------------------

		private:
			enum class EBuilderState
			{
				RECORDING,
				COMPILED,
				EXECUTED
			};
			// Builderの状態.
			EBuilderState	state_ = EBuilderState::RECORDING;
			
			class RenderTaskGraphManager* p_compiled_manager_ = nullptr;// Compileを実行したManager. 割り当てられたリソースなどはこのManagerが持っている.
			uint32_t compiled_order_id_ = {};
			
			// -------------------------------------------------------------------------------------------
			static constexpr  int k_base_height = 1080;
			int res_base_height_ = k_base_height;
			int res_base_width_ = static_cast<int>( static_cast<float>(k_base_height) * 16.0f/9.0f);
			
			std::vector<ITaskNode*> node_sequence_{};// Graph構成ノードシーケンス. 生成順がGPU実行順で, AsyncComputeもFenceで同期をする以外は同様.
			std::unordered_map<const ITaskNode*, TaskNodeRenderFunctionType_Graphics> node_function_graphics_{};// Node毎のRender処理Lambda登録用(Graphics Queue).
			std::unordered_map<const ITaskNode*, TaskNodeRenderFunctionType_Compute> node_function_compute_{};// Node毎のRender処理Lambda登録用(Compute Queue).

			std::unordered_map<RtgResourceHandleKeyType, RtgResourceDesc2D> handle_2_desc_{};// Handleからその定義のMap.
			
			struct NodeHandleUsageInfo
			{
				RtgResourceHandle		handle{};// あるNodeからどのようなHandleで利用されたか.
				ACCESS_TYPE				access{};// あるNodeから上記Handleがどのアクセスタイプで利用されたか.
			};
			std::unordered_map<const ITaskNode*, std::vector<NodeHandleUsageInfo>> node_handle_usage_list_{};// Node毎のResourceHandleアクセス情報をまとめるMap.
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// Importリソース用のmap.
			std::vector<ExternalResourceInfo>					imported_resource_ = {};
			std::unordered_map<RtgResourceHandleKeyType, int>	imported_handle_2_index_ = {};

			// ImportしたSwapchainは何かとアクセスするため専用にHandle保持.
			RtgResourceHandle									handle_imported_swapchain_ = {};
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			
			// 次フレームまで寿命を延長するハンドルのmap. keyのみ利用, valueは現在未使用.
			std::unordered_map<RtgResourceHandleKeyType, int>	propagate_next_handle_ = {};
			// ------------------------------------------------------------------------------------------------------------------------------------------------------

			
			// ------------------------------------------------------------------------------------------------------------------------------------------------------
			// Compileで構築される情報.
			struct CompiledBuilder
			{
				struct NodeDependency
				{
					int from = -1;
					int to = -1;
					int fence_id = -1;// Wait側に格納されるFence. Signal側はtoの指すIndexが持つこのIDを利用する.
				};

				// Compileで構築される情報.
				// Handleに割り当てられたリソースのPool上のIndex.
				// このままMapのキーとして利用するためuint64扱いできるようにしている(もっと整理できそう).
				using CompiledResourceInfoKeyType = uint64_t;  
				struct CompiledResourceInfo
				{
					union
					{
						// (u64)0は特殊IDで無効扱い. unique_idが0でもswapchainビットが1であれば有効.
						CompiledResourceInfoKeyType data = {};
						struct Detail
						{
							int32_t	 resource_id;		// 内部リソースプール又は外部リソースリストへの参照.
							uint32_t is_external	: 1; // 外部リソースマーク.
							uint32_t dummy			: 31;
						}detail;
					};
					
					CompiledResourceInfo() = default;
					constexpr CompiledResourceInfo(CompiledResourceInfoKeyType data)
					{ this->data = data; }
					
					operator CompiledResourceInfoKeyType() const
					{ return data; }

					// 無効値.
					static constexpr CompiledResourceInfo k_invalid()
					{
						CompiledResourceInfo tmp = {};
						tmp.detail.resource_id = -1;// 無効値.
						return tmp;
					}
				};
				
				struct NodeHandleState
				{
					rhi::EResourceState prev_ = {};
					rhi::EResourceState curr_ = {};
				};
			
				// Queue違いのNode間のfence依存関係.
				std::vector<NodeDependency>							node_dependency_fence_ = {};
				// HandleからリニアインデックスへのMap.
				std::unordered_map<RtgResourceHandleKeyType, int>	handle_2_linear_index_ = {};
				// NodeSequenceの順序に沿ったHandle配列.
				std::vector<RtgResourceHandle>						linear_handle_array_ = {};
				// Handleのリニアインデックスから割り当て済みリソースID.
				std::vector<CompiledResourceInfo>					linear_handle_resource_id_ = {};
				// NodeのHandle毎のリソース状態遷移.
				std::unordered_map<const ITaskNode*, std::unordered_map<RtgResourceHandleKeyType, NodeHandleState>> node_handle_state_ = {};
			};
			CompiledBuilder compiled_{};
			// ------------------------------------------------------------------------------------------------------------------------------------------------------

		private:
			// グラフからリソース割当と状態遷移を確定.
			// 現状はRenderThreadでCompileしてそのままRenderThreadで実行するというスタイルとする.
			bool Compile(class RenderTaskGraphManager& manager);
			
			// Sequence上でのノードの位置を返す.
			int GetNodeSequencePosition(const ITaskNode* p_node) const;

			// Builderの状態取得用.
			bool IsRecordable() const;
			bool IsCompilable() const;
			bool IsExecutable() const;
			
			// ------------------------------------------
			// 外部リソースを登録共通部.
			RtgResourceHandle RegisterExternalResourceCommon(
				rhi::RefTextureDep tex, rhi::RhiRef<rhi::SwapChainDep> swapchain, rhi::RefRtvDep rtv, rhi::RefDsvDep dsv, rhi::RefSrvDep srv, rhi::RefUavDep uav,
				rhi::EResourceState curr_state, rhi::EResourceState nesesary_end_state);
		};



	// ------------------------------------------------------------------------------------------------------------------------------------------------------
		// Rtg core system.
		// RenderTaskGraphBuilderのCompileやそれらが利用するリソースの永続的なプール管理.
		class RenderTaskGraphManager
		{
			friend class RenderTaskGraphBuilder;
			
		public:
			RenderTaskGraphManager() = default;
			~RenderTaskGraphManager();
		public:
			// 初期化.
			bool Init(rhi::DeviceDep* p_device, int job_thread_count = 8);

			//	フレーム開始通知. Game-Render同期中に呼び出す.
			//		内部リソースプールの中で一定フレームアクセスされていないものを破棄するなどの処理.
			void BeginFrame();

			// builderをCompileしてリソース割当を確定する.
			//	Compileしたbuilderは必ずExecuteする必要がある.
			//	また, 複数のbuilderをCompileした場合はCompileした順序でExecuteが必要(確定したリソースの状態遷移コマンド実行を正しい順序で実行するために).
			bool Compile(RenderTaskGraphBuilder& builder);
			
		public:
			// Builderが利用するCommandListをPoolから取得(Graphics).
			void GetNewFrameCommandList(rhi::GraphicsCommandListDep*& out_ref)
			{
				commandlist_pool_.GetFrameCommandList(out_ref);
			}
			// Builderが利用するCommandListをPoolから取得(Compute).
			void GetNewFrameCommandList(rhi::ComputeCommandListDep*& out_ref)
			{
				commandlist_pool_.GetFrameCommandList(out_ref);
			}

		public:
			rhi::DeviceDep* GetDevice()
			{
				return p_device_;
			}
			
			thread::JobSystem* GetJobSystem()
			{
				return &job_system_;
			}
			
		private:
			// Poolからリソース検索または新規生成. 戻り値は実リソースID.
			//	検索用のリソース定義keyと, アクセス期間外の再利用のためのアクセスステージ情報を引数に取る.
			//	access_stage : リソース再利用を有効にしてアクセス開始ステージを指定する, nullptrの場合はリソース再利用をしない.
			int GetOrCreateResourceFromPool(ResourceSearchKey key, const TaskStage* p_access_stage_for_reuse = nullptr);
			// プールリソースの最終アクセス情報を書き換え. BuilderのCompile時の一時的な用途.
			void SetInternalResouceLastAccess(int resource_id, TaskStage last_access_stage);
			// 割り当て済みリソース番号から内部リソースポインタ取得.
			InternalResourceInstanceInfo* GetInternalResourcePtr(int resource_id);

			// BuilderからハンドルとリソースIDを紐づけて次のフレームへ伝搬する.
			void PropagateResourceToNextFrame(RtgResourceHandle handle, int resource_id);
			// 伝搬されたハンドルに紐付けられたリソースIDを検索.
			int FindPropagatedResourceId(RtgResourceHandle handle);
			
		private:
			rhi::DeviceDep* p_device_ = nullptr;

			// 同一Manager下のBuilderのCompileは排他処理.
			std::mutex	compile_mutex_ = {};
			
			// Compileで割り当てられるリソースのPool.
			std::vector<InternalResourceInstanceInfo> internal_resource_pool_ = {};
			
			// 次のフレームへ伝搬するハンドルとリソースIDのMap.
			std::unordered_map<RtgResourceHandleKeyType, int> propagate_next_handle_[2] = {};
			// 次のフレームへ伝搬するハンドル登録用FlipIndex. 前回フレームから伝搬されたハンドルは 1-flip_propagate_next_handle_next_ のMapが対応.
			int flip_propagate_next_handle_next_ = 0;
			std::unordered_map<RtgResourceHandleKeyType, int> propagate_next_handle_temporal_ = {};
			
			pool::CommandListPool commandlist_pool_ = {};
		private:
			// JobSystem. 専用に内部で持っているが要検討.
			thread::JobSystem	job_system_;
		private:
			// ユニークなハンドルIDを取得.
			//	TODO. 64bit.
			static uint32_t GetNewHandleId();
			static uint32_t	s_res_handle_id_counter_;// リソースハンドルユニークID. 生成のたびに加算しユニーク識別.
		};
		
		// ------------------------------------------------------------------------------------------------------------------------------------------------------


		// ------------------------------------------------------------------------------------------------------------------------------------------------------
		template<typename COMMAND_LIST_TYPE>
		TaskCommandListAllocator<COMMAND_LIST_TYPE>::TaskCommandListAllocator(std::vector<rhi::CommandListBaseDep*>* task_command_list_buffer, int user_command_list_offset, RenderTaskGraphManager* manager)
			: command_list_array_(task_command_list_buffer), user_command_list_array_offset_(user_command_list_offset), manager_(manager)
		{
			assert(task_command_list_buffer && manager && u8"初期化引数エラー");
		}
		template<typename COMMAND_LIST_TYPE>
		void TaskCommandListAllocator<COMMAND_LIST_TYPE>::Alloc(int num_command_list)
		{
			const int require_command_list_count = (num_command_list + user_command_list_array_offset_);
			if(require_command_list_count > command_list_array_->size())
			{
				command_list_array_->resize(require_command_list_count, nullptr);// 増加分はnullptr fill.
			}
		}
		template<typename COMMAND_LIST_TYPE>
		int TaskCommandListAllocator<COMMAND_LIST_TYPE>::NumAllocatedCommandList() const
		{
			return ((int)command_list_array_->size() - user_command_list_array_offset_);
		}
		template<typename COMMAND_LIST_TYPE>
		COMMAND_LIST_TYPE* TaskCommandListAllocator<COMMAND_LIST_TYPE>::GetOrCreate(int index)
		{
			assert(NumAllocatedCommandList() > index && u8"範囲外アクセス");
			
			const int command_list_index = index + user_command_list_array_offset_;// オフセット分をスキップした位置にアクセス.
			if(nullptr == command_list_array_->at(command_list_index))
			{
				// 指定の追加コマンドリストが未確保であればここで確保.
				COMMAND_LIST_TYPE* new_command_list{};
				manager_->GetNewFrameCommandList(new_command_list);
				// 自動的にBeginする.
				new_command_list->Begin();
				// 登録.
				command_list_array_->at(command_list_index) = new_command_list;
			}
			return (COMMAND_LIST_TYPE*)command_list_array_->at(command_list_index);
		}
		template<typename COMMAND_LIST_TYPE>
		COMMAND_LIST_TYPE* TaskCommandListAllocator<COMMAND_LIST_TYPE>::GetOrCreate_Front()
		{
			return GetOrCreate(0);
		}
		template<typename COMMAND_LIST_TYPE>
		COMMAND_LIST_TYPE* TaskCommandListAllocator<COMMAND_LIST_TYPE>::GetOrCreate_Back()
		{
			return GetOrCreate(NumAllocatedCommandList()-1);
		}
		// ------------------------------------------------------------------------------------------------------------------------------------------------------

		
	}
}