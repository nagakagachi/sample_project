#pragma once

#include "gfx/rtg/graph_builder.h"

#include "rhi/d3d12/command_list.d3d12.h"


namespace ngl
{

	// Render Task Graph 検証実装.
	namespace rtg
	{

		
		// オペレータ.
		constexpr bool TaskStage::operator<(const TaskStage arg) const
		{
			return step_ < arg.step_;
		}
		constexpr bool TaskStage::operator>(const TaskStage arg) const
		{
			return step_ > arg.step_;
		}
		constexpr bool TaskStage::operator<=(const TaskStage arg) const
		{
			return step_ <= arg.step_;
		}
		constexpr bool TaskStage::operator>=(const TaskStage arg) const
		{
			return step_ >= arg.step_;
		}

		
		// リソースハンドルを生成.
		RtgResourceHandle RenderTaskGraphBuilder::CreateResource(RtgResourceDesc2D res_desc)
		{
			// Compile前のRecordフェーズでのみ許可.
			assert(IsRecordable());
			
			// ID確保.
			const auto new_handle_id = RenderTaskGraphManager::GetNewHandleId();
			
			RtgResourceHandle handle{};
			handle.detail.unique_id = new_handle_id;// ユニークID割当.

			if(handle_2_desc_.end() != handle_2_desc_.find(handle))
			{
				assert(false);
			}
			
			// Desc登録.
			{
				handle_2_desc_[handle] = res_desc;// desc記録.
			}
			
			return handle;
		}

		// 次のフレームへ寿命を延長する.
		//	前回フレームのハンドルのリソースを利用する場合に, この関数で寿命を延長した上で次フレームで同じハンドルを使うことでアクセス可能にする予定.
		RtgResourceHandle RenderTaskGraphBuilder::PropagateResourceToNextFrame(RtgResourceHandle handle)
		{
			// Compile前のRecordフェーズでのみ許可.
			assert(IsRecordable());
			
			propagate_next_handle_[handle] = 0;

			return handle;
		}
		
		// 外部リソースを登録共通部.
		RtgResourceHandle RenderTaskGraphBuilder::RegisterExternalResourceCommon(
			rhi::RefTextureDep tex, rhi::RhiRef<rhi::SwapChainDep> swapchain, rhi::RefRtvDep rtv, rhi::RefDsvDep dsv, rhi::RefSrvDep srv, rhi::RefUavDep uav,
			rhi::EResourceState curr_state, rhi::EResourceState nesesary_end_state)
		{	
			// 無効なリソースチェック.
			if (!swapchain.IsValid() && !tex.IsValid())
			{
				std::cout << u8"[RenderTaskGraphBuilder][RegisterExternalResource] 外部リソース登録のResourceが不正です." << std::endl;
				assert(false);
			}

			// Debugでは二重登録チェック.
#			if defined(_DEBUG)
			if(tex.IsValid())
			{
				for(const auto& e : imported_resource_)
				{
					if(e.tex_.Get() == tex.Get())
					{
						// 二重登録されているのでERROR.
						std::cout << u8"[RenderTaskGraphBuilder][RegisterExternalResource] 外部リソースの二重登録が検出されました(texture). " << tex.Get() << std::endl;
						assert(false);
						return {};
					}
				}
			}
			else
			{
				for(const auto& e : imported_resource_)
				{
					if(e.swapchain_.Get() == swapchain.Get())
					{
						// 二重登録されているのでERROR.
						std::cout << u8"[RenderTaskGraphBuilder][RegisterExternalResource] 外部リソースの二重登録が検出されました(swapchain). " << swapchain.Get() << std::endl;
						assert(false);
						return {};
					}
				}
			}
#			endif
			
			// ID確保.
			const auto new_handle_id = RenderTaskGraphManager::GetNewHandleId();
			
			// ハンドルセットアップ.
			RtgResourceHandle new_handle = {};
			{
				new_handle.detail.is_external = 1;// 外部リソースマーク.
				new_handle.detail.is_swapchain = (swapchain.IsValid())? 1 : 0;// Swapchainマーク.
				new_handle.detail.unique_id = new_handle_id;
			}
			
			// ResourceのDescをHandleから引けるように登録.
			RtgResourceDesc2D res_desc = {};
			{
				if(swapchain.IsValid())
				{
					res_desc = RtgResourceDesc2D::CreateAsAbsoluteSize(swapchain->GetWidth(), swapchain->GetHeight(), swapchain->GetDesc().format);
				}
				else
				{
					res_desc = RtgResourceDesc2D::CreateAsAbsoluteSize(tex->GetWidth(), tex->GetHeight(), tex->GetDesc().format);
				}
				handle_2_desc_[new_handle] = res_desc;// desc記録.
			}

			// 外部リソース情報.
			{
				// 外部リソース用Index.
				const int res_index = (int)imported_resource_.size();
				imported_resource_.push_back({});

				// 外部リソースハンドルから外部リソース用IndexへのMap.
				imported_handle_2_index_[new_handle] = res_index;

				// 外部リソース用Indexで情報登録.
				ExternalResourceInfo& ex_res_info = imported_resource_[res_index];
				{
					ex_res_info.swapchain_ = swapchain;
					ex_res_info.tex_ = tex;
					ex_res_info.rtv_ = rtv;
					ex_res_info.dsv_ = dsv;
					ex_res_info.srv_ = srv;
					ex_res_info.uav_ = uav;
				
					ex_res_info.require_begin_state_ = curr_state;
					ex_res_info.require_end_state_ = nesesary_end_state;
				
					ex_res_info.cached_state_ = curr_state;
					ex_res_info.prev_cached_state_ = curr_state;
					ex_res_info.last_access_stage_ = TaskStage::k_frontmost_stage();
				}
			}
			
			return new_handle;
		}

		// 外部リソースの登録. 一般.
		RtgResourceHandle RenderTaskGraphBuilder::RegisterExternalResource(
			rhi::RefTextureDep tex, rhi::RefRtvDep rtv, rhi::RefDsvDep dsv, rhi::RefSrvDep srv, rhi::RefUavDep uav,
			rhi::EResourceState curr_state, rhi::EResourceState nesesary_end_state)
		{
			// Compile前のRecordフェーズでのみ許可.
			assert(IsRecordable());
			RtgResourceHandle h = RegisterExternalResourceCommon(tex, {}, rtv, dsv, srv, uav, curr_state, nesesary_end_state);
			return h;
		}
		
		// 外部リソースの登録. Swapchain.
		RtgResourceHandle RenderTaskGraphBuilder::RegisterSwapchainResource(rhi::RhiRef<rhi::SwapChainDep> swapchain, rhi::RefRtvDep rtv, rhi::EResourceState curr_state, rhi::EResourceState nesesary_end_state)
		{
			// Compile前のRecordフェーズでのみ許可.
			assert(IsRecordable());
			handle_imported_swapchain_ = RegisterExternalResourceCommon({}, swapchain, rtv, {}, {}, {}, curr_state, nesesary_end_state);
			return handle_imported_swapchain_;
		}

		// Swapchainリソースハンドルを取得. RegisterExternalResourceで登録しておく必要がある.
		RtgResourceHandle RenderTaskGraphBuilder::GetSwapchainResourceHandle() const
		{
			return handle_imported_swapchain_;
		}
		
		// Descを取得.
		RtgResourceDesc2D RenderTaskGraphBuilder::GetResourceHandleDesc(RtgResourceHandle handle) const
		{
			const auto find_it = handle_2_desc_.find(handle);
			if(handle_2_desc_.end() == find_it)
			{
				// 未登録のHandleの定義取得は不正.
				assert(false);
				return {};// 一応空を返しておく.
			}
			return find_it->second;
		}

		// Nodeからのリソースアクセスを記録.
		// NodeのRender実行順と一致する順序で登録をする必要がある. この順序によってリソースステート遷移の確定や実リソースの割当等をする.
		RtgResourceHandle RenderTaskGraphBuilder::RecordResourceAccess(const ITaskNode& node, const RtgResourceHandle res_handle, const ACCESS_TYPE access_type)
		{
			if(!IsRecordable())
			{
				std::cout <<  u8"[ERROR] このBuilderはRecordできません. すでにCompile済み, 又はExecute済みのBuilderは破棄して新規Builderを利用してください." << std::endl;
				assert(false);
				return {};
			}

			// TaskNodeのタイプによって許可されないアクセスをチェック.
			//	AsyncComputeで許可されないアクセス等を事前にエラーとする.
			{
				if(ETASK_TYPE::COMPUTE ==  node.TaskType())
				{
					// AsyncComputeでは SRVとUAVアクセスのみ許可.
					if(
						access_type != access_type::SHADER_READ
						&&
						access_type != access_type::UAV
						)
					{
						std::cout <<  u8"[ERROR] AsyncComputeTaskで許可されないアクセスタイプを検出しました. 許可されるアクセスタイプは ShaderRead と UAV のみです." << std::endl;
						assert(false);
						return {};
					}
				}
			}
				
			// Node->Handle&AccessTypeのMap登録.
			{
				if(node_handle_usage_list_.end() == node_handle_usage_list_.find(&node))
				{
					node_handle_usage_list_[&node] = {};// Node用のMap要素追加.
				}

				NodeHandleUsageInfo push_info = {};
				push_info.handle = res_handle;
				push_info.access = access_type;
				node_handle_usage_list_[&node].push_back(push_info);// NodeからHandleへのアクセス情報を記録.
			}

			// Passメンバに保持するコードを短縮するためHandleをそのままリターン.
			return res_handle;
		}


		// GraphicsTask用のRender処理登録. IGraphicsTaskNode派生Taskはこの関数で自身のRender処理を登録する.
		void RenderTaskGraphBuilder::RegisterTaskNodeRenderFunction(const IGraphicsTaskNode* node, const std::function<void(rtg::RenderTaskGraphBuilder& builder, rhi::GraphicsCommandListDep* gfx_commandlist)>& render_function)
		{
			// 念の為二重登録チェック.
			assert(node_function_graphics_.end() == node_function_graphics_.find(node));
			node_function_graphics_.insert(std::pair(node, render_function));
		}
		// AsyncComputeTask用のRender処理登録. IComputeTaskNode派生Taskはこの関数で自身の非同期Compute Render処理を登録する.
		void RenderTaskGraphBuilder::RegisterTaskNodeRenderFunction(const IComputeTaskNode* node, const std::function<void(rtg::RenderTaskGraphBuilder& builder, rhi::ComputeCommandListDep* gfx_commandlist)>& render_function)
		{
			// 念の為二重登録チェック.
			assert(node_function_compute_.end() == node_function_compute_.find(node));
			node_function_compute_.insert(std::pair(node, render_function));
		}
		
		// グラフからリソース割当と状態遷移を確定.
		// CompileされたGraphは必ずExecuteが必要.
		bool RenderTaskGraphBuilder::Compile(RenderTaskGraphManager& manager)
		{
			// Compile可能チェック.
			if(!IsCompilable())
			{
				std::cout <<  u8"[ERROR] このBuilderはCompileできません. すでにCompile済み, 又はExecute済みの可能性があります." << std::endl;
				assert(false);
				return false;
			}
			
			// 状態遷移.
			state_ = EBuilderState::COMPILED;
			// Compileでリソース割当をするマネージャを保持.
			p_compiled_manager_ = &manager;

			// Validation Check.
			{
				for(int sequence_id = 0; sequence_id < node_sequence_.size(); ++sequence_id)
				{
					ITaskNode* p_node = node_sequence_[sequence_id];
					assert(node_handle_usage_list_.end() != node_handle_usage_list_.find(p_node));// ありえない.

					// Nodeが同じHandleに対して重複したアクセスがないかチェック.
					for(int i = 0; i < node_handle_usage_list_[p_node].size() - 1; ++i)
					{
						for(int j = i + 1; j < node_handle_usage_list_[p_node].size(); ++j)
						{
							if(node_handle_usage_list_[p_node][i].handle == node_handle_usage_list_[p_node][j].handle)
							{
								std::cout << u8"[RenderTaskGraphBuilder][Validation Error] 同一リソースへの重複アクセス登録." << std::endl;
								assert(false);
							}
						}
					}
				}
			}

			// リセット.
			compiled_ = {};
			
			// ------------------------------------------------------------------------
			// Nodeの依存関係(Graphics-Compute).
			compiled_.node_dependency_fence_.resize(node_sequence_.size());
			std::fill(compiled_.node_dependency_fence_.begin(), compiled_.node_dependency_fence_.end(), CompiledBuilder::NodeDependency{});// fill -1
			{
				std::vector<ETASK_TYPE> task_type = {};
				std::vector<CompiledBuilder::NodeDependency> task_dependency = {};
				task_type.resize(node_sequence_.size());
				task_dependency.resize(node_sequence_.size());
				std::fill(task_dependency.begin(), task_dependency.end(), CompiledBuilder::NodeDependency{});// fill -1
				// NodeのTaskTypeにアクセスしやすくするための配列セットアップ.
				for(int i = 0; i < node_sequence_.size(); ++i)
				{
					task_type[i] = node_sequence_[i]->TaskType();
				}
				// 先にGraphics-Computeの依存関係をリストアップする.
				for(int i = 1; i < node_sequence_.size(); ++i)
				{
					const auto* p_node_i = node_sequence_[i];
					const auto task_type_i = p_node_i->TaskType();

					int nearest_dependency_index = -1;
					// 前段のNodeを近い順に探索.
					for(int j = i - 1; j >= 0; --j)
					{
						const auto* p_node_j = node_sequence_[j];
						const auto task_type_j = p_node_j->TaskType();

						// 異なるTypeのNodeで同一Handleへアクセスしているものを探す
						if(task_type_i == task_type_j)
							continue;
				
						for(auto& res_access_i : node_handle_usage_list_[p_node_i])
						{
							const bool cur_node_access_write = RtgIsWriteAccess(res_access_i.access);
							
							for(auto& res_access_j : node_handle_usage_list_[p_node_j])
							{
								const bool prev_node_access_write = RtgIsWriteAccess(res_access_j.access);

								// 注目ノードと同じハンドルを先行ノードが参照している.
								const bool is_same_handle = res_access_i.handle == res_access_j.handle;
								// 注目ノードと先行ノードの両方のアクセスタイプを考慮.
								const bool is_need_wait_dependency_access = !(!prev_node_access_write && !cur_node_access_write);
								
								// 現状ではGraphicsとComputeで Read-Read のアクセスでは依存は発生しないものとしている.
								//	これで問題が出る場合はアクセスタイプの組み合わせに限らず同じハンドルアクセスは依存と判断することを検討.
								if(
									is_same_handle
									&& is_need_wait_dependency_access
									)
								{
									// 最も近い前段のNodeからの依存のみで十分なので, 最大値で探索する.
									nearest_dependency_index = std::max(nearest_dependency_index, j);
									break;
								}
							}
							if(0 <= nearest_dependency_index)
								break;// 早期break.
						}
						if(0 <= nearest_dependency_index)
							break;// 早期break.
					}
					if(0 <= nearest_dependency_index)
					{
						// 近い依存のみを更新.
						// 前方からそれより前方への依存を近い順に探索しているため, node_iからみたnearest_dependency_indexは最短距離にある依存作のはず.
						if(0 > task_dependency[nearest_dependency_index].to
							|| i < task_dependency[nearest_dependency_index].to)
						{
							task_dependency[i].from = nearest_dependency_index;
							task_dependency[nearest_dependency_index].to = i;
						}
					}
				}
				// リストアップした依存関係からFenceを張るべき有効な関係を抽出する.
				//	依存元と依存先の位置関係から意味のない依存関係を除外して有効な依存関係のみ抽出.
				int fence_count = 0;
				for(int type_i = 0; type_i < 2; ++type_i)
				{
					const int other_type_i = 1 - type_i;// 0 or 1.
					
					for(int i = 0; i < node_sequence_.size(); ++i)
					{
						if((int)task_type[i] != type_i)
							continue;// 処理対象のTypeのみ.

						// 前段のNodeを探索.
						bool is_valid_dependency = true;
						for(int j = i-1; j >= 0 && is_valid_dependency; --j)
						{
							if((int)task_type[j] != type_i)
								continue;// 処理対象のTypeのみ.
							// 前方のNodeからの依存先は, 自身の依存先よりも前方であるはず. 同じか後方にあるような依存先の場合はこの依存関係iは意味が無いものとして除去する.
							if(task_dependency[i].from <= task_dependency[j].from)
							{
								is_valid_dependency = false;
							}
						}
						if(is_valid_dependency)
						{
							if(0 <= task_dependency[i].from)
							{
								compiled_.node_dependency_fence_[i].from = task_dependency[i].from;
								compiled_.node_dependency_fence_[task_dependency[i].from].to = i;

								compiled_.node_dependency_fence_[i].fence_id = fence_count;
								++fence_count;
							}
						}
					}
				}
			}
			// ------------------------------------------------------------------------
			
			
			std::vector<TaskStage> node_stage_array = {};// Sequence上のインデックスからタスクステージ情報を引く.
			{
				int step_counter = 0;
				for(int node_i = 0; node_i < node_sequence_.size(); ++node_i)
				{
					TaskStage node_stage = {};
					node_stage.step_ = step_counter;
					node_stage_array.push_back(node_stage);

					++step_counter;
				}
			}

			// 存在するハンドル毎に仮の線形インデックスを割り振る. ランダムアクセスのため.
			for(int node_i = 0; node_i < node_sequence_.size(); ++node_i)
			{
				const ITaskNode* p_node = node_sequence_[node_i];
				// このNodeのリソースアクセス情報を巡回.
				for(auto& res_access : node_handle_usage_list_[p_node])
				{
					if(compiled_.handle_2_linear_index_.end() == compiled_.handle_2_linear_index_.find(res_access.handle))
					{
						// このResourceHandleの要素が未登録なら新規.
						compiled_.handle_2_linear_index_[res_access.handle] = static_cast<int>(compiled_.linear_handle_array_.size());
						compiled_.linear_handle_array_.push_back(res_access.handle);
					}
				}
			}
			int handle_count = static_cast<int>(compiled_.linear_handle_array_.size());

			
			// SequenceのNodeを順に処理して実リソースの割当とそのステート遷移確定をしていく.
			struct ResourceHandleAccessFromNode
			{
				const ITaskNode*	p_node_ = {};
				ACCESS_TYPE			access_type = access_type::INVALID;
			};
			struct ResourceHandleAccessInfo
			{
				bool access_pattern_[access_type::_MAX] = {};
				std::vector<ResourceHandleAccessFromNode> from_node_ = {};
			};
			// リソースハンドルへの各ノードからのアクセスタイプ収集.
			std::vector<ResourceHandleAccessInfo> handle_access_info_array(handle_count);
			for(int node_i = 0; node_i < node_sequence_.size(); ++node_i)
			{
				const ITaskNode* p_node = node_sequence_[node_i];
				// このNodeのリソースアクセス情報を巡回.
				for(auto& res_access : node_handle_usage_list_[p_node])
				{
					const auto handle_index = compiled_.handle_2_linear_index_[res_access.handle];
					
					// このResourceHandleへのNodeからのアクセスを順にリストアップ.
					ResourceHandleAccessFromNode access_flow_part = {};
					access_flow_part.p_node_ = p_node;
					access_flow_part.access_type = res_access.access;

					// ノードからのアクセス情報を追加.
					handle_access_info_array[handle_index].from_node_.push_back(access_flow_part);
					// アクセスパターンタイプに追加.
					handle_access_info_array[handle_index].access_pattern_[res_access.access] = true;
				}
			}
				// Validation. ここで RenderTarget且つDepthStencilTarget等の許可されないアクセスチェック.
				for(auto handle_access : handle_access_info_array)
				{
					if(
						handle_access.access_pattern_[access_type::RENDER_TARGET]
						&&
						handle_access.access_pattern_[access_type::DEPTH_TARGET]
						)
					{
						std::cout << u8"RenderTarget と DepthStencilTarget を同時に指定することは不許可." << std::endl;
						assert(false);
						return false;
					}
				}

			
			// リソースハンドル毎のアクセス期間を計算.
			std::vector<TaskStage> handle_life_first_array(handle_count);
			std::vector<TaskStage> handle_life_last_array(handle_count);
			for(auto handle_index = 0; handle_index < handle_access_info_array.size(); ++handle_index)
			{
				TaskStage stage_first = {std::numeric_limits<int>::max()};// 最大値初期化.
				TaskStage stage_last = {std::numeric_limits<int>::min()};// 最小値初期化.

				for(const auto access_node : handle_access_info_array[handle_index].from_node_)
				{
					const int node_pos = GetNodeSequencePosition(access_node.p_node_);
					stage_first = std::min(stage_first, node_stage_array[node_pos]);// operatorオーバーロード解決.
					stage_last = std::max(stage_last, node_stage_array[node_pos]);
				}

				handle_life_first_array[handle_index] = stage_first;// このハンドルへの最初のアクセス位置
				handle_life_last_array[handle_index] = stage_last;// このハンドルへの最後のアクセス位置
			}

			// 次のフレームまで伝搬するハンドルの寿命を終端まで延長してこのハンドルのリソースがこのGraphの最後まで生存することを保証する.
			//	更に後段でManagerに対して次フレームのヒストリリソースとしてハンドルと関連付けるように指示をする.
			{
				for(auto e : propagate_next_handle_)
				{
					const int handle_index = compiled_.handle_2_linear_index_[e.first];
					// グラフ終端までアクセスがあるものとして延長.
					constexpr TaskStage stage_end = {std::numeric_limits<int>::max()};
					handle_life_last_array[handle_index] = stage_end;
				}
			}
			
			// リソースハンドル毎にPoolから実リソースを割り当てる.
			// ハンドルのアクセス期間を元に実リソースの再利用も可能.
			compiled_.linear_handle_resource_id_.resize(handle_count, CompiledBuilder::CompiledResourceInfo::k_invalid());// 無効値-1でHandle個数分初期化.

			// MEMO. handle_2_compiled_index_はunordered_mapのためイテレートに使うと順序がNodeSequence順にならずにいきなり終端の最終アクセスPassへの割当が発生して正しい再利用が働かない.
			for(int handle_index = 0; handle_index < compiled_.linear_handle_array_.size(); ++handle_index)
			{
				const RtgResourceHandle res_handle = compiled_.linear_handle_array_[handle_index];
				const auto handle_id = handle_index;

					
				// シーケンス上の順序で再利用を考慮してリソースを割り当て.
				
				const ResourceHandleAccessInfo& handle_access = handle_access_info_array[handle_id];

				if(res_handle.detail.is_external || res_handle.detail.is_swapchain)
				{
					// 外部リソースの場合.
					
					assert(imported_handle_2_index_.end() != imported_handle_2_index_.find(res_handle));// 登録済み外部リソースかチェック.

					const int ex_res_index = imported_handle_2_index_[res_handle];
					// リソースの最終アクセスステージを更新.
					{
						imported_resource_[ex_res_index].last_access_stage_ = handle_life_last_array[handle_id];
					}
					// 割当情報.
					{
						compiled_.linear_handle_resource_id_[handle_id].detail.resource_id = ex_res_index;
						compiled_.linear_handle_resource_id_[handle_id].detail.is_external = true;// 外部リソースマーク.
					}
				}
				else
				{
					// 内部リソースの場合.

					int allocated_resource_id = -1;// 無効値.

					// 前フレームから伝搬されたハンドルかチェック
					const int propagated_resource_id = p_compiled_manager_->FindPropagatedResourceId(res_handle);
					if(0 <= propagated_resource_id)
					{
						// 伝搬リソースがある場合は利用.
						// ここでSRVやUAVなどのアクセスタイプを充足するかや, 前回使用時のサイズ情報(動的解像度)を取り出して使うのがいいかもしれない.
						
						allocated_resource_id = propagated_resource_id;
					}
					else
					{
						// 伝搬リソースではない場合は通常の内部プールからの割当.

						// 問題点として, 初回フレーム等で前回フレーム自体が存在せず, 伝搬リソースが存在しない場合にどう対応すべきか.
						// FindPropagatedResourceId()が無効値を返してきて且つ, handle_2_desc_に未登録であるようなパターンになる.
						// その場合は割当失敗として処理を続けて, GetAllocatedHandleResource()が無効値を返すようにするのが良さそう. それ以降は描画Pass実装側の責任にする.

						auto find_handle_res_desc = handle_2_desc_.find(res_handle);
						if(handle_2_desc_.end() != find_handle_res_desc)
						{
							// 初回フレーム等で前回からの伝搬ができていない伝搬リソースハンドルは handle_2_desc_ に定義登録されていないため, それらはスキップして無効なリソースIDを割り当てておく.
							
							const auto require_desc = find_handle_res_desc->second;
							int concrete_w = res_base_width_;
							int concrete_h = res_base_height_;
							// MEMO ここで相対サイズモードの場合はスケールされたサイズになるが, このまま要求して新規生成された場合小さいサイズで作られて使いまわしされにくいものになる懸念が多少ある.
							require_desc.GetConcreteTextureSize(res_base_width_, res_base_height_, concrete_w, concrete_h);

							// アクセスタイプでUsageを決定. 同時指定が不可能なパターンのチェックはこれ以前に実行している予定.
							ACCESS_TYPE_MASK usage_mask = 0;
							{
								if(handle_access.access_pattern_[access_type::RENDER_TARGET])
									usage_mask |= access_type_mask::RENDER_TARGET;
								if(handle_access.access_pattern_[access_type::DEPTH_TARGET])
									usage_mask |= access_type_mask::DEPTH_TARGET;
								if(handle_access.access_pattern_[access_type::UAV])
									usage_mask |= access_type_mask::UAV;
								if(handle_access.access_pattern_[access_type::SHADER_READ])
									usage_mask |= access_type_mask::SHADER_READ;
							}
				
							ResourceSearchKey search_key = {};
							{
								search_key.format = require_desc.desc.format;
								search_key.require_width_ = concrete_w;
								search_key.require_height_ = concrete_h;
								search_key.usage_ = usage_mask;
							}

#if 1
							// リソースのアクセス範囲を考慮して再利用可能なら再利用する
							TaskStage* p_request_access_stage = &handle_life_first_array[handle_id];
#else
							// 再利用を一切しないデバッグ用.
							TaskStage* p_request_access_stage = nullptr;
#endif

							// 内部リソースプールからリソース取得. 伝搬リソースに該当するものは選択されない.
							allocated_resource_id = p_compiled_manager_->GetOrCreateResourceFromPool(search_key, p_request_access_stage);
							assert(0 <= allocated_resource_id);// 必ず有効なIDが帰るはず.
						
							// 割当決定したリソースの最終アクセスステージを更新 (このハンドルの最終アクセスステージ).
							// 伝搬リソースの場合は事前に最終端まで引き伸ばされているため更新しない.
							p_compiled_manager_->SetInternalResouceLastAccess(allocated_resource_id, handle_life_last_array[handle_id]);
						}
						else
						{
							// 初回フレーム等で前回からの伝搬ができていない伝搬リソースハンドルの可能性がある.
							// それらはリソース割当失敗として -1 のまま処理を進める.
							std::cout << u8"ハンドルへのリソース割当に失敗. 無効なHandleや伝搬指定をしていない前回フレームのハンドルの可能性があります. " << u8"(handle unique_id = " << res_handle.detail.unique_id << u8")" << std::endl;
						}
					}

					// 割当情報.
					{
						// allocated_resource_id は初回フレームの伝搬リソース等では無効値-1の可能性がある.
						compiled_.linear_handle_resource_id_[handle_id].detail.resource_id = allocated_resource_id;
						compiled_.linear_handle_resource_id_[handle_id].detail.is_external = false;
					}
				}
			}
			
			// Graph上で割り当てられた有効なリソースIDから密なリニアインデックスへのマップ. 有効リソースID毎の情報をワークバッファ上で操作するため.
			std::unordered_map<CompiledBuilder::CompiledResourceInfoKeyType, int> res_id_2_linear_map = {};
			// 有効リソースリニアインデックスからリソースIDへのマッピングをする配列.
			std::vector<CompiledBuilder::CompiledResourceInfo> res_linear_2_id_array = {};
			// Graph上で有効な実リソース数.
			int valid_res_count = 0;
			for(const auto& res_id : compiled_.linear_handle_resource_id_)
			{
				if(res_id_2_linear_map.end() == res_id_2_linear_map.find(res_id))
				{
					res_id_2_linear_map[res_id] = valid_res_count;
					res_linear_2_id_array.push_back(res_id);
					++valid_res_count;
				}
			}
			
			// 時系列順で実リソースへのアクセスNodeをリストアップ.
			std::vector<std::vector<const ITaskNode*>> res_access_node_array(valid_res_count);
			for(const auto* p_node : node_sequence_)
			{
				const auto& node_handles = node_handle_usage_list_[p_node];
				for(const auto& access_info : node_handles)
				{
					// access_info.access;
					const auto handle_id = compiled_.handle_2_linear_index_[access_info.handle];
					const auto res_id = compiled_.linear_handle_resource_id_[handle_id];

					const auto res_index = res_id_2_linear_map[res_id];

					// リソースに対して時系列でのアクセスNodeリスト.
					res_access_node_array[res_index].push_back(p_node);
				}
			}

			// リソース割当を確定したのでステート遷移を決定する.
			//	各Nodeの各Handleがその時点でどのようにステート遷移すべきかの情報を構築.
			for(int res_index = 0; res_index < res_access_node_array.size(); ++res_index)
			{
				const CompiledBuilder::CompiledResourceInfo res_id = res_linear_2_id_array[res_index];

				// 初回フレームの伝搬リソース等は無効なリソースIDとなっているためチェック.
				if(0 <= res_id.detail.resource_id)
				{
					rhi::EResourceState begin_state = {};
					if(!res_id.detail.is_external)
					{
						// 内部リソースの場合はキャッシュされたステートから開始.
						auto* p_resource = p_compiled_manager_->GetInternalResourcePtr(res_id.detail.resource_id);
						begin_state = p_resource->cached_state_;// 実リソースのCompile時点のステートから開始.
					}
					else
					{
						// 外部リソースの場合は登録された開始ステートから開始.
						begin_state = imported_resource_[res_id.detail.resource_id].cached_state_;
					}
				
					rhi::EResourceState curr_state = begin_state;
					for(const auto* p_node : res_access_node_array[res_index])
					{
						for(const auto handle : node_handle_usage_list_[p_node])
						{
							const int handle_index = compiled_.handle_2_linear_index_[handle.handle];
							if(res_id == compiled_.linear_handle_resource_id_[handle_index])
							{
								// Handleへのアクセスタイプから次のrhiステートを決定.
								rhi::EResourceState next_state = {};
								if(access_type::RENDER_TARGET == handle.access)
								{
									next_state = rhi::EResourceState::RenderTarget;
								}
								else if(access_type::DEPTH_TARGET == handle.access)
								{
									next_state = rhi::EResourceState::DepthWrite;
								}
								else if(access_type::UAV == handle.access)
								{
									next_state = rhi::EResourceState::UnorderedAccess;
								}
								else if(access_type::SHADER_READ == handle.access)
								{
									next_state = rhi::EResourceState::ShaderRead;
								}
								else
								{
									assert(false);
								}
							
								// このリソースに対してこのnode時点では cur_state -> next_state となる.
								CompiledBuilder::NodeHandleState node_handle_state = {};
								{
									node_handle_state.prev_ = curr_state;
									node_handle_state.curr_ = next_state;
								}
								// Node毎のHandle時点での前回ステートと現在ステートを確定.
								compiled_.node_handle_state_[p_node][handle.handle] = node_handle_state;
							
								// 次へ.
								curr_state = next_state;
								break;
							}
						}
					}

					// 最終ステートを保存.
					if(!res_id.detail.is_external)
					{
						auto* p_resource = p_compiled_manager_->GetInternalResourcePtr(res_id.detail.resource_id);
						// Compile前のステートを保持.
						p_resource->prev_cached_state_ = p_resource->cached_state_;
						// Compile後のステートに更新.
						p_resource->cached_state_ = curr_state;
					}
					else
					{
						// Compile前のステートを保持.
						imported_resource_[res_id.detail.resource_id].prev_cached_state_
						= imported_resource_[res_id.detail.resource_id].cached_state_;
					
						// Compile後のステートに更新.
						imported_resource_[res_id.detail.resource_id].cached_state_ = curr_state;
					}
				}
			}

			// Managerに次フレームへ伝搬するリソースを指示する.
			for(auto e : propagate_next_handle_)
			{
				const RtgResourceHandle handle(e.first);
				if(compiled_.handle_2_linear_index_.end() == compiled_.handle_2_linear_index_.find(handle))
				{
					// ありえないのでassert.
					assert(false);
					continue;
				}
				const int handle_id = compiled_.handle_2_linear_index_[handle];
				if(compiled_.linear_handle_resource_id_[handle_id].detail.is_external)
				{
					// フレーム伝搬は内部リソースのみ許可.
					assert(false);
					continue;
				}
				// Handleと割当リソースIDをマネージャにフレーム伝搬指示.
				p_compiled_manager_->PropagateResourceToNextFrame(handle, compiled_.linear_handle_resource_id_[handle_id].detail.resource_id);
			}
			
			// デバッグ表示.
			if(false)
			{
#if defined(_DEBUG)
				// 各ResourceHandleへの処理順でのアクセス情報.
				std::cout << "-Access Flow Debug" << std::endl;
				for(auto handle_index : compiled_.handle_2_linear_index_)
				{
					const auto handle = RtgResourceHandle(handle_index.first);
					const auto handle_id = handle_index.second;
					
					const auto& lifetime_first = handle_life_first_array[handle_id];
					const auto& lifetime_last = handle_life_last_array[handle_id];

					std::cout << "	-RtgResourceHandle ID " << handle << std::endl;
					std::cout << "		-FirstAccess " << static_cast<int>(lifetime_first.step_) << std::endl;
					std::cout << "		-LastAccess " << static_cast<int>(lifetime_last.step_) << std::endl;

					std::cout << "		-Resource" << std::endl;
					
					const auto res_id = compiled_.linear_handle_resource_id_[handle_id];
					if(!res_id.detail.is_external)
					{
						std::cout << "			-Internal" << std::endl;
						
						const auto res = (0 <= res_id.detail.resource_id)? p_compiled_manager_->internal_resource_pool_[res_id.detail.resource_id] : InternalResourceInstanceInfo();
						std::cout << "				-id " << res_id.detail.resource_id << std::endl;
						std::cout << "				-tex_ptr " << res.tex_.Get() << std::endl;
					}
					else
					{
						std::cout << "			-External" << std::endl;
						
						const auto res = (0 <= res_id.detail.resource_id)? imported_resource_[res_id.detail.resource_id] : ExternalResourceInfo{};
						std::cout << "				-id " << res_id.detail.resource_id << std::endl;
						if(res.tex_.IsValid())
							std::cout << "				-tex_ptr " << res.tex_.Get() << std::endl;
						else if(res.swapchain_.IsValid())
							std::cout << "				-swapchain_ptr " << res.swapchain_.Get() << std::endl;
					}
					
					for(auto res_access : handle_access_info_array[handle_id].from_node_)
					{
						const auto prev_state = compiled_.node_handle_state_[res_access.p_node_][handle].prev_;
						const auto curr_state = compiled_.node_handle_state_[res_access.p_node_][handle].curr_;
						
						std::cout << "		-Node " << res_access.p_node_->GetDebugNodeName().Get() << std::endl;
						std::cout << "			-AccessType " << static_cast<int>(res_access.access_type) << std::endl;
						// 確定したステート遷移.
						std::cout << "			-PrevState " << static_cast<int>(prev_state) << std::endl;
						std::cout << "			-CurrState " << static_cast<int>(curr_state) << std::endl;
					}
				}
#endif
			}
			
			return true;
		}

		RtgAllocatedResourceInfo RenderTaskGraphBuilder::GetAllocatedResource(const ITaskNode* node, RtgResourceHandle res_handle) const
		{	
			// Compileされていないかチェック.
			if (state_ != EBuilderState::COMPILED)
			{
				std::cout <<  u8"[ERROR] このBuilderはCompileされていません." << std::endl;
				assert(false);
				return {};
			}
			assert(nullptr != p_compiled_manager_);// Compileされていれば設定されているはず.

			// 初回フレームの伝搬リソース等はこのパターンなので無効値を返す.
			if(res_handle.IsInvalid())
			{
				// このパターンは初回フレームの伝搬リソースであり得るのでassertではなく無効値.
				return {};
			}
			if (compiled_.handle_2_linear_index_.end() == compiled_.handle_2_linear_index_.find(res_handle))
			{
				// 初回フレームの伝搬リソースであってもハンドル自体は登録されるはずなので, それがない場合はassert.　
				assert(false);// 念のためMapに登録されているかチェック.
			}
			const auto it_node_handle_state = compiled_.node_handle_state_.find(node);
			if (compiled_.node_handle_state_.end() == it_node_handle_state)
			{
				//assert(false);// 念のためMapに登録されているかチェック.
				// このパターンは初回フレームの伝搬リソースであり得るのでassertではなく無効値.
				return {};
			}
			const auto it_handle_state = it_node_handle_state->second.find(res_handle);
			if (it_node_handle_state->second.end() == it_handle_state)
			{
				//assert(false);// 念のためMapに登録されているかチェック.
				// このパターンは初回フレームの伝搬リソースであり得るのでassertではなく無効値.
				return {};
			}

			const auto it_compiled_handle_index = compiled_.handle_2_linear_index_.find(res_handle);
			assert(compiled_.handle_2_linear_index_.end() != it_compiled_handle_index);
			const int handle_id = it_compiled_handle_index->second;
			const CompiledBuilder::CompiledResourceInfo handle_res_id = compiled_.linear_handle_resource_id_[handle_id];
			if(0 > handle_res_id.detail.resource_id)
			{
				// このパターンは初回フレームの伝搬リソースであり得るのでassertではなく無効値.
				return {};
			}
			
			// ステート遷移情報取得.
			const CompiledBuilder::NodeHandleState state_transition = it_handle_state->second;
			
			// 返却情報構築.
			RtgAllocatedResourceInfo ret_info = {};
			ret_info.prev_state_ = state_transition.prev_;
			ret_info.curr_state_ = state_transition.curr_;

			if (!handle_res_id.detail.is_external)
			{
				// 内部リソースプール.
				const InternalResourceInstanceInfo& res = p_compiled_manager_->internal_resource_pool_[handle_res_id.detail.resource_id];
				ret_info.tex_ = res.tex_;
				ret_info.rtv_ = res.rtv_;
				ret_info.dsv_ = res.dsv_;
				ret_info.uav_ = res.uav_;
				ret_info.srv_ = res.srv_;
			}
			else
			{
				// 外部リソース.
				const ExternalResourceInfo& res = imported_resource_[handle_res_id.detail.resource_id];
				ret_info.swapchain_ = res.swapchain_;// 外部リソースはSwapchainの場合もある.
				ret_info.tex_ = res.tex_;
				ret_info.rtv_ = res.rtv_;
				ret_info.dsv_ = res.dsv_;
				ret_info.uav_ = res.uav_;
				ret_info.srv_ = res.srv_;
			}

			return ret_info;
		}

		void RenderTaskGraphBuilder::Execute(
			std::vector<RtgSubmitCommandSequenceElem>& out_graphics_commands, std::vector<RtgSubmitCommandSequenceElem>& out_compute_commands,
			thread::JobSystem* p_job_system)
		{
			// Compileされていないチェック.
			if(!IsExecutable())
			{
				std::cout <<  u8"[ERROR] このBuilderはExecuteできません. Record, Compileしてください." << std::endl;
				assert(false);
				return;
			}

			// Nodeの使用Resouceに有効な状態遷移が1つでも存在するかをチェック.
			auto check_exist_state_transition = [&](const ITaskNode* p_node)-> bool
			{					
				const auto& node_handle_access = node_handle_usage_list_[p_node];
				for (const auto& handle_access : node_handle_access)
				{
					RtgAllocatedResourceInfo handle_res = GetAllocatedResource(p_node, handle_access.handle);
					if (handle_res.tex_.IsValid() && (handle_res.prev_state_ != handle_res.curr_state_))
					{
						// 通常テクスチャリソースの場合.
						return true;
					}
					else if (handle_res.swapchain_.IsValid() && (handle_res.prev_state_ != handle_res.curr_state_))
					{
						// Swapchain(外部)の場合.
						return true;
					}
				}
				return false;// 有効な状態遷移が1つも存在しなければ false;
			};
			
			// 各Taskの使用リソースバリア発行.
			auto generate_barrier_command = [&](const ITaskNode* p_node, rhi::GraphicsCommandListDep* p_command_list )
			{					
				// Nodeが登録したHandleを全て列挙. Nodeのdebug_ref_handles_はデバッグ用とであることと, メンバマクロ登録されたHandleしか格納されていないため, Builderに登録されたHandle全てを列挙するにはこの方法しかない.
				const auto& node_handle_access = node_handle_usage_list_[p_node];
				for (const auto& handle_access : node_handle_access)
				{
					RtgAllocatedResourceInfo handle_res = GetAllocatedResource(p_node, handle_access.handle);
					if (handle_res.tex_.IsValid())
					{
						// 通常テクスチャリソースの場合.

						// 状態遷移コマンド発効..
						if (handle_res.prev_state_ != handle_res.curr_state_)
						{
							p_command_list->ResourceBarrier(handle_res.tex_.Get(), handle_res.prev_state_, handle_res.curr_state_);
						}
					}
					else if (handle_res.swapchain_.IsValid())
					{
						// Swapchain(外部)の場合.
						
						// 状態遷移コマンド発効..
						if (handle_res.prev_state_ != handle_res.curr_state_)
						{
							p_command_list->ResourceBarrier(handle_res.swapchain_.Get(), handle_res.swapchain_->GetCurrentBufferIndex(), handle_res.prev_state_, handle_res.curr_state_);
						}
					}
				}
			};
			// 外部リソースの最終リソースバリア発行.
			auto generate_final_barrier_for_imported_resource = [](std::vector<ExternalResourceInfo>& ref_imported_resource, rhi::GraphicsCommandListDep* p_command_list)
			{
				for(auto& ex_res : ref_imported_resource)
				{
					// CompileされたGraph内で最終的に遷移したステートが, 登録時に指定された最終ステートと異なる場合は追加で遷移コマンド.
					if(ex_res.require_end_state_ != ex_res.cached_state_)
					{
						if(ex_res.swapchain_.IsValid())
						{
							// Swapchainの場合.
							p_command_list->ResourceBarrier(ex_res.swapchain_.Get(), ex_res.swapchain_->GetCurrentBufferIndex(), ex_res.cached_state_, ex_res.require_end_state_);
						}
						else
						{
							p_command_list->ResourceBarrier(ex_res.tex_.Get(), ex_res.cached_state_, ex_res.require_end_state_);
						}
					}
				}
			};

			
			// Node毎に複数CommandList利用が可能なようにNode毎のCommandList配列. Node毎のMultiThread処理.
			std::vector<std::vector<rhi::CommandListBaseDep*>> node_commandlists = {};
			node_commandlists.resize(node_sequence_.size());
			
			// TaskのレンダリングタスクのJob実行リスト.
			std::vector< std::function<void(void)> > render_jobs{}; 
			for (const auto& e : node_sequence_)
			{
				const int node_index = GetNodeSequencePosition(e);

				if(ETASK_TYPE::GRAPHICS == e->TaskType())
				{
					// Graphics.
					
					// CommandList積み込みJob部分.
					{
						// このNode用にCommandList確保. Graphics板を取得.
						rhi::GraphicsCommandListDep* p_cmdlist = {};
						p_compiled_manager_->GetNewFrameCommandList(p_cmdlist);
						node_commandlists[node_index].push_back(p_cmdlist);// Node別CommandListArrayに登録.
						
						// CommandLList Begin. Endは別途実行.
						p_cmdlist->Begin();
						generate_barrier_command(e, p_cmdlist);
						
						auto render_func = [this, e, p_cmdlist]()
						{
							// TaskNodeはそれぞれ自身のポインタをキーとして適切なシグネチャのLambdaを登録する.
							if(auto render_func = node_function_graphics_.find(e); render_func != node_function_graphics_.end())
							{
								render_func->second(*this, p_cmdlist);// 登録されていれば実行.
							}
						};
						// JobリストにTaskのレンダリング処理を登録.
						render_jobs.push_back(render_func);
					}
				}
				else if(ETASK_TYPE::COMPUTE == e->TaskType())
				{
					// Compute.
					
					//	Compute側で必要なリソースバリアを発行するためのCommandList.
					if(check_exist_state_transition(e))
					{
						// 状態遷移コマンド発行用にGraphics板を取得.
						rhi::GraphicsCommandListDep* p_cmdlist = {};
						p_compiled_manager_->GetNewFrameCommandList(p_cmdlist);
						node_commandlists[node_index].push_back(p_cmdlist);// Node別CommandListArrayに登録.
						
						// CommandLList Begin. Endは別途実行.
						p_cmdlist->Begin();
						generate_barrier_command(e, p_cmdlist);
					}
					
					// ComputeTaskのCommandを発行する.
					{
						rhi::ComputeCommandListDep* p_cmdlist = {};
						p_compiled_manager_->GetNewFrameCommandList(p_cmdlist);
						node_commandlists[node_index].push_back(p_cmdlist);// Node別CommandListArrayに登録.
						
						// CommandLList Begin. Endは別途実行.
						p_cmdlist->Begin();
					
						auto render_func = [this, e, p_cmdlist]()
						{
							// TaskNodeはそれぞれ自身のポインタをキーとして適切なシグネチャのLambdaを登録する.
							if(auto render_func = node_function_compute_.find(e); render_func != node_function_compute_.end())
							{
								render_func->second(*this, p_cmdlist);// 登録されていれば実行.
							}
						};
						// JobリストにTaskのレンダリング処理を登録.
						render_jobs.push_back(render_func);
					}
				}
				else
				{
					assert(false);
				}
			}

			if(p_job_system)
			{
				// Parallel.
				for(auto& job : render_jobs)
				{
					p_job_system->Add([&job]{job();});
				}
				p_job_system->WaitAll();
			}
			else
			{
				for(auto& job : render_jobs)
				{
					job();
				}
			}

			// Taskが積み込みをした全CommandListをEnd.
			for(auto& per_node_list : node_commandlists)
			{
				for(auto& list : per_node_list)
				{
					list->End();
				}
			}

			// 外部リソースの必須最終ステートの解決.
			rhi::GraphicsCommandListDep* ref_cmdlist_final = {};
			p_compiled_manager_->GetNewFrameCommandList(ref_cmdlist_final);
			{
				ref_cmdlist_final->Begin();
				// 外部リソースの最終リソース解決バリア発行.
				generate_final_barrier_for_imported_resource(imported_resource_, ref_cmdlist_final);
				ref_cmdlist_final->End();
			}

			// Fence込のSubmit可能シーケンスを生成.
			out_graphics_commands.clear();
			out_compute_commands.clear();
			{	
				// Task間同期に必要なFenceをセットアップ.
				std::vector<rhi::RhiRef<rhi::FenceDep>> sync_fences;
				{
					sync_fences.clear();
					for(const auto& e : compiled_.node_dependency_fence_)
					{
						if(0 > e.fence_id)
							continue;
						
						for(;sync_fences.size() <= e.fence_id;)
						{
							sync_fences.push_back({});
						}
						if(!sync_fences[e.fence_id].IsValid())
						{
							ngl::rhi::RhiRef<ngl::rhi::FenceDep> fence = new ngl::rhi::FenceDep();
							fence->Initialize(p_compiled_manager_->p_device_);

							fence->IncrementHelperFenceValue();// Fence値を加算してWaitが即完了しないようにする.
							sync_fences[e.fence_id] = fence;
						}
					}
				}
				for(int i = 0; i < node_commandlists.size(); ++i)
				{
					const auto queue_type = node_sequence_[i]->TaskType();

					// 別のQueueを待機する.
					if(0 <= compiled_.node_dependency_fence_[i].from)
					{
						const auto fence_id = compiled_.node_dependency_fence_[i].fence_id;
						assert(0 <= fence_id);
						
						RtgSubmitCommandSequenceElem wait_elem = {};
						wait_elem.type = ERtgSubmitCommandType::Wait;
						wait_elem.fence = sync_fences[fence_id];
						wait_elem.fence_value = sync_fences[fence_id]->GetHelperFenceValue();// すでにユニーク値になっているのでそのまま使用.
						
						if(ETASK_TYPE::GRAPHICS == queue_type)
						{
							out_graphics_commands.push_back(wait_elem);
						}
						else
						{
							out_compute_commands.push_back(wait_elem);
						}
					}

					// CommandList部積み込み.
					for(auto&& list : node_commandlists[i])
					{
						RtgSubmitCommandSequenceElem command_elem = {};
						command_elem.type = ERtgSubmitCommandType::CommandList;
						command_elem.command_list = list;
							
						if(ETASK_TYPE::GRAPHICS == queue_type)
						{
							// Graphics.
							out_graphics_commands.push_back(command_elem);
						}
						else
						{
							// Compute.
							// CommandList自体のタイプがGtaphicsの場合は, ComputeのためのState遷移CommandであるためGraphicsへPushし必要なFenceも張る.
							if(list->GetD3D12GraphicsCommandList()->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT)
							{
								// Compute用のState遷移をGraphics側に発行するCommandListなのでGraphicsへPush.
								out_graphics_commands.push_back(command_elem);

								// さらにこのGraphicsComamndをComputeが待機する必要があるためFence追加.
								{
									// 現状ではFenceはその場で生成して使い捨て.
									ngl::rhi::RhiRef<ngl::rhi::FenceDep> fence = new ngl::rhi::FenceDep();
									fence->Initialize(p_compiled_manager_->p_device_);

									const u64 state_transition_fenec_value = fence->GetHelperFenceValue() + 1;//一応現在の値+1で
									{
										// GraphicsでPushしたState遷移をComputeで待たせるためのSignal.
										RtgSubmitCommandSequenceElem signal_elem = {};
										signal_elem.type = ERtgSubmitCommandType::Signal;
										signal_elem.fence = fence;
										signal_elem.fence_value = state_transition_fenec_value;

										out_graphics_commands.push_back(signal_elem);
									}
									{
										// GraphicsでPushしたState遷移をComputeが待つためのWait.
										RtgSubmitCommandSequenceElem wait_elem = {};
										wait_elem.type = ERtgSubmitCommandType::Wait;
										wait_elem.fence = fence;
										wait_elem.fence_value = state_transition_fenec_value;

										out_compute_commands.push_back(wait_elem);
									}
								}
							}
							else
							{
								// Compute TypeのCommandListはそのままPush.
								out_compute_commands.push_back(command_elem);
							}
						}
					}
					
					// 別のQueueへSignal発行する.
					if(0 <= compiled_.node_dependency_fence_[i].to)
					{
						// Fence自体はWait側が管理しているため, Signal側はtoの指すIndexのFenceを使用する.
						const auto fence_id = compiled_.node_dependency_fence_[ compiled_.node_dependency_fence_[i].to ].fence_id;
						assert(0 <= fence_id);
						
						RtgSubmitCommandSequenceElem signal_elem = {};
						signal_elem.type = ERtgSubmitCommandType::Signal;
						signal_elem.fence = sync_fences[fence_id];
						signal_elem.fence_value = sync_fences[fence_id]->GetHelperFenceValue();// すでにユニーク値になっているのでそのまま使用.
						
						if(ETASK_TYPE::GRAPHICS == queue_type)
						{
							out_graphics_commands.push_back(signal_elem);
						}
						else
						{
							out_compute_commands.push_back(signal_elem);
						}
					}
				}
			}
			
			// 最終CommandList登録.
			{
				RtgSubmitCommandSequenceElem command_elem = {};
				command_elem.type = ERtgSubmitCommandType::CommandList;
				command_elem.command_list = ref_cmdlist_final;

				out_graphics_commands.push_back(command_elem);
			}
			
			// ExecuteしたBuilderは使い捨てとすることで状態リセットの実装ミス等を回避する.
			{
				// 状態遷移.
				state_ = EBuilderState::EXECUTED;// 実行済みBuilderは再利用不可なのでステート遷移はそのまま続行.
				
				p_compiled_manager_ = nullptr;
				// 外部リソースクリア.
				{
					imported_resource_ = {};
					imported_handle_2_index_ = {};
					handle_imported_swapchain_ = {};
				}
			}
		}

		RenderTaskGraphBuilder::~RenderTaskGraphBuilder()
		{
			for (auto* p : node_sequence_)
			{
				if (p)
				{
					delete p;
					p = nullptr;
				}
			}
			node_sequence_.clear();

			node_function_graphics_.clear();
			node_function_compute_.clear();
		}

		// Sequence上でのノードの位置を返す.
		// シンプルに直列なリスト上での位置.
		int RenderTaskGraphBuilder::GetNodeSequencePosition(const ITaskNode* p_node) const
		{
			int i = 0;
			auto find_pos = std::find(node_sequence_.begin(), node_sequence_.end(), p_node);
			
			if (node_sequence_.end() == find_pos)
				return -1;
			return static_cast<int>(std::distance(node_sequence_.begin(), find_pos));
		}

		// Builderの状態取得用.
		bool RenderTaskGraphBuilder::IsRecordable() const
		{
			return EBuilderState::RECORDING == state_;
		}
		bool RenderTaskGraphBuilder::IsCompilable() const
		{
			return EBuilderState::RECORDING == state_;// 実質Recordableと同等.
		}
		bool RenderTaskGraphBuilder::IsExecutable() const
		{
			return EBuilderState::COMPILED == state_;
		}
		
		// RtgのExecute() で構築して生成したComandListのSequenceをGPUへSubmitするヘルパー関数.
		void RenderTaskGraphBuilder::SubmitCommand(
			rhi::GraphicsCommandQueueDep& graphics_queue, rhi::ComputeCommandQueueDep& compute_queue,
			std::vector<RtgSubmitCommandSequenceElem>& graphics_commands, std::vector<RtgSubmitCommandSequenceElem>& compute_commands)
		{
			//	連続したCommandListをなるべく一度のExecuteにまとめ, 必要な箇所でFenceを張る.
			auto submit_sequence = [](std::vector<ngl::rtg::RtgSubmitCommandSequenceElem>& sequence, ngl::rhi::CommandQueueBaseDep& graphics_queue)
			{
				std::vector<ngl::rhi::CommandListBaseDep*> exec_command_list_reservoir = {};
				for(auto&& e : sequence)
				{
					if(ngl::rtg::ERtgSubmitCommandType::CommandList == e.type)
					{
						exec_command_list_reservoir.push_back(e.command_list);
					}
					else
					{
						// Fenceが現れたらそれまでのCommandListをまとめてSubmit.
						if(0u < exec_command_list_reservoir.size())
						{
							graphics_queue.ExecuteCommandLists(static_cast<unsigned int>(exec_command_list_reservoir.size()), exec_command_list_reservoir.data());
							exec_command_list_reservoir.clear();
						}
						assert(e.fence.IsValid());
						
						if(ngl::rtg::ERtgSubmitCommandType::Signal == e.type)
						{
							graphics_queue.Signal(e.fence.Get(), e.fence_value);
						}
						else if(ngl::rtg::ERtgSubmitCommandType::Wait == e.type)
						{
							graphics_queue.Wait(e.fence.Get(), e.fence_value);
						}
						else
						{
							assert(false);// ありえない.
						}
					}
				}
				// 残ったCommandListをSubmit.
				if(0u < exec_command_list_reservoir.size())
				{
					graphics_queue.ExecuteCommandLists(static_cast<unsigned int>(exec_command_list_reservoir.size()), exec_command_list_reservoir.data());
					exec_command_list_reservoir.clear();
				}
			};
			
			// QueueにSubmit.
			submit_sequence(graphics_commands, graphics_queue);
			submit_sequence(compute_commands, compute_queue);
		}
		// --------------------------------------------------------------------------------------------------------------------
		uint32_t RenderTaskGraphManager::s_res_handle_id_counter_ = 0;

		// 破棄.
		RenderTaskGraphManager::~RenderTaskGraphManager()
		{
			int valid_resource_count = 0;
			for(auto&& e : internal_resource_pool_)
			{
				if(e.IsValid())
				{
					++valid_resource_count;
				}
			}
			// 終了時にPoolのサイズと有効なリソース数のデバッグ表示.
			std::cout << u8"<RenderTaskGraphManager>" << std::endl;
			std::cout << u8"	valid internal resource count / resource pool size = " << valid_resource_count << u8" / " << internal_resource_pool_.size() << std::endl;
			std::cout << u8"</RenderTaskGraphManager>" << std::endl;
		}
		// 初期化.
		bool RenderTaskGraphManager::Init(rhi::DeviceDep* p_device, int job_thread_count)
		{
			if(!p_device)
			{
				assert(p_device);
				return false;
			}
			
			p_device_ = p_device;

			// CommandListPool初期化.
			commandlist_pool_.Initialize(p_device);

			// 内部用JobSystem.
			assert(0 < job_thread_count);
			job_system_.Init(job_thread_count);
			
			return true;
		}
		
		//	フレーム開始通知. Game-Render同期中に呼び出す.
		//		内部リソースプールの中で一定フレームアクセスされていないものを破棄するなどの処理.
		void RenderTaskGraphManager::BeginFrame()
		{
			// フレーム開始でフレーム伝搬リソースのMapフリップとクリア.
			{
				flip_propagate_next_handle_next_ = 1 - flip_propagate_next_handle_next_;// このフレームから伝搬するハンドル用.
				propagate_next_handle_[flip_propagate_next_handle_next_].clear();

				// フレーム内の後段で前段rtgの伝搬リソースを参照するためのテンポラルMapをクリア.
				propagate_next_handle_temporal_.clear();
			}

			// 未使用リソースの破棄.
			{
				// 破棄する未使用フレーム数. 1以上. 数フレームは猶予を持たせたほうが良い場合もある.
				constexpr int unused_internal_resource_delete_frame = 1;
				for(auto& e : internal_resource_pool_)
				{
					if(!e.IsValid())
						continue;

					// カウンタ加算.
					++e.unused_frame_counter_;

					// 使用されずに一定フレーム経過したリソースは破棄.
					if(unused_internal_resource_delete_frame < e.unused_frame_counter_)
					{
						// 参照カウンタをクリアして解放.
						e = {};
					}
				}
			}

			// CommandListPoolの更新.
			{
				commandlist_pool_.BeginFrame();
			}
		}
		
		// タスクグラフを構築したbuilderをCompileしてリソース割当を確定する.
		// Compileしたbuilderは必ずExecuteする必要がある.
		// また, 複数のbuilderをCompileした場合はCompileした順序でExecuteが必要(確定したリソースの状態遷移コマンド実行を正しい順序で実行するために).
		bool RenderTaskGraphManager::Compile(RenderTaskGraphBuilder& builder)
		{
			assert(nullptr != p_device_);
			// Compile可能チェック.
			if(!builder.IsCompilable())
			{
				std::cout <<  u8"[ERROR] このBuilderはCompileできません. すでにCompile済み, 又はExecute済みの可能性があります." << std::endl;
				assert(false);
				return false;
			}
			
			// Compileは排他処理.
			std::scoped_lock<std::mutex> lock(compile_mutex_);
			
			// Compile前に内部リソースプールの情報をクリア,更新する.
			// 通常のリソースはCompileで割当が可能なように最終アクセスステージをリセット.
			// 前回フレームからの伝搬リソースについては、 GetOrCreateResourceFromPool() で割当られないように最終アクセスステージを修正する.
			//	Stateは継続するので維持.
			for(auto& e : internal_resource_pool_)
			{
				if(!e.IsValid())
					continue;
				e.last_access_stage_ = TaskStage::k_frontmost_stage();// 次のCompileのために最終アクセスを負の最大にリセット.
			}
			// 伝搬リソースの最終アクセスステージを最終端に書き換えて通常の割当から除外.
			// 
			{
				const int flip_index_propagated = 1 - flip_propagate_next_handle_next_;
				for(auto e : propagate_next_handle_[flip_index_propagated])
				{
					assert(internal_resource_pool_[e.second].IsValid());// 伝搬しているなら存在するはず.
					internal_resource_pool_[e.second].last_access_stage_ = TaskStage::k_endmost_stage();// 最終端.
				}
				// このフレームですでにCompileされているrtgで伝搬されたリソースをそのフレームの後段rtgで利用できるようにする.
				for(auto e : propagate_next_handle_temporal_)
				{
					assert(internal_resource_pool_[e.second].IsValid());// 伝搬しているなら存在するはず.
					internal_resource_pool_[e.second].last_access_stage_ = TaskStage::k_endmost_stage();// 最終端.
				}
			}
			
			// Compile実行.
			const bool result = builder.Compile(*this);

			// Compileで割り当てられた内部リソースの未使用カウンタをリセット. 外部リソースは無視すること.
			{
				for(auto& e : builder.compiled_.linear_handle_resource_id_)
				{
					if(!e.detail.is_external)
					{
						auto* p_resource = GetInternalResourcePtr(e.detail.resource_id);
						assert(p_resource);
						p_resource->unused_frame_counter_ = 0;// リセット.
					}
				}
			}
			return result;
		}
		
		uint32_t RenderTaskGraphManager::GetNewHandleId()
		{
			++s_res_handle_id_counter_;
			if (0 == s_res_handle_id_counter_)
			{
				// IDが一周したことを一応チェックする.
				std::cout << u8"[RenderTaskGraphManager] HandleIDが一周." << std::endl;
				
				++s_res_handle_id_counter_;// 0は無効ID扱いのためスキップ.
			}
			
			return s_res_handle_id_counter_;
		}
		// Poolからリソース検索または新規生成. この関数はCompileから呼ばれるため排他.
		int RenderTaskGraphManager::GetOrCreateResourceFromPool(ResourceSearchKey key, const TaskStage* p_access_stage_for_reuse)
		{
			//	アクセスステージが引数で渡されなかった場合は未割り当てリソース以外を割り当てないようにTaskStage::k_frontmost_stage(負の最大)扱いとする.
			const TaskStage require_access_stage = (p_access_stage_for_reuse)? ((*p_access_stage_for_reuse)) : TaskStage::k_frontmost_stage();

			assert(nullptr != p_device_);
			
			// keyで既存リソースから検索または新規生成.
					
			// poolから検索.
			int res_id = -1;
			int empty_index = -1;
			for(int i = 0; i < internal_resource_pool_.size(); ++i)
			{
				// 内部リソースが未登録スロットはスキップ.
				if(!internal_resource_pool_[i].IsValid())
				{
					// 後段で生成した新規リソースの登録先として空のインデックスを保持.
					if(0 > empty_index)
						empty_index = i;
					
					continue;
				}
				
				// 要求アクセスステージに対してアクセス期間が終わっていなければ再利用不可能.
				//	MEMO. 新規生成した実リソースの last_access_stage_ を負のstageで初期化しておくこと.
				if(internal_resource_pool_[i].last_access_stage_ >= require_access_stage)
					continue;
				
				// フォーマットチェック.
				if(internal_resource_pool_[i].tex_->GetFormat() != key.format)
					continue;
				// 要求サイズを格納できるならOK.
				if(internal_resource_pool_[i].tex_->GetWidth() < static_cast<uint32_t>(key.require_width_))
					continue;
				// 要求サイズを格納できるならOK.
				if(internal_resource_pool_[i].tex_->GetHeight() < static_cast<uint32_t>(key.require_height_))
					continue;
				// RTV要求している場合.
				if(key.usage_ & access_type_mask::RENDER_TARGET)
				{
					if(!internal_resource_pool_[i].rtv_.IsValid())
						continue;
				}
				// DSV要求している場合.
				if(key.usage_ & access_type_mask::DEPTH_TARGET)
				{
					if(!internal_resource_pool_[i].dsv_.IsValid())
						continue;
				}
				// UAV要求している場合.
				if(key.usage_ & access_type_mask::UAV)
				{
					if(!internal_resource_pool_[i].uav_.IsValid())
						continue;
				}
				// SRV要求している場合.
				if(key.usage_ & access_type_mask::SHADER_READ)
				{
					if(!internal_resource_pool_[i].srv_.IsValid())
						continue;
				}

				// 再利用可能なリソース発見.
				res_id = i;
				break;
			}

			// 新規生成.
			if(0 > res_id)
			{
				rhi::TextureDep::Desc desc = {};
				rhi::EResourceState init_state = rhi::EResourceState::General;
				{
					desc.type = rhi::ETextureType::Texture2D;// 現状2D固定.
					desc.initial_state = init_state;
					desc.array_size = 1;
					desc.mip_count = 1;
					desc.sample_count = 1;
					desc.heap_type = rhi::EResourceHeapType::Default;
						
					desc.format = key.format;
					desc.width = key.require_width_;	// MEMO 相対サイズの場合はここには縮小サイズ等が来てしまうので無駄がありそう.
					desc.height = key.require_height_;
					desc.depth = 1;
							
					desc.bind_flag = 0;
					{
						if(key.usage_ & access_type_mask::RENDER_TARGET)
							desc.bind_flag |= rhi::ResourceBindFlag::RenderTarget;
						if(key.usage_ & access_type_mask::DEPTH_TARGET)
							desc.bind_flag |= rhi::ResourceBindFlag::DepthStencil;
						if(key.usage_ & access_type_mask::UAV)
							desc.bind_flag |= rhi::ResourceBindFlag::UnorderedAccess;
						if(key.usage_ & access_type_mask::SHADER_READ)
							desc.bind_flag |= rhi::ResourceBindFlag::ShaderResource;
					}
				}
				
				rhi::RefTextureDep new_tex = {};
				rhi::RefRtvDep new_rtv = {};
				rhi::RefDsvDep new_dsv = {};
				rhi::RefUavDep new_uav = {};
				rhi::RefSrvDep new_srv = {};

				// Texture.
				new_tex = new rhi::TextureDep();
				if (!new_tex->Initialize(p_device_, desc))
				{
					assert(false);
					return -1;
				}
				// Rtv.
				if(key.usage_ & access_type_mask::RENDER_TARGET)
				{
					new_rtv = new rhi::RenderTargetViewDep();
					if (!new_rtv->Initialize(p_device_, new_tex.Get(), 0, 0, 1))
					{
						assert(false);
						return -1;
					}
				}
				// Dsv.
				if(key.usage_ & access_type_mask::DEPTH_TARGET)
				{
					new_dsv = new rhi::DepthStencilViewDep();
					if (!new_dsv->Initialize(p_device_, new_tex.Get(), 0, 0, 1))
					{
						assert(false);
						return -1;
					}
				}
				// Uav.
				if(key.usage_ & access_type_mask::UAV)
				{
					new_uav = new rhi::UnorderedAccessViewDep();
					if (!new_uav->Initialize(p_device_, new_tex.Get(), 0, 0, 1))
					{
						assert(false);
						return -1;
					}
				}
				// Srv.
				if(key.usage_ & access_type_mask::SHADER_READ)
				{
					new_srv = new rhi::ShaderResourceViewDep();
					if (!new_srv->InitializeAsTexture(p_device_, new_tex.Get(), 0, 1, 0, 1))
					{
						assert(false);
						return -1;
					}
				}

				InternalResourceInstanceInfo new_pool_elem = {};
				{
					// 新規生成した実リソースは最終アクセスステージを負の最大にしておく(ステージ0のリクエストに割当できるように).
					new_pool_elem.last_access_stage_ = TaskStage::k_frontmost_stage();
					
					new_pool_elem.tex_ = new_tex;
					new_pool_elem.rtv_ = new_rtv;
					new_pool_elem.dsv_ = new_dsv;
					new_pool_elem.uav_ = new_uav;
					new_pool_elem.srv_ = new_srv;
						
					new_pool_elem.cached_state_ = init_state;// 新規生成したらその初期ステートを保持.
					new_pool_elem.prev_cached_state_ = init_state;
				}
				
				if(0 <= empty_index)
				{
					res_id = empty_index;// 空きインデックスがあるためそこを利用.
				}
				else
				{
					res_id = static_cast<int>(internal_resource_pool_.size());// 新規要素ID.
					internal_resource_pool_.push_back({});// 要素増加.
				}
				// 登録.
				internal_resource_pool_[res_id] = new_pool_elem;
			}

			// チェック
			if(p_access_stage_for_reuse)
			{
				// アクセス期間による再利用を有効にしている場合は, 最終アクセスステージリソースは必ず引数のアクセスステージよりも前のもののはず.
				assert(internal_resource_pool_[res_id].last_access_stage_ < (*p_access_stage_for_reuse));
			}

			return res_id;
		}
		void RenderTaskGraphManager::SetInternalResouceLastAccess(int resource_id, TaskStage last_access_stage)
		{
			if(0 > resource_id || internal_resource_pool_.size() <= resource_id)
			{
				// 範囲外.
				assert(false);
				return;
			}
			if(!internal_resource_pool_[resource_id].IsValid())
			{
				// 空きスロットに対する操作はロジックミス.
				assert(false);
				return;
			}	
			// 更新.
			internal_resource_pool_[resource_id].last_access_stage_ = last_access_stage;
		}
		InternalResourceInstanceInfo* RenderTaskGraphManager::GetInternalResourcePtr(int resource_id)
		{
			if(0 > resource_id || internal_resource_pool_.size() <= resource_id)
			{
				return nullptr;
			}
			// 更新.
			return &internal_resource_pool_[resource_id];
		}
		
		void RenderTaskGraphManager::PropagateResourceToNextFrame(RtgResourceHandle handle, int resource_id)
		{
			const int flip_index_next = flip_propagate_next_handle_next_;
			propagate_next_handle_[flip_index_next][handle] = resource_id;

			// このフレームの後段rtgで伝搬リソースとしてアクセスするためにテンポラルなMapにも登録. このMapは二重化せずフレーム最初にクリアされる.
			propagate_next_handle_temporal_[handle] = resource_id;
		}
		int RenderTaskGraphManager::FindPropagatedResourceId(RtgResourceHandle handle)
		{
			// 前フレームから伝搬されたリソース探索.
			{
				const int flip_index_propagated = 1 - flip_propagate_next_handle_next_;
				const auto find_it = propagate_next_handle_[flip_index_propagated].find(handle);
				if(propagate_next_handle_[flip_index_propagated].end() != find_it)
				{
					// 前フレームで伝搬指定されたものが発見できた.
					return find_it->second;
				}
			}

			// このフレームでの前段で登録されている可能性のある伝搬リソースも後段で利用できるように探索.
			{
				const auto find_it = propagate_next_handle_temporal_.find(handle);
				if(propagate_next_handle_temporal_.end() != find_it)
				{
					// 前フレームで伝搬指定されたものが発見できた.
					return find_it->second;
				}
			}
			
			// 未登録のため無効値.
			return -1;
		}
		
	}
}
