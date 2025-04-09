#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <array>
#include <unordered_map>
#include <mutex>


#include "rhi/rhi.h"
#include "rhi/d3d12/rhi_util.d3d12.h"

#include "text/hash_text.h"
#include "util/types.h"


namespace ngl
{
	namespace rhi
	{
		class DeviceDep;
		class PersistentDescriptorAllocator;
		class FrameCommandListDescriptorInterface;


		// Pipelineへ設定するDescriptor群をまとめるオブジェクト.
		//	一旦このオブジェクトに描画に必要なDescriptorを設定し, CommapndListにこのオブジェクトを設定する際に動的DescriptorHeapから取得した連続DescriptorにコピーしてそれをPipelineにセットする.
		//	連続Descriptor対応のために各リソースタイプ毎に1Pipelineで使用できるテーブルサイズの上限(レジスタ番号の上限)がある. -> k_cbv_table_size他.
		//	リソース名でレジスタ番号を指定してDescriptorを設定したい場合は対応するPipelineの PipelineResourceViewLayoutDep がmapで保持しているのでそちらを利用する仕組みを作る.
		class DescriptorSetDep
		{
		private:
			template<u32 SIZE>
			struct Handles
			{
				D3D12_CPU_DESCRIPTOR_HANDLE	cpu_handles[SIZE] = {};
				// ランタイムで設定された最大のレジスタインデックス. このシステムでは必ず0開始連番でバインドするため, 最大レジスタインデックス+1分だけDynamicDescriptorを確保してCommandListに積む.
				int							max_use_register_index = -1;

				void Reset()
				{
					memset(cpu_handles, 0, sizeof(cpu_handles));
					max_use_register_index = -1;
				}

				void SetHandle(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
				{
					assert(index < SIZE);
					cpu_handles[index] = handle;
					max_use_register_index = std::max<int>(max_use_register_index, static_cast<int>(index));
				}
			};

		public:
			DescriptorSetDep()
			{}
			~DescriptorSetDep()
			{}

			void Reset();

			inline void SetVsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetVsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetVsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetPsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetPsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetPsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetPsUav(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetGsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetGsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetGsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetHsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetHsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetHsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetDsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetDsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetDsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetCsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetCsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetCsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);
			inline void SetCsUav(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle);

			const Handles<k_cbv_table_size>& GetVsCbv() const { return vs_cbv_; }
			const Handles<k_srv_table_size>& GetVsSrv() const { return vs_srv_; }
			const Handles<k_sampler_table_size>& GetVsSampler() const { return vs_sampler_; }
			const Handles<k_cbv_table_size>& GetPsCbv() const { return ps_cbv_; }
			const Handles<k_srv_table_size>& GetPsSrv() const { return ps_srv_; }
			const Handles<k_sampler_table_size>& GetPsSampler() const { return ps_sampler_; }
			const Handles<k_uav_table_size>& GetPsUav() const { return ps_uav_; }
			const Handles<k_cbv_table_size>& GetGsCbv() const { return gs_cbv_; }
			const Handles<k_srv_table_size>& GetGsSrv() const { return gs_srv_; }
			const Handles<k_sampler_table_size>& GetGsSampler() const { return gs_sampler_; }
			const Handles<k_cbv_table_size>& GetHsCbv() const { return hs_cbv_; }
			const Handles<k_srv_table_size>& GetHsSrv() const { return hs_srv_; }
			const Handles<k_sampler_table_size>& GetHsSampler() const { return hs_sampler_; }
			const Handles<k_cbv_table_size>& GetDsCbv() const { return ds_cbv_; }
			const Handles<k_srv_table_size>& GetDsSrv() const { return ds_srv_; }
			const Handles<k_sampler_table_size>& GetDsSampler() const { return ds_sampler_; }
			const Handles<k_cbv_table_size>& GetCsCbv() const { return cs_cbv_; }
			const Handles<k_srv_table_size>& GetCsSrv() const { return cs_srv_; }
			const Handles<k_sampler_table_size>& GetCsSampler() const { return cs_sampler_; }
			const Handles<k_uav_table_size>& GetCsUav() const { return cs_uav_; }

		private:
			Handles<k_cbv_table_size>		vs_cbv_;
			Handles<k_srv_table_size>		vs_srv_;
			Handles<k_sampler_table_size>	vs_sampler_;
			Handles<k_cbv_table_size>		ps_cbv_;
			Handles<k_srv_table_size>		ps_srv_;
			Handles<k_sampler_table_size>	ps_sampler_;
			Handles<k_uav_table_size>		ps_uav_;
			Handles<k_cbv_table_size>		gs_cbv_;
			Handles<k_srv_table_size>		gs_srv_;
			Handles<k_sampler_table_size>	gs_sampler_;
			Handles<k_cbv_table_size>		hs_cbv_;
			Handles<k_srv_table_size>		hs_srv_;
			Handles<k_sampler_table_size>	hs_sampler_;
			Handles<k_cbv_table_size>		ds_cbv_;
			Handles<k_srv_table_size>		ds_srv_;
			Handles<k_sampler_table_size>	ds_sampler_;
			Handles<k_cbv_table_size>		cs_cbv_;
			Handles<k_srv_table_size>		cs_srv_;
			Handles<k_sampler_table_size>	cs_sampler_;
			Handles<k_uav_table_size>		cs_uav_;
		};

		inline void DescriptorSetDep::Reset()
		{
			vs_cbv_.Reset();
			vs_srv_.Reset();
			vs_sampler_.Reset();
			ps_cbv_.Reset();
			ps_srv_.Reset();
			ps_sampler_.Reset();
			ps_uav_.Reset();
			gs_cbv_.Reset();
			gs_srv_.Reset();
			gs_sampler_.Reset();
			hs_cbv_.Reset();
			hs_srv_.Reset();
			hs_sampler_.Reset();
			ds_cbv_.Reset();
			ds_srv_.Reset();
			ds_sampler_.Reset();
			cs_cbv_.Reset();
			cs_srv_.Reset();
			cs_sampler_.Reset();
			cs_uav_.Reset();
		}

		inline void DescriptorSetDep::SetVsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			vs_cbv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetVsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			vs_srv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetVsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			vs_sampler_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetPsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			ps_cbv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetPsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			ps_srv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetPsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			ps_sampler_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetPsUav(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			ps_uav_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetGsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			gs_cbv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetGsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			gs_srv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetGsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			gs_sampler_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetHsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			hs_cbv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetHsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			hs_srv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetHsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			hs_sampler_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetDsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			ds_cbv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetDsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			ds_srv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetDsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			ds_sampler_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetCsCbv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			cs_cbv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetCsSrv(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			cs_srv_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetCsSampler(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			cs_sampler_.SetHandle(index, handle);
		}
		inline void DescriptorSetDep::SetCsUav(u32 index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle)
		{
			cs_uav_.SetHandle(index, handle);
		}


		// DescriptorHeadpの管理Wrapper.
		class DescriptorHeapWrapper
		{
		public:
			struct Desc
			{
				D3D12_DESCRIPTOR_HEAP_TYPE	type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				u32							allocate_descriptor_count_ = 0;
				// Shaderからの可視性. trueの場合GPU常駐メモリに確保される.
				// CopyDescriptorsのSrcに利用するHeapはfalseでなければならない.
				// RTV/DSVのHeapはtrueの場合ValidationErrorとなる.
				// trueとしたHeapに直接CPUから書き込む運用も特に問題はない(https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_descriptor_heap_flags).
				bool						shader_visible = true;
			};

			DescriptorHeapWrapper();
			~DescriptorHeapWrapper();
			bool Initialize(DeviceDep* p_device, const Desc& desc);
			void Finalize();

			const Desc& GetDesc() const
			{
				return desc_;
			}
			ID3D12DescriptorHeap* GetD3D12()
			{
				return p_heap_.Get();
			}
			const ID3D12DescriptorHeap* GetD3D12() const
			{
				return p_heap_.Get();
			}
			u32 GetHandleIncrementSize() const
			{
				return handle_increment_size_;
			}
			const D3D12_CPU_DESCRIPTOR_HANDLE& GetCpuHandleStart() const
			{
				return cpu_handle_start_;
			}
			const D3D12_GPU_DESCRIPTOR_HANDLE& GetGpuHandleStart() const
			{
				return gpu_handle_start_;
			}

		private:
			// オブジェクト初期化情報
			Desc		desc_ = {};

			// Heap本体
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>	p_heap_ = nullptr;
			// Headの定義情報
			D3D12_DESCRIPTOR_HEAP_DESC		heap_desc_{};
			// Heap上の要素アドレスサイズ
			u32								handle_increment_size_ = 0;
			// CPU/GPUハンドル先頭
			D3D12_CPU_DESCRIPTOR_HANDLE		cpu_handle_start_ = {};
			D3D12_GPU_DESCRIPTOR_HANDLE		gpu_handle_start_ = {};
		};


		// PersistentDescriptorAllocator から取得したDescriptorHandleの管理.
		struct PersistentDescriptorInfo
		{
			static constexpr u32 k_invalid_allocation_index = ~u32(0);

			static bool IsValid(const PersistentDescriptorInfo& v)
			{
				return (k_invalid_allocation_index != v.allocation_index && nullptr != v.allocator);
			}
			bool IsValid() const
			{
				return IsValid(*this);
			}

			// アロケーション元
			PersistentDescriptorAllocator*	allocator = nullptr;
			u32								allocation_index = k_invalid_allocation_index;

			D3D12_CPU_DESCRIPTOR_HANDLE	cpu_handle = {};
			D3D12_GPU_DESCRIPTOR_HANDLE	gpu_handle = {};
		};

		/*
			リソースのDescriptorを配置する目的のグローバルなHeapを管理する
			現状はShaderから不可視(ShaderVisible=false)なHeapを管理する.
			ここで管理されているDescriptorは直接描画には利用されず,別実装のFrameDescriptorHeap上に描画直前にCopyDescriptorsでコピーされて利用される.

			mutexによって Allocate と Deallocate はスレッドセーフ.

			非常に単純な線形探索アロケーション方式.
		*/
		class PersistentDescriptorAllocator
		{
		public:
			struct Desc
			{
				D3D12_DESCRIPTOR_HEAP_TYPE	type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				u32							allocate_descriptor_count_ = 500000;
			};

			PersistentDescriptorAllocator();
			~PersistentDescriptorAllocator();

			bool Initialize(DeviceDep* p_device, const Desc& desc);
			void Finalize();

			PersistentDescriptorInfo	Allocate();
			void						Deallocate(const PersistentDescriptorInfo& v);

			PersistentDescriptorInfo	GetDefaultPersistentDescriptor() const
			{
				return default_persistent_descriptor_;
			}

		private:
			std::mutex			mutex_;

			// オブジェクト初期化情報
			Desc				desc_ = {};

			// 要素のアロケーション状態フラグ.
			u32					num_use_flag_elem_ = 0;
			std::vector<u32>	use_flag_bit_array_;
			// 空き検索を効率化するために前回のAllocateで確保した位置の次のインデックスを保持しておく. シーケンシャルな確保が効率化されるだけで根本的な高速化にはならないので注意.
			u32					last_allocate_index_ = 0;
			// アロケーションされている要素数
			u32					num_allocated_ = 0;

			// デバッグ用の端数部マスク
			u32					tail_fraction_bit_mask_ = 0;

			DescriptorHeapWrapper			heap_wrapper_ = {};

			// 安全のために未使用スロットへコピーするための空のデフォルトDescriptor.
			PersistentDescriptorInfo		default_persistent_descriptor_;

		private:
			static constexpr u32 k_num_flag_elem_bit_ = sizeof(decltype(*use_flag_bit_array_.data())) * 8;
		};



		namespace dynamic_descriptor_allocator
		{
			// アロケーションハンドル.
			struct RangeHandle
			{
				RangeHandle()
					: data(0)
				{
				}

				bool IsValid() const
				{
					// size 0 で有効無効チェック.
					return detail.size != 0;
				}
				union
				{
					uint64_t data;
					struct
					{
						uint32_t head;
						uint32_t size;
					} detail;
				};
			};
			// アロケータ
			class RangeAllocator
			{
			public:

			public:
				RangeAllocator();
				~RangeAllocator();

				bool Initialize(uint32_t max_size);
				void Finalize();

				// 確保.
				RangeHandle Alloc(uint32_t size);
				// 解放.
				void Dealloc(const RangeHandle& handle);

				uint32_t MaxSize() const;
			private:
				class RangeAllocatorImpl* impl_ = nullptr;
			};
		}

		using DynamicDescriptorAllocHandle = dynamic_descriptor_allocator::RangeHandle;

		/*
			DynamicDescriptorManagerは巨大なHeapを確保し, 要求に応じて連続領域を切り出して貸し出す役割. スレッドセーフ.
			基本的に細かいサイズでのアロケーションには使わず, 2000等の大きな単位でアロケーションしてページのように利用する.

			SamplerはHeapのサイズがAPIで制限されているため別途クラスを用意する.
		*/
		class DynamicDescriptorManager
		{
		public:
			struct Desc
			{
				D3D12_DESCRIPTOR_HEAP_TYPE	type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				// 一括確保するDescriptor数
				u32							allocate_descriptor_count_ = 500000;
			};

			DynamicDescriptorManager();
			~DynamicDescriptorManager();

			bool Initialize(DeviceDep* p_device, const Desc& desc);
			void Finalize();

			// frame_index はグローバルに加算され続けるインデックスであり, Deviceから供給される.
			void ReadyToNewFrame(u32 frame_index);

			// 確保.
			DynamicDescriptorAllocHandle AllocateDescriptorArray(u32 count);
			// 解放.
			void Deallocate(const DynamicDescriptorAllocHandle& handle);
			// 遅延解放リクエスト. frame_indexは現在のDeviceのフレームインデックスを指定する.
			void DeallocateDeferred(const DynamicDescriptorAllocHandle& handle, u32 frame_index);
			// ハンドルからDescriptor情報取得.
			void GetDescriptor(const DynamicDescriptorAllocHandle& handle, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head) const;

			u32	GetHandleIncrementSize() const
			{
				return handle_increment_size_;
			}
			ID3D12DescriptorHeap* GetD3D12DescriptorHeap()
			{
				return p_heap_.Get();
			}

			DeviceDep* GetDevice() { return parent_device_; }

		private:
			std::mutex							mutex_;

			Desc								desc_;

			DeviceDep*							parent_device_ = nullptr;

			// Heap本体
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>		p_heap_ = nullptr;
			D3D12_DESCRIPTOR_HEAP_DESC			heap_desc_{};
			// Heap上の要素アドレスサイズ
			u32									handle_increment_size_ = 0;
			// CPU/GPUハンドル先頭
			D3D12_CPU_DESCRIPTOR_HANDLE			cpu_handle_start_ = {};
			D3D12_GPU_DESCRIPTOR_HANDLE			gpu_handle_start_ = {};

			dynamic_descriptor_allocator::RangeAllocator	range_allocator_ = {};

			struct DeferredDeallocateInfo
			{
				std::vector<DynamicDescriptorAllocHandle>	handles ;
				uint32_t									frame = 0;
				bool										used = false;
			};
			std::vector<DeferredDeallocateInfo*> deferred_deallocate_list_ = {};
		};

		/*
			DynamicDescriptorManagerから固定ページで確保したDescriptorをStackベースで貸し出すインターフェース.
			DynamicDescriptorManagerから取得した固定範囲をStackAllocatorとして利用し. 足りなくなれば追加で DynamicDescriptorManager から取得する.

			解放は一括でそれまでに確保したページ全てを解放する.
		*/
		class DynamicDescriptorStackAllocatorInterface
		{
		public:
			struct Desc
			{
				u32		stack_size = 2000;
			};

			DynamicDescriptorStackAllocatorInterface();
			~DynamicDescriptorStackAllocatorInterface();

			bool Initialize(DynamicDescriptorManager* p_manager, const Desc& desc);
			void Finalize();

			// スタックベースでDescriptorを確保.
			bool Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head);
			// 確保した領域全てを解放してスタックをリセット.
			void Deallocate();
			// 確保した領域全てを遅延破棄リクエストしてスタックをリセット. frame_indexは現在のDeviceのフレームインデックスを指定する.
			void DeallocateDeferred(u32 frame_index);

			DynamicDescriptorManager* GetManager()
			{
				return p_manager_;
			}

		private:
			Desc								desc_ = {};

			DynamicDescriptorManager*					p_manager_ = nullptr;
			std::vector<DynamicDescriptorAllocHandle>	alloc_pages_;

			u32									cur_stack_use_count_ = 0;
			D3D12_CPU_DESCRIPTOR_HANDLE			cur_stack_cpu_handle_start_ = {};
			D3D12_GPU_DESCRIPTOR_HANDLE			cur_stack_gpu_handle_start_ = {};
		};
		
		/*
			DynamicDescriptorStackAllocatorInterface を使用してフレーム毎のCommandListが使用するDescritpro領域を管理する.
			確保時のIDにフレームフリップインデックスを使うことで, システム(Device)がフレーム開始処理で過去フレームの確保した領域を自動で解放するようになっている.
		*/
		class FrameCommandListDynamicDescriptorAllocatorInterface
		{
		public:
			struct Desc
			{
				u32						stack_size = 2000;
			};

			FrameCommandListDynamicDescriptorAllocatorInterface();
			~FrameCommandListDynamicDescriptorAllocatorInterface();

			bool Initialize(DynamicDescriptorManager* p_manager, const Desc& desc);
			void Finalize();

			// frame_index はグローバルに加算され続けるインデックスであり, Deviceから供給される.
			void ReadyToNewFrame(u32 frame_index);

			bool Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head);

			DynamicDescriptorManager* GetManager()
			{
				return alloc_interface_.GetManager();
			}

		private:
			Desc								desc_ = {};
			DynamicDescriptorStackAllocatorInterface	alloc_interface_ = {};
		};


		// Page単位でDescriptorHeapを確保するManager. 一つのHeapサイズに制限があるSampler向けだが, CBV_SRV_UAVでも利用可能.
		//	基本的にはFrameDescriptorHeapPageInterface経由で利用され, Allocで足りなくなった場合は持っていたPageをPoolに返却して新たなPageを取得するというライフサイクル.
		//	返却されたPageの利用可能リストへの移動などの処理はDeviceのフレームインデックスと協調して自動的に実行される. (Allocate時についでに処理している).
		class FrameDescriptorHeapPagePool
		{
		public:
			static constexpr u32 k_max_sampler_heap_handle_count = 2048;// D3D12の制限2048.

			// HeapTypeの種類数の定義のためだけの意味のない定義.
			static constexpr D3D12_DESCRIPTOR_HEAP_TYPE k_heap_types[] =
			{
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
			};
			static constexpr auto k_num_heap_types = std::size(k_heap_types);


			static constexpr u32 GetHeapTypeIndex(D3D12_DESCRIPTOR_HEAP_TYPE t)
			{
				assert(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == t || D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER == t);

				return (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV == t)? 0 : 1;
			}

			FrameDescriptorHeapPagePool();
			~FrameDescriptorHeapPagePool();

			bool Initialize(DeviceDep* p_device);
			void Finalize();

			// Pageを割り当て.
			ID3D12DescriptorHeap* AllocatePage(D3D12_DESCRIPTOR_HEAP_TYPE t);
			// Pageを返却.
			void DeallocatePage(D3D12_DESCRIPTOR_HEAP_TYPE t, ID3D12DescriptorHeap* p_heap);
 
			u32 GetPageSize(D3D12_DESCRIPTOR_HEAP_TYPE t) const;
			u32	GetHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE t) const;

		private:
			DeviceDep* p_device_ = {};

			// index 0 -> CBV_SRV_UAV
			// index 1 -> SAMPLER
			std::mutex		mutex_[k_num_heap_types] = {};

			u32 page_size_[k_num_heap_types] = {};

			u32 handle_increment_size_[k_num_heap_types] = {};

			// 生成したHeapは解放のためにすべてスマートポインタで保持.
			std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>>	created_pool_[k_num_heap_types];
			std::queue<ID3D12DescriptorHeap*>			available_pool_[k_num_heap_types];
			// 返却時のフレームインデックスをペアで格納.
			std::queue<std::pair<u64, ID3D12DescriptorHeap*>> retired_pool_[k_num_heap_types];

		};

		/*
			CommandListがそれぞれ保持するフレーム毎のDescriptorHeap管理.
			内部ではPoolへの参照をもち, Poolから一定サイズのHeap単位をPageとして取得して利用する.
			
			FrameCommandListDescriptorInterface との違いは巨大な単一のHeapではなくPage単位のHeapを利用する点.
			これによりHeap辺り2048のハンドルまでという制限のあるSamplerのDescriptorにも対応している.
		*/
		class FrameDescriptorHeapPageInterface
		{
		public:
			struct Desc
			{
				// D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV or D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER.
				D3D12_DESCRIPTOR_HEAP_TYPE	type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			};


			FrameDescriptorHeapPageInterface();
			~FrameDescriptorHeapPageInterface();

			bool Initialize(FrameDescriptorHeapPagePool* p_pool, const Desc& desc);
			void Finalize();

			const FrameDescriptorHeapPagePool* GetPool() const { return p_pool_; }
			FrameDescriptorHeapPagePool* GetPool() { return p_pool_; }

			bool Allocate(u32 handle_count, D3D12_CPU_DESCRIPTOR_HANDLE& alloc_cpu_handle_head, D3D12_GPU_DESCRIPTOR_HANDLE& alloc_gpu_handle_head);
			ID3D12DescriptorHeap* GetD3D12DescriptorHeap() { return p_cur_heap_; }

			u32	GetHandleIncrementSize() const;
		private:
			Desc							desc_ = {};
			FrameDescriptorHeapPagePool*	 p_pool_ = {};

			u32								use_handle_count_ = 0;
			ID3D12DescriptorHeap*			p_cur_heap_ = {};
			D3D12_CPU_DESCRIPTOR_HANDLE		cur_cpu_handle_start_ = {};
			D3D12_GPU_DESCRIPTOR_HANDLE		cur_gpu_handle_start_ = {};
		};


	}
}