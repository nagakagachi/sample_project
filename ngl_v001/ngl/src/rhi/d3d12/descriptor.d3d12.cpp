

#include "rhi/d3d12/descriptor.d3d12.h"

#include <array>
#include <algorithm>
#include <limits>

#include "util/bit_operation.h"

#include "rhi/d3d12/device.d3d12.h"

namespace ngl
{
	namespace rhi
	{
		
		#if NGL_DEBUG_DESCRIPTOR_SET_OPTIMIZATION
			PersistentDescriptorInfo DefaultPersistentDescriptorInfoHolder::global_default_persistent_descriptor_cbvsrvuav = {};
			PersistentDescriptorInfo DefaultPersistentDescriptorInfoHolder::global_default_persistent_descriptor_sampler = {};
		#endif

		void DefaultPersistentDescriptorInfoHolder::SetGlobalDefaultPersistentDescriptor_CbvSrvUav(const PersistentDescriptorInfo& v)
		{
			#if NGL_DEBUG_DESCRIPTOR_SET_OPTIMIZATION
				// この設定は一度しか呼ばれない想定であるため, すでに有効なDefault Descriptorが設定されている場合はエラー.
				assert(!global_default_persistent_descriptor_cbvsrvuav.IsValid());
				global_default_persistent_descriptor_cbvsrvuav = v;
			#else
				//
			#endif
		}
		void DefaultPersistentDescriptorInfoHolder::SetGlobalDefaultPersistentDescriptor_Sampler(const PersistentDescriptorInfo& v)
		{
			#if NGL_DEBUG_DESCRIPTOR_SET_OPTIMIZATION
				// この設定は一度しか呼ばれない想定であるため, すでに有効なDefault Descriptorが設定されている場合はエラー.
				assert(!global_default_persistent_descriptor_sampler.IsValid());
				global_default_persistent_descriptor_sampler = v;
			#else
				//
			#endif
		}
		const PersistentDescriptorInfo& DefaultPersistentDescriptorInfoHolder::GetGlobalDefaultPersistentDescriptor_CbvSrvUav()
		{
			#if NGL_DEBUG_DESCRIPTOR_SET_OPTIMIZATION
				// 取得する時点では必ずDefault Descriptorが設定されているはず.
				// もし設定されていない場合はエラー.
				// DeviceDepなどが保持するPersistentDescriptorAllocatorの初期化と同時に設定することを想定.
				assert(global_default_persistent_descriptor_cbvsrvuav.IsValid());
				return global_default_persistent_descriptor_cbvsrvuav;
			#else
				// この場合は呼ばれるはずがない.
				assert(false);
				static PersistentDescriptorInfo dummy = {};
				return dummy;
			#endif
		}
		const PersistentDescriptorInfo& DefaultPersistentDescriptorInfoHolder::GetGlobalDefaultPersistentDescriptor_Sampler()
		{
			#if NGL_DEBUG_DESCRIPTOR_SET_OPTIMIZATION
				// 取得する時点では必ずDefault Descriptorが設定されているはず.
				// もし設定されていない場合はエラー.
				// DeviceDepなどが保持するPersistentDescriptorAllocatorの初期化と同時に設定することを想定.
				assert(global_default_persistent_descriptor_sampler.IsValid());
				return global_default_persistent_descriptor_sampler;
			#else
				// この場合は呼ばれるはずがない.
				assert(false);
				static PersistentDescriptorInfo dummy = {};
				return dummy;
			#endif
		}


		namespace dynamic_descriptor_allocator
		{
			// 実装部.
			class RangeAllocatorImpl
			{
			public:
				RangeAllocatorImpl();
				~RangeAllocatorImpl();

				bool Initialize(int max_size);
				void Finalize();

				// 確保.
				RangeHandle Alloc(int size);
				// 解放.
				void Dealloc(const RangeHandle& handle);

				struct FreeRangeNode
				{
					FreeRangeNode* prev = nullptr;
					FreeRangeNode* next = nullptr;
					int pos = 0;
					int size = 0;

					void ResetLink()
					{
						prev = nullptr;
						next = nullptr;
					}

					void RemoveFromList()
					{
						if (nullptr != prev)
						{
							prev->next = next;
						}
						if (nullptr != next)
						{
							next->prev = prev;
						}
						ResetLink();
					}
				};

				void PushFrontToList(FreeRangeNode*& list_head, FreeRangeNode* node)
				{
					// ノードプールの先頭に挿入.
					if (nullptr == list_head)
					{
						list_head = node;
					}
					else
					{
						list_head->prev = node;
						node->next = list_head;
						list_head = node;
					}
				}

				int max_size_ = 0;
				FreeRangeNode* free_head_ = nullptr;
				FreeRangeNode* node_pool_head_ = nullptr;
			};


			RangeAllocatorImpl::RangeAllocatorImpl()
			{
			}
			RangeAllocatorImpl::~RangeAllocatorImpl()
			{
				Finalize();
			}
			bool RangeAllocatorImpl::Initialize(int max_size)
			{
				max_size_ = max_size;

				auto* node = new FreeRangeNode();
				node->pos = 0;
				node->size = max_size;
				node->prev = nullptr;
				node->next = nullptr;
				free_head_ = node;

				return true;
			}
			void RangeAllocatorImpl::Finalize()
			{
				{
					auto* p = node_pool_head_;
					node_pool_head_ = nullptr;
					for (; p != nullptr;)
					{
						auto p_del = p;
						p = p->next;
						delete p_del;
					}
				}
				{
					auto* p = free_head_;
					free_head_ = nullptr;
					for (; p != nullptr;)
					{
						auto p_del = p;
						p = p->next;
						delete p_del;
					}
				}
			}
			// Thread Unsafe.
			RangeHandle RangeAllocatorImpl::Alloc(int size)
			{
				// 線形探索.
				// TLSF的に対応しても良いかも.
				auto* node = free_head_;
				for (; node != nullptr && size > node->size; node = node->next)
				{
				}

				// 枯渇.
				if (nullptr == node)
				{
					// TODO.
					// ここで断片化が原因の可能性があるため矯正マージをして再検索も検討する.

					return {};
				}

				int pos = node->pos;
				// レンジの切り出しでnodeの情報を修正.
				node->pos += size;
				node->size -= size;

				// 空になったら除去.
				if (0 >= node->size)
				{
					if (nullptr != free_head_ && free_head_ == node)
						free_head_ = node->next;
					node->RemoveFromList();

					PushFrontToList(node_pool_head_, node);
				}

				// ハンドル返却.
				RangeHandle handle = {};
				handle.detail.head = pos;
				handle.detail.size = size;
				return handle;
			}
			// Thread Unsafe.
			// 解放時に線形探索.
			void RangeAllocatorImpl::Dealloc(const RangeHandle& handle)
			{
				assert(handle.IsValid());
				if (!handle.IsValid())
					return;

				// 登録用のnode.
				if (nullptr == node_pool_head_)
				{
					// なければ追加.
					node_pool_head_ = new FreeRangeNode();
				}

				auto free_node = node_pool_head_;
				// poolから取得.
				node_pool_head_ = node_pool_head_->next;
				free_node->ResetLink();

				// 情報.
				free_node->pos = handle.detail.head;
				free_node->size = handle.detail.size;


				// 線形探索でposによる挿入位置決定.
				auto* node = free_head_;
				auto* prev = (nullptr != node) ? node->prev : nullptr;


				// フリーリストへの挿入位置を線形探索.
				for (; nullptr != node;)
				{
					// 解放要素
					if (node->pos > free_node->pos)
						break;

					prev = node;
					node = node->next;
				}
				if (nullptr == prev)
				{
					// 先頭に挿入.
					PushFrontToList(free_head_, free_node);
				}
				else
				{
					// 発見した位置に挿入.
					free_node->next = prev->next;
					free_node->prev = prev;
					if (prev->next)
					{
						prev->next->prev = free_node;
					}
					prev->next = free_node;
				}

				// マージ.
				{
					// 一つ後とのマージ.
					if (nullptr != free_node->next)
					{
						if (free_node->pos + free_node->size == free_node->next->pos)
						{
							// サイズマージ.
							free_node->size += free_node->next->size;

							// 次要素をリストから除去.
							auto next = free_node->next;

							if (nullptr != free_head_ && free_head_ == next)
								free_head_ = next->next;
							next->RemoveFromList();

							// プールに返却.
							PushFrontToList(node_pool_head_, next);
						}
					}
					// 一つ前とのマージ. free_nodeの指すポインタは上で後ろとマージされても変わらないため問題なし.
					if (nullptr != free_node->prev)
					{
						if (free_node->prev->pos + free_node->prev->size == free_node->pos)
						{
							// サイズマージ.
							free_node->prev->size += free_node->size;

							// 自身をリストから除去.
							if (nullptr != free_head_ && free_head_ == free_node)
								free_head_ = free_node->next;
							free_node->RemoveFromList();

							// プールに返却.
							PushFrontToList(node_pool_head_, free_node);
						}
					}
				}

			}



			// -----------------------------------------------------------------------
			// DynamicDescriptorの管理用.
			//	基本用途としては大きなサイズを切り出して利用するためアロケーション頻度は低くサイズ粒度も大きい前提.
			// -----------------------------------------------------------------------
			RangeAllocator::RangeAllocator()
			{
				impl_ = new RangeAllocatorImpl();
			}
			RangeAllocator::~RangeAllocator()
			{
				delete impl_;
				impl_ = nullptr;
			}
			bool RangeAllocator::Initialize(uint32_t max_size)
			{
				return impl_->Initialize(max_size);
			}
			void RangeAllocator::Finalize()
			{
				impl_->Finalize();
			}
			// 確保.
			RangeHandle RangeAllocator::Alloc(uint32_t size)
			{
				return impl_->Alloc(size);
			}
			// 解放.
			void RangeAllocator::Dealloc(const RangeHandle& handle)
			{
				impl_->Dealloc(handle);
			}
			uint32_t RangeAllocator::MaxSize() const
			{
				return impl_->max_size_;
			}
			// -----------------------------------------------------------------------
			// -----------------------------------------------------------------------
		}










		DescriptorHeapWrapper::DescriptorHeapWrapper()
		{
		}
		DescriptorHeapWrapper::~DescriptorHeapWrapper()
		{
			Finalize();
		}
		bool DescriptorHeapWrapper::Initialize(DeviceDep* p_device, const Desc& desc)
		{
			assert(p_device);
			assert(0 < desc.allocate_descriptor_count_);
			if (!p_device || 0 >= desc.allocate_descriptor_count_)
			{
				return false;
			}

			desc_ = desc;
			// Heap作成
			{
				heap_desc_ = {};
				heap_desc_.Type = desc_.type;
				heap_desc_.NumDescriptors = desc_.allocate_descriptor_count_;
				heap_desc_.NodeMask = 0;
				// SHaderからの可視性.
				//https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-copydescriptors
				heap_desc_.Flags = (desc.shader_visible)? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

				if (FAILED(p_device->GetD3D12Device()->CreateDescriptorHeap(&heap_desc_, IID_PPV_ARGS(&p_heap_))))
				{
					std::cout << "[ERROR] Create DescriptorHeap" << std::endl;
					return false;
				}

				handle_increment_size_ = p_device->GetD3D12Device()->GetDescriptorHandleIncrementSize(heap_desc_.Type);
				cpu_handle_start_ = p_heap_->GetCPUDescriptorHandleForHeapStart();
				if (desc.shader_visible)
				{
					gpu_handle_start_ = p_heap_->GetGPUDescriptorHandleForHeapStart();// D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE指定されていないHeapでGetGPUDescriptorHandleForHeapStart呼び出しはValidationErrorとなる.
				}
			}
			return true;
		}
		void DescriptorHeapWrapper::Finalize()
		{
			// ComPtrで解放
			p_heap_ = nullptr;
		}


		PersistentDescriptorAllocator::PersistentDescriptorAllocator()
		{
		}
		PersistentDescriptorAllocator::~PersistentDescriptorAllocator()
		{
			Finalize();
		}

		bool PersistentDescriptorAllocator::Initialize(DeviceDep* p_device, const Desc& desc)
		{
			assert(p_device);
			assert(0 < desc.allocate_descriptor_count_);

			desc_ = desc;
			last_allocate_index_ = 0;
			num_allocated_ = 0;
			tail_fraction_bit_mask_ = 0;

			// Heap作成
			{
				// このHeap上のDescriptorは直接描画に利用しないためNone指定によってシェーダから不可視(InVisible)とする.
				// CopyDescriptorsのコピー元はシェーダから不可視とする必要があるためD3D12_DESCRIPTOR_HEAP_FLAG_NONE.
				// シェーダから可視なヒープはCPUからのアクセスが低速でパフォーマンスに影響があるため.
				// 
				//https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-copydescriptors
				DescriptorHeapWrapper::Desc heap_desc = {};
				heap_desc.allocate_descriptor_count_ = desc.allocate_descriptor_count_;
				heap_desc.type = desc.type;
				heap_desc.shader_visible = false;

				if (!heap_wrapper_.Initialize(p_device, heap_desc))
				{
					return false;
				}
			}

			{
				// 管理用情報構築
				num_use_flag_elem_ = (desc_.allocate_descriptor_count_ + (k_num_flag_elem_bit_ - 1)) / k_num_flag_elem_bit_;
				use_flag_bit_array_.resize(num_use_flag_elem_);
				
				// リセット

				// NOTE:
				//	使用ビットフラグの総ビット数は実際の確保可能サイズよりも大きいため、末尾チェックを簡略化するために事前に末尾の使用不可ビットは1にしておく.
				//	ランタイムでこの部分を0にすると不正な位置にAllocationできると判断されてしまうため上書きしないように！
				// 一旦すべて0クリアした後に端数部を1で埋める.
				std::fill_n(use_flag_bit_array_.data(), num_use_flag_elem_, 0u);
				// 端数
				if (0 < desc_.allocate_descriptor_count_)
				{
					const u32 fraction = desc_.allocate_descriptor_count_ - k_num_flag_elem_bit_ * (num_use_flag_elem_ - 1);
					if (0 < fraction && fraction < k_num_flag_elem_bit_)
					{
						const u32 lower_mask = (1u << fraction) - 1;
						tail_fraction_bit_mask_ = ~lower_mask;

						// 末尾要素の端数部が使用されないようにマスクを書き込み. ここの部分が操作中にゼロになることは発生し得ない. 発生した場合はバグ.
						use_flag_bit_array_[num_use_flag_elem_ - 1] = tail_fraction_bit_mask_;
					}
				}
			}

			// デフォルト利用のためのDescriptorを一つ作成しておく.
			default_persistent_descriptor_ = Allocate();

			return true;
		}
		void PersistentDescriptorAllocator::Finalize()
		{
			Deallocate(default_persistent_descriptor_);

			heap_wrapper_.Finalize();
		}

		/*
			確保
		*/
		PersistentDescriptorInfo PersistentDescriptorAllocator::Allocate()
		{
			PersistentDescriptorInfo ret = {};

			constexpr u32 full_mask = ~u32(0);
			u32 find = full_mask;


			// ロック
			std::lock_guard<std::mutex> lock(mutex_);

			// ---------------------------------------------------------------------------------------------------------------------------------
			// 念の為端数部のマスクが書き換わっていないかチェック
			assert(tail_fraction_bit_mask_ == (tail_fraction_bit_mask_ & use_flag_bit_array_[num_use_flag_elem_ - 1]));

			// 空きbitがある要素を探す
			// last_allocate_index_を使って連続Allocateを多少マシにする. 毎回0から検索する場合の3倍程度は速い. 更に高速化する場合は32要素のどこかに空きがあれば対応する1bitが0になるような階層bit列などを実装する.
			// allocate_index_から使用ビットフラグ要素インデックスに変換.
			const u32 last_allocate_flag_elem_index = last_allocate_index_ / k_num_flag_elem_bit_;
			// 最初に last_allocate_flag_elem_index から末端へ検索( last_allocate_flag_elem_index+1でないのはDeallocateされている可能性があるため )
			for (auto i = last_allocate_flag_elem_index; i < num_use_flag_elem_; ++i)
			{
				if (full_mask != use_flag_bit_array_[i])
				{
					find = i;
					break;
				}
			}
			if (num_use_flag_elem_ <= find)
			{
				// 見つからなければ0からlast_allocate_flag_elem_indexまで検索
				for (auto i = 0u; i < last_allocate_flag_elem_index; ++i)
				{
					if (full_mask != use_flag_bit_array_[i])
					{
						find = i;
						break;
					}
				}
			}

			// 空きが見つからなかったら即終了
			if (num_use_flag_elem_ <= find)
			{
				std::cout << "PersistentDescriptorAllocator::Allocate: Failed to Allocate" << std::endl;
				return ret;
			}

			// 反転ビットのLSBを空きとして利用する.
			const s32 empty_bit_pos = LeastSignificantBit64(~(static_cast<u64>(use_flag_bit_array_[find])));
			assert(0 <= empty_bit_pos && empty_bit_pos < 32);
			// アロケーション位置計算
			const u32 allocation_index = (find * k_num_flag_elem_bit_) + empty_bit_pos;
			// アロケーション位置が確保しているHeap要素数を超えている場合は失敗 ( 32bit使用ビットフラグは32単位だがHeap自体は1単位なので末端でありうる )
			// 使用ビットフラグの末端の無効部分を1で埋めておけばここのチェックは不要だが誤ってゼロで埋めたりするとバグになるので素直に実装する.
			if (desc_.allocate_descriptor_count_ <= allocation_index)
			{
				std::cout << "PersistentDescriptorAllocator::Allocate: Failed to Allocate" << std::endl;
				return ret;
			}

			// 最後にAllocateしたインデックスを保存(使用ビットフラグ要素インデックスではないので注意)
			last_allocate_index_ = allocation_index;
			// 使用ビットを建てる
			use_flag_bit_array_[find] |= (1 << empty_bit_pos);

			assert(desc_.allocate_descriptor_count_ >= num_allocated_);
			// アロケーション数加算
			++num_allocated_;
			// ---------------------------------------------------------------------------------------------------------------------------------


			// 戻り値セットアップ
			ret.allocator = this;
			ret.allocation_index = allocation_index;
			// ハンドルセット
			ret.cpu_handle = heap_wrapper_.GetCpuHandleStart();// cpu_handle_start_;
			ret.gpu_handle = heap_wrapper_.GetGpuHandleStart();// gpu_handle_start_;
			// アドレスオフセット
			const auto handle_offset = heap_wrapper_.GetHandleIncrementSize() * ret.allocation_index;
			ret.cpu_handle.ptr += static_cast<size_t>(handle_offset);
			ret.gpu_handle.ptr += static_cast<size_t>(handle_offset);

			return ret;
		}

		/*
			解放
		*/
		void PersistentDescriptorAllocator::Deallocate(const PersistentDescriptorInfo& v)
		{
			// 割当元が自身でない要素の破棄をリクエストされたらアサート
			assert(v.allocator == this && v.allocation_index < desc_.allocate_descriptor_count_);
			if ((this != v.allocator) || (desc_.allocate_descriptor_count_ <= v.allocation_index))
				return;

			// アロケーションインデックスからビット位置計算.
			const u32 flag_elem_index = v.allocation_index / k_num_flag_elem_bit_;
			const u32 flag_elem_bit_pos = v.allocation_index - (flag_elem_index * k_num_flag_elem_bit_);
			const u32 flag_elem_mask = (1 << flag_elem_bit_pos);

			// ロック
			std::lock_guard<std::mutex> lock(mutex_);

			// ---------------------------------------------------------------------------------------------------------------------------------
			// 破棄対象であるにも関わらず未使用状態の場合はアサート
			assert(0 != (use_flag_bit_array_[flag_elem_index] & flag_elem_mask));
			if (0 != (use_flag_bit_array_[flag_elem_index] & flag_elem_mask))
			{
				// フラグを落とす
				use_flag_bit_array_[flag_elem_index] &= ~(1 << flag_elem_bit_pos);

				assert(0 < num_allocated_);
				// アロケーション数減算
				--num_allocated_;
			}
			// ---------------------------------------------------------------------------------------------------------------------------------
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------



		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		DynamicDescriptorManager::DynamicDescriptorManager()
		{
		}
		DynamicDescriptorManager::~DynamicDescriptorManager()
		{
			Finalize();
		}

		bool DynamicDescriptorManager::Initialize(DeviceDep* p_device, const Desc& desc)
		{
			assert(nullptr != p_device);
			if (!p_device)
				return false;
			assert(0 < desc.allocate_descriptor_count_);
			if (0 >= desc.allocate_descriptor_count_)
				return false;

			parent_device_ = p_device;

			desc_ = desc;

			// Heap作成
			{
				heap_desc_ = {};
				heap_desc_.Type = desc_.type;
				heap_desc_.NumDescriptors = desc_.allocate_descriptor_count_;
				heap_desc_.NodeMask = 0;
				// このHeap上のDescriptorは描画に利用するためVISIBLE設定. シェーダから可視.
				heap_desc_.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

				if (FAILED(p_device->GetD3D12Device()->CreateDescriptorHeap(&heap_desc_, IID_PPV_ARGS(&p_heap_))))
				{
					std::cout << "[ERROR] Create DescriptorHeap" << std::endl;
					return false;
				}

				handle_increment_size_ = p_device->GetD3D12Device()->GetDescriptorHandleIncrementSize(heap_desc_.Type);
				cpu_handle_start_ = p_heap_->GetCPUDescriptorHandleForHeapStart();
				gpu_handle_start_ = p_heap_->GetGPUDescriptorHandleForHeapStart();
			}

			range_allocator_.Initialize(desc_.allocate_descriptor_count_);

			return true;
		}
		void DynamicDescriptorManager::Finalize()
		{
		}

		// frame_index はグローバルに加算され続けるインデックスであり, Deviceから供給される.
		void DynamicDescriptorManager::ReadyToNewFrame(u32 frame_index)
		{
			// ロック
			std::lock_guard<std::mutex> lock(mutex_);

			for (int i = 0; i < deferred_deallocate_list_.size(); ++i)
			{
				if (!deferred_deallocate_list_[i]->used)
					continue;

				const auto elem_frame_index = deferred_deallocate_list_[i]->frame;
				u32 frame_diff = (frame_index > elem_frame_index) ? frame_index - elem_frame_index : elem_frame_index - frame_index;
				// フレーム差分が十分以上なら破棄.
				if (1 <= frame_diff)
				{
					// ロック中なので自身のDeallocateメソッドではなく, range_allocatorの関数を直接呼んでいる.
					for (auto h : deferred_deallocate_list_[i]->handles)
					{
						// 破棄.
						range_allocator_.Dealloc(h);
					}

					// クリアと未使用設定.
					deferred_deallocate_list_[i]->handles.clear();
					deferred_deallocate_list_[i]->used = false;
					deferred_deallocate_list_[i]->frame = 0;// 念のため
				}
			}
		}

		DynamicDescriptorAllocHandle DynamicDescriptorManager::AllocateDescriptorArray(u32 count)
		{
			assert(0 < count && desc_.allocate_descriptor_count_ > count);

			// ロック
			std::lock_guard<std::mutex> lock(mutex_);

			const auto range_handle = range_allocator_.Alloc(count);

			assert(range_handle.IsValid());
			// 空きが無かったため失敗
			if (!range_handle.IsValid())
			{
				return {};
			}

			return range_handle;
		}
		void DynamicDescriptorManager::Deallocate(const DynamicDescriptorAllocHandle& handle)
		{
			// ロック
			std::lock_guard<std::mutex> lock(mutex_);

			range_allocator_.Dealloc(handle);
		}
		void DynamicDescriptorManager::DeallocateDeferred(const DynamicDescriptorAllocHandle& handle, u32 frame_index)
		{
			// ロック
			std::lock_guard<std::mutex> lock(mutex_);

			// 登録先リスト検索.
			int frame_list_index = -1;
			int unused_list_index = -1;
			for (int i = 0; i < deferred_deallocate_list_.size(); ++i)
			{
				if (deferred_deallocate_list_[i]->used)
				{
					if (deferred_deallocate_list_[i]->frame == frame_index)
					{
						frame_list_index = i;
						// 対応する有効なリストが見つかれば終了.
						break;
					}
				}
				else
				{
					unused_list_index = i;
				}
			}
				
			if (0 > frame_list_index)
			{
				if (0 > unused_list_index)
				{
					unused_list_index = (int)deferred_deallocate_list_.size();
					// 新規.
					deferred_deallocate_list_.push_back(new DeferredDeallocateInfo());
				}
				frame_list_index = unused_list_index;


				deferred_deallocate_list_[frame_list_index]->used = true;
				deferred_deallocate_list_[frame_list_index]->frame = frame_index;
				deferred_deallocate_list_[frame_list_index]->handles.clear();
			}
			assert(0 <= frame_list_index);
			// 登録.
			deferred_deallocate_list_[frame_list_index]->handles.push_back(handle);

		}
		// ハンドルからDescriptor情報取得.
		void DynamicDescriptorManager::GetDescriptor(const DynamicDescriptorAllocHandle& handle, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head) const
		{
			assert(handle.IsValid());
			if (!handle.IsValid())
				return;

			// アロケーション位置の先頭を取得して返す.
			alloc_cpu_handle_head = cpu_handle_start_;
			alloc_cpu_handle_head.ptr += (static_cast<UINT64>(handle_increment_size_) * static_cast<UINT64>(handle.detail.head));
			alloc_gpu_handle_head = gpu_handle_start_;
			alloc_gpu_handle_head.ptr += (static_cast<UINT64>(handle_increment_size_) * static_cast<UINT64>(handle.detail.head));
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		DynamicDescriptorStackAllocatorInterface::DynamicDescriptorStackAllocatorInterface()
		{
		}
		DynamicDescriptorStackAllocatorInterface::~DynamicDescriptorStackAllocatorInterface()
		{
			Finalize();
		}

		bool DynamicDescriptorStackAllocatorInterface::Initialize(DynamicDescriptorManager* p_manager, const Desc& desc)
		{
			assert(p_manager);
			if (!p_manager)
				return false;
			assert(0 < desc.stack_size);
			if (0 >= desc.stack_size)
				return false;

			desc_ = desc;

			p_manager_ = p_manager;

			// Full扱いでリセット.
			cur_stack_use_count_ = desc_.stack_size;

			cur_stack_cpu_handle_start_ = {};
			cur_stack_gpu_handle_start_ = {};

			alloc_pages_ = {};

			return true;
		}
		void DynamicDescriptorStackAllocatorInterface::Finalize()
		{
			for (auto e : alloc_pages_)
			{
				p_manager_->Deallocate(e);
			}

			cur_stack_use_count_ = desc_.stack_size;
			p_manager_ = nullptr;
		}
		bool DynamicDescriptorStackAllocatorInterface::Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head)
		{
			assert(p_manager_);

			// そもそものスタックサイズよりも大きいサイズを要求されることは想定外.
			assert(desc_.stack_size >= count);
			if (desc_.stack_size < count)
				return false;

			// 現在のスタックが足りない場合はManagerから新規に取得. 連続要素が必要なので.
			if (desc_.stack_size < cur_stack_use_count_ + count)
			{
				// Empty
				cur_stack_use_count_ = 0;

				// Stack分を新規に確保する.
				auto alloc_result = p_manager_->AllocateDescriptorArray(desc_.stack_size);
				if (alloc_result.IsValid())
				{
					// 確保ハンドル保持.
					alloc_pages_.push_back(alloc_result);
					// Descriptor取得.
					p_manager_->GetDescriptor(alloc_result, cur_stack_cpu_handle_start_, cur_stack_gpu_handle_start_);
				}
				else
				{
					assert(false);
					return false;
				}
			}

			// 改めてチェック
			assert(desc_.stack_size >= cur_stack_use_count_ + count);
			if (desc_.stack_size < cur_stack_use_count_ + count)
				return false;

			const auto increment_size = p_manager_->GetHandleIncrementSize();
			const auto increment_offset = increment_size * cur_stack_use_count_;

			alloc_cpu_handle_head = cur_stack_cpu_handle_start_;
			alloc_cpu_handle_head.ptr += (increment_offset);
			alloc_gpu_handle_head = cur_stack_gpu_handle_start_;
			alloc_gpu_handle_head.ptr += (increment_offset);

			// スタック消費量更新.
			cur_stack_use_count_ += count;
			return true;
		}
		void DynamicDescriptorStackAllocatorInterface::Deallocate()
		{
			assert(p_manager_);

			for (auto e : alloc_pages_)
			{
				p_manager_->Deallocate(e);
			}
			alloc_pages_.clear();

			// スタックをフルにしてリセット.
			cur_stack_use_count_ = desc_.stack_size;
		}
		// 確保した領域全てを遅延破棄リクエストしてスタックをリセット. frame_indexは現在のDeviceのフレームインデックスを指定する.
		void DynamicDescriptorStackAllocatorInterface::DeallocateDeferred(u32 frame_index)
		{
			assert(p_manager_);

			for (auto e : alloc_pages_)
			{
				p_manager_->DeallocateDeferred(e, frame_index);
			}
			alloc_pages_.clear();

			// スタックをフルにしてリセット.
			cur_stack_use_count_ = desc_.stack_size;
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// 
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		FrameCommandListDynamicDescriptorAllocatorInterface::FrameCommandListDynamicDescriptorAllocatorInterface()
		{
		}
		FrameCommandListDynamicDescriptorAllocatorInterface::~FrameCommandListDynamicDescriptorAllocatorInterface()
		{
			Finalize();
		}

		bool FrameCommandListDynamicDescriptorAllocatorInterface::Initialize(DynamicDescriptorManager* p_manager, const Desc& desc)
		{
			assert(p_manager);
			if (!p_manager)
				return false;
			assert(0 < desc.stack_size);
			if (0 >= desc.stack_size)
				return false;

			desc_ = desc;

			DynamicDescriptorStackAllocatorInterface::Desc alloc_interface_desc = {};
			alloc_interface_desc.stack_size = desc.stack_size;
			alloc_interface_.Initialize(p_manager, alloc_interface_desc);

			return true;
		}
		void FrameCommandListDynamicDescriptorAllocatorInterface::Finalize()
		{
			// 念の為フレームインデックスのDescriptorを解放.
			alloc_interface_.Deallocate();

			alloc_interface_.Finalize();
		}
		// 新しいフレームの準備
		void FrameCommandListDynamicDescriptorAllocatorInterface::ReadyToNewFrame(u32 frame_index)
		{
			alloc_interface_.DeallocateDeferred(frame_index);
		}
		bool FrameCommandListDynamicDescriptorAllocatorInterface::Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head)
		{
			// フレームフリップ番号で確保.
			if (alloc_interface_.Allocate(count, alloc_cpu_handle_head, alloc_gpu_handle_head))
			{
			}
			else
			{
				assert(false);
				return false;
			}

			return true;
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		FrameDescriptorHeapPagePool::FrameDescriptorHeapPagePool()
		{
		}
		FrameDescriptorHeapPagePool::~FrameDescriptorHeapPagePool()
		{
			Finalize();
		}

		bool FrameDescriptorHeapPagePool::Initialize(DeviceDep* p_device)
		{
			assert(p_device);
			if (!p_device)
				return false;

			p_device_ = p_device;

			constexpr auto i_srv = GetHeapTypeIndex(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			constexpr auto i_sampler = GetHeapTypeIndex(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

			// CBV_SRV_UAV.
			page_size_[i_srv] = 4096;
			// SAMPLER.
			page_size_[i_sampler] = k_max_sampler_heap_handle_count;

			handle_increment_size_[i_srv] = p_device_->GetD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			handle_increment_size_[i_sampler] = p_device_->GetD3D12Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

			return true;
		}
		void FrameDescriptorHeapPagePool::Finalize()
		{
		}

		// ページを割り当て.
		ID3D12DescriptorHeap* FrameDescriptorHeapPagePool::AllocatePage(D3D12_DESCRIPTOR_HEAP_TYPE t)
		{
			assert(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == t || D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER == t);

			const auto type_index = GetHeapTypeIndex(t);

			// HeapType毎にLock.
			std::lock_guard<std::mutex> lock(mutex_[type_index]);

			// タイミングで返却リストのものを処理する.
			{
				const auto func_check_fence_complete = [](DeviceDep* p_device, u64 retired_frame)
				{
					const auto frame_index = p_device->GetDeviceFrameIndex();
					if (frame_index == retired_frame)
						return false;

					const auto max_index = ~u64(0);
					const auto frame_diff = (frame_index > retired_frame) ? (frame_index - retired_frame) : (max_index - retired_frame + (frame_index + 1));
					return (2 <= frame_diff);
				};

				// 返却Queueの先頭から経過フレームチェックで再利用可能なものをAvailableに移動.
				while (!retired_pool_[type_index].empty() && func_check_fence_complete(p_device_, retired_pool_[type_index].front().first))
				{
					available_pool_[type_index].push(retired_pool_[type_index].front().second);
					retired_pool_[type_index].pop();
				}
			}

			// 割り当てられるものがあるならそれを返す.
			if (!available_pool_[type_index].empty())
			{
				auto lend_object = available_pool_[type_index].front();
				available_pool_[type_index].pop();
				return lend_object;
			}
			else
			{
				// なければ新規に作成
				D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
				heap_desc.Type = t;
				heap_desc.NumDescriptors = page_size_[type_index];
				heap_desc.NodeMask = 0;
				// このHeap上のDescriptorは描画に利用するためVISIBLE設定. シェーダから可視.
				heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

				Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> p_heap;
				if (FAILED(p_device_->GetD3D12Device()->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&p_heap))))
				{
					std::cout << "[ERROR] Create DescriptorHeap" << std::endl;
					return nullptr;
				}
				// 生成したHeapをすべてスマートポインタで保持しておく.
				created_pool_[type_index].push_back(p_heap);

				// 確保したものを返す.
				return p_heap.Get();
			}
		}

		void FrameDescriptorHeapPagePool::DeallocatePage(D3D12_DESCRIPTOR_HEAP_TYPE t, ID3D12DescriptorHeap* p_heap)
		{
			assert(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == t || D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER == t);

			const auto type_index = GetHeapTypeIndex(t);
			const auto frame_index = p_device_->GetDeviceFrameIndex();

			// HeapType毎にLock.
			std::lock_guard<std::mutex> lock(mutex_[type_index]);

			retired_pool_[type_index].push(std::make_pair(frame_index, p_heap));
		}


		u32 FrameDescriptorHeapPagePool::GetPageSize(D3D12_DESCRIPTOR_HEAP_TYPE t) const
		{
			assert(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == t || D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER == t);

			return page_size_[GetHeapTypeIndex(t)];
		}
		u32	FrameDescriptorHeapPagePool::GetHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE t) const
		{
			assert(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == t || D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER == t);

			return handle_increment_size_[GetHeapTypeIndex(t)];
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		FrameDescriptorHeapPageInterface::FrameDescriptorHeapPageInterface()
		{
		}
		FrameDescriptorHeapPageInterface::~FrameDescriptorHeapPageInterface()
		{
			Finalize();
		}

		bool FrameDescriptorHeapPageInterface::Initialize(FrameDescriptorHeapPagePool* p_pool, const Desc& desc)
		{
			assert(p_pool);
			if (!p_pool)
				return false;

			assert(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == desc.type || D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER == desc.type);
			// CBV_SRV_UAVとSAMPLERのみ対応.
			if (!(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == desc.type || D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER == desc.type))
				return false;

			p_pool_ = p_pool;
			desc_ = desc;
			use_handle_count_ = 0;

			return true;
		}
		void FrameDescriptorHeapPageInterface::Finalize()
		{
			if (p_cur_heap_)
			{
				// 現在のPageを返却.
				p_pool_->DeallocatePage(desc_.type, p_cur_heap_);
				p_cur_heap_ = nullptr;
			}
		}
		u32	FrameDescriptorHeapPageInterface::GetHandleIncrementSize() const
		{
			return p_pool_->GetHandleIncrementSize(desc_.type);
		}
		bool FrameDescriptorHeapPageInterface::Allocate(u32 handle_count, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head)
		{
			assert(p_pool_);

			// Pageが確保されていないか, 必要個数が確保できない場合はPageを新規取得.
			if (!p_cur_heap_ || (use_handle_count_ + handle_count > p_pool_->GetPageSize(desc_.type)))
			{
				if (p_cur_heap_)
				{
					// 現在のPageを返却.
					p_pool_->DeallocatePage(desc_.type, p_cur_heap_);
					p_cur_heap_ = nullptr;
				}

				// 新規Page.
				p_cur_heap_ = p_pool_->AllocatePage(desc_.type);
				assert(p_cur_heap_);
				if (!p_cur_heap_)
				{
					return false;
				}
				// 各種リセット.
				use_handle_count_ = 0;
				cur_cpu_handle_start_ = p_cur_heap_->GetCPUDescriptorHandleForHeapStart();
				cur_gpu_handle_start_ = p_cur_heap_->GetGPUDescriptorHandleForHeapStart();
			}

			// Handleを確保.
			const auto increment_size = p_pool_->GetHandleIncrementSize(desc_.type);
			// 使用範囲の末端までオフセットして割り当てる.
			const auto increment_offset = increment_size * use_handle_count_;

			// ページ先頭から位置までオフセットして返す.
			alloc_cpu_handle_head = cur_cpu_handle_start_;
			alloc_cpu_handle_head.ptr += (increment_offset);
			alloc_gpu_handle_head = cur_gpu_handle_start_;
			alloc_gpu_handle_head.ptr += (increment_offset);

			// 確保分だけ使用数加算.
			use_handle_count_ += handle_count;

			return true;
		}
	}
}