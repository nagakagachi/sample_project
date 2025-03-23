
#include "rhi/d3d12/resource_view.d3d12.h"

#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/resource.d3d12.h"

#include <array>
#include <algorithm>

namespace ngl
{
	namespace rhi
	{


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		ConstantBufferViewDep::ConstantBufferViewDep()
		{
		}
		ConstantBufferViewDep::~ConstantBufferViewDep()
		{
			Finalize();
		}
		bool ConstantBufferViewDep::Initialize(BufferDep* p_buffer, const Desc& desc)
		{
			if (!p_buffer || !p_buffer->GetParentDevice())
			{
				assert(false);
				return false;
			}
			if (!check_bits(ResourceBindFlag::ConstantBuffer, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			auto&& p_device = p_buffer->GetParentDevice();

			auto&& descriptor_allocator = p_device->GetPersistentDescriptorAllocator();
			view_ = descriptor_allocator->Allocate();
			if (!view_.IsValid())
			{
				std::cout << "[ERROR] ConstantBufferViewDep::Initialize" << std::endl;
				assert(false);
				return false;
			}

			D3D12_CONSTANT_BUFFER_VIEW_DESC view_desc = {};
			view_desc.BufferLocation = p_buffer->GetD3D12Resource()->GetGPUVirtualAddress();
			view_desc.SizeInBytes = p_buffer->GetBufferSize();// アライメント考慮サイズを指定している.
			auto handle = view_.cpu_handle;
			p_device->GetD3D12Device()->CreateConstantBufferView(&view_desc, handle);

			return true;
		}
		void ConstantBufferViewDep::Finalize()
		{
			auto&& descriptor_allocator = view_.allocator;
			if (descriptor_allocator)
			{
				descriptor_allocator->Deallocate(view_);
			}

			view_ = {};
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		VertexBufferViewDep::VertexBufferViewDep()
		{
		}
		VertexBufferViewDep::~VertexBufferViewDep()
		{
			Finalize();
		}
		bool VertexBufferViewDep::Initialize(BufferDep* p_buffer, const Desc& desc)
		{
			InitializeRhiObject(p_buffer->GetParentDevice());

			if (!p_buffer || !p_buffer->GetParentDevice())
			{
				assert(false);
				return false;
			}

			const auto& buffer_desc = p_buffer->GetDesc();
			if (!check_bits(ResourceBindFlag::VertexBuffer, buffer_desc.bind_flag))
			{
				assert(false);
				return false;
			}

			view_ = {};
			view_.SizeInBytes = buffer_desc.element_count * buffer_desc.element_byte_size;
			view_.StrideInBytes = buffer_desc.element_byte_size;
			view_.BufferLocation = p_buffer->GetD3D12Resource()->GetGPUVirtualAddress();

			return true;
		}
		void VertexBufferViewDep::Finalize()
		{
			view_ = {};
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		IndexBufferViewDep::IndexBufferViewDep()
		{
		}
		IndexBufferViewDep::~IndexBufferViewDep()
		{
			Finalize();
		}
		bool IndexBufferViewDep::Initialize(BufferDep* p_buffer, const Desc& desc)
		{
			InitializeRhiObject(p_buffer->GetParentDevice());

			if (!p_buffer || !p_buffer->GetParentDevice())
			{
				assert(false);
				return false;
			}

			const auto& buffer_desc = p_buffer->GetDesc();
			if (!check_bits(ResourceBindFlag::IndexBuffer, buffer_desc.bind_flag))
			{
				assert(false);
				return false;
			}

			view_ = {};
			view_.SizeInBytes = buffer_desc.element_count * buffer_desc.element_byte_size;
			view_.BufferLocation = p_buffer->GetD3D12Resource()->GetGPUVirtualAddress();
			view_.Format = (buffer_desc.element_byte_size == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

			return true;
		}
		void IndexBufferViewDep::Finalize()
		{
			view_ = {};
		}
		// -------------------------------------------------------------------------------------------------------------------------------------------------


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		SamplerDep::SamplerDep()
		{
		}
		SamplerDep::~SamplerDep()
		{
			Finalize();
		}
		bool SamplerDep::Initialize(DeviceDep* p_device, const Desc& desc)
		{
			InitializeRhiObject(p_device);

			if (!p_device)
				return false;

			auto&& descriptor_allocator = p_device->GetPersistentSamplerDescriptorAllocator();// Samplerは専用のAllocatorを利用.
			view_ = descriptor_allocator->Allocate();
			if (!view_.IsValid())
				return false;

			// Persistent上に作成.
			D3D12_SAMPLER_DESC d3ddesc = {};
			d3ddesc.AddressU = ConvertTextureAddressMode(desc.AddressU);
			d3ddesc.AddressV = ConvertTextureAddressMode(desc.AddressV);
			d3ddesc.AddressW = ConvertTextureAddressMode(desc.AddressW);
			d3ddesc.BorderColor[0] = desc.BorderColor[0];
			d3ddesc.BorderColor[1] = desc.BorderColor[1];
			d3ddesc.BorderColor[2] = desc.BorderColor[2];
			d3ddesc.BorderColor[3] = desc.BorderColor[3];
			d3ddesc.ComparisonFunc = ConvertComparisonFunc(desc.ComparisonFunc);
			d3ddesc.Filter = ConvertTextureFilter(desc.Filter);
			d3ddesc.MaxAnisotropy = desc.MaxAnisotropy;
			d3ddesc.MaxLOD = desc.MaxLOD;
			d3ddesc.MinLOD = desc.MinLOD;
			d3ddesc.MipLODBias = desc.MipLODBias;

			p_device->GetD3D12Device()->CreateSampler(&(d3ddesc), view_.cpu_handle);
			return true;
		}
		void SamplerDep::Finalize()
		{
			auto&& descriptor_allocator = view_.allocator;
			if (descriptor_allocator)
			{
				descriptor_allocator->Deallocate(view_);
			}
			view_ = {};
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------

		namespace
		{
			/** texture dimension -> view dimension.
			*/
			EResourceDimension getTextureDimension(ETextureType type, bool is_texture_array)
			{
				switch (type)
				{
					//case TextureType::Buffer:
					//	assert(is_texture_array == false);
					//	return ResourceDimension::Buffer;
				case ETextureType::Texture1D:
					return (is_texture_array) ? EResourceDimension::Texture1DArray : EResourceDimension::Texture1D;
				case ETextureType::Texture2D:
					return (is_texture_array) ? EResourceDimension::Texture2DArray : EResourceDimension::Texture2D;
				case ETextureType::Texture2DMultisample:
					return (is_texture_array) ? EResourceDimension::Texture2DMSArray : EResourceDimension::Texture2DMS;
				case ETextureType::Texture3D:
					assert(is_texture_array == false);
					return EResourceDimension::Texture3D;
				case ETextureType::TextureCube:
					return (is_texture_array) ? EResourceDimension::TextureCubeArray : EResourceDimension::TextureCube;
				default:
					assert(false);
					return EResourceDimension::Unknown;
				}
			}

			/** view dimension to D3D12 view dimension.
			*/
			template<typename ViewType>
			ViewType getTextureViewDimension(EResourceDimension dimension);

			// for srv.
			template<>
			D3D12_SRV_DIMENSION getTextureViewDimension<D3D12_SRV_DIMENSION>(EResourceDimension dimension)
			{
				switch (dimension)
				{
				case EResourceDimension::Texture1D: return D3D12_SRV_DIMENSION_TEXTURE1D;
				case EResourceDimension::Texture1DArray: return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
				case EResourceDimension::Texture2D: return D3D12_SRV_DIMENSION_TEXTURE2D;
				case EResourceDimension::Texture2DArray: return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				case EResourceDimension::Texture2DMS: return D3D12_SRV_DIMENSION_TEXTURE2DMS;
				case EResourceDimension::Texture2DMSArray: return D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
				case EResourceDimension::Texture3D: return D3D12_SRV_DIMENSION_TEXTURE3D;
				case EResourceDimension::TextureCube: return D3D12_SRV_DIMENSION_TEXTURECUBE;
				case EResourceDimension::TextureCubeArray: return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
				case EResourceDimension::AccelerationStructure: return D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
				default:
					assert(false);
					return D3D12_SRV_DIMENSION_UNKNOWN;
				}
			}
			// for uav.
			template<>
			D3D12_UAV_DIMENSION getTextureViewDimension<D3D12_UAV_DIMENSION>(EResourceDimension dimension)
			{
				switch (dimension)
				{
				case EResourceDimension::Texture1D: return D3D12_UAV_DIMENSION_TEXTURE1D;
				case EResourceDimension::Texture1DArray: return D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
				case EResourceDimension::Texture2D: return D3D12_UAV_DIMENSION_TEXTURE2D;
				case EResourceDimension::Texture2DArray: return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
				case EResourceDimension::Texture3D: return D3D12_UAV_DIMENSION_TEXTURE3D;
				default:
					assert(false);
					return D3D12_UAV_DIMENSION_UNKNOWN;
				}
			}
			// for dsv.
			template<>
			D3D12_DSV_DIMENSION getTextureViewDimension<D3D12_DSV_DIMENSION>(EResourceDimension dimension)
			{
				switch (dimension)
				{
				case EResourceDimension::Texture1D: return D3D12_DSV_DIMENSION_TEXTURE1D;
				case EResourceDimension::Texture1DArray: return D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
				case EResourceDimension::Texture2D: return D3D12_DSV_DIMENSION_TEXTURE2D;
				case EResourceDimension::Texture2DArray: return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				case EResourceDimension::Texture2DMS: return D3D12_DSV_DIMENSION_TEXTURE2DMS;
				case EResourceDimension::Texture2DMSArray: return D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
					// TODO: Falcor previously mapped cube to 2D array. Not sure if needed anymore.
					//case ReflectionResourceType::Dimensions::TextureCube: return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				default:
					assert(false);
					return D3D12_DSV_DIMENSION_UNKNOWN;
				}
			}
			// for rtv.
			template<>
			D3D12_RTV_DIMENSION getTextureViewDimension<D3D12_RTV_DIMENSION>(EResourceDimension dimension)
			{
				switch (dimension)
				{
				case EResourceDimension::Texture1D: return D3D12_RTV_DIMENSION_TEXTURE1D;
				case EResourceDimension::Texture1DArray: return D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
				case EResourceDimension::Texture2D: return D3D12_RTV_DIMENSION_TEXTURE2D;
				case EResourceDimension::Texture2DArray: return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				case EResourceDimension::Texture2DMS: return D3D12_RTV_DIMENSION_TEXTURE2DMS;
				case EResourceDimension::Texture2DMSArray: return D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
				case EResourceDimension::Texture3D: return D3D12_RTV_DIMENSION_TEXTURE3D;
				default:
					assert(false);
					return D3D12_RTV_DIMENSION_UNKNOWN;
				}
			}




			// -------------------------------------------------------------------------------------------------------------------------------------------------
			// Texture Dsv, Rtv, Uavの設定共通化.
			template<typename DescType>
			DescType createDsvRtvUavDescCommon(const TextureDep* p_texture, uint32_t mip_slice, uint32_t first_array_slice, uint32_t array_size)
			{
				assert(p_texture);   // Buffers should not get here

				uint32_t arrayMultiplier = (p_texture->GetType() == ETextureType::TextureCube) ? 6 : 1;

				if (array_size + first_array_slice > p_texture->GetArraySize())
				{
					array_size = p_texture->GetArraySize() - first_array_slice;
				}

				DescType desc = {};
				desc.Format = ConvertResourceFormat(p_texture->GetFormat());
				desc.ViewDimension = getTextureViewDimension<decltype(desc.ViewDimension)>(getTextureDimension(p_texture->GetType(), p_texture->GetArraySize() > 1));

				switch (p_texture->GetType())
				{
				case ETextureType::Texture1D:
					if (p_texture->GetArraySize() > 1)
					{
						desc.Texture1DArray.ArraySize = array_size;
						desc.Texture1DArray.FirstArraySlice = first_array_slice;
						desc.Texture1DArray.MipSlice = mip_slice;
					}
					else
					{
						desc.Texture1D.MipSlice = mip_slice;
					}
					break;
				case ETextureType::Texture2D:
				case ETextureType::TextureCube:
					if (p_texture->GetArraySize() * arrayMultiplier > 1)
					{
						desc.Texture2DArray.ArraySize = array_size * arrayMultiplier;
						desc.Texture2DArray.FirstArraySlice = first_array_slice * arrayMultiplier;
						desc.Texture2DArray.MipSlice = mip_slice;
					}
					else
					{
						desc.Texture2D.MipSlice = mip_slice;
					}
					break;
				case ETextureType::Texture2DMultisample:
					if constexpr (std::is_same_v<DescType, D3D12_DEPTH_STENCIL_VIEW_DESC> || std::is_same_v<DescType, D3D12_RENDER_TARGET_VIEW_DESC>)
					{
						if (p_texture->GetArraySize() > 1)
						{
							desc.Texture2DMSArray.ArraySize = array_size;
							desc.Texture2DMSArray.FirstArraySlice = first_array_slice;
						}
					}
					else
					{
						throw std::exception("Texture2DMultisample does not support UAV views");
					}
					break;
				case ETextureType::Texture3D:
					if constexpr (std::is_same_v<DescType, D3D12_UNORDERED_ACCESS_VIEW_DESC> || std::is_same_v<DescType, D3D12_RENDER_TARGET_VIEW_DESC>)
					{
						assert(p_texture->GetArraySize() == 1);
						desc.Texture3D.MipSlice = mip_slice;
						desc.Texture3D.FirstWSlice = 0;
						desc.Texture3D.WSize = p_texture->GetDepth(mip_slice);
					}
					else
					{
						throw std::exception("Texture3D does not support DSV views");
					}
					break;
				default:
					assert(false);
				}

				return desc;
			}
			// Texture Dsv用.
			D3D12_DEPTH_STENCIL_VIEW_DESC createDsvDesc(const TextureDep* p_texture, uint32_t mip_slice, uint32_t first_array_slice, uint32_t array_size)
			{
				return createDsvRtvUavDescCommon<D3D12_DEPTH_STENCIL_VIEW_DESC>(p_texture, mip_slice, first_array_slice, array_size);
			}
			// Texture Rtv用.
			D3D12_RENDER_TARGET_VIEW_DESC createRtvDesc(const TextureDep* p_texture, uint32_t mip_slice, uint32_t first_array_slice, uint32_t array_size)
			{
				return createDsvRtvUavDescCommon<D3D12_RENDER_TARGET_VIEW_DESC>(p_texture, mip_slice, first_array_slice, array_size);
			}
			// Texture Uav用.
			D3D12_UNORDERED_ACCESS_VIEW_DESC createUavDesc(const TextureDep* p_texture, uint32_t mip_slice, uint32_t first_array_slice, uint32_t array_size)
			{
				return createDsvRtvUavDescCommon<D3D12_UNORDERED_ACCESS_VIEW_DESC>(p_texture, mip_slice, first_array_slice, array_size);
			}

			// Texture Srv用.
			D3D12_SHADER_RESOURCE_VIEW_DESC createTextureSrvDesc(const TextureDep* p_texture, uint32_t mip_slice, uint32_t mip_count, uint32_t first_array_slice, uint32_t array_size)
			{
				assert(p_texture);
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};

				//If not depth, returns input format
				desc.Format = ConvertResourceFormat(depthToColorFormat(p_texture->GetFormat()));

				bool isTextureArray = p_texture->GetArraySize() > 1;
				desc.ViewDimension = getTextureViewDimension<D3D12_SRV_DIMENSION>(getTextureDimension(p_texture->GetType(), isTextureArray));

				switch (p_texture->GetType())
				{
				case ETextureType::Texture1D:
					if (isTextureArray)
					{
						desc.Texture1DArray.MipLevels = mip_count;
						desc.Texture1DArray.MostDetailedMip = mip_slice;
						desc.Texture1DArray.ArraySize = array_size;
						desc.Texture1DArray.FirstArraySlice = first_array_slice;
					}
					else
					{
						desc.Texture1D.MipLevels = mip_count;
						desc.Texture1D.MostDetailedMip = mip_slice;
					}
					break;
				case ETextureType::Texture2D:
					if (isTextureArray)
					{
						desc.Texture2DArray.MipLevels = mip_count;
						desc.Texture2DArray.MostDetailedMip = mip_slice;
						desc.Texture2DArray.ArraySize = array_size;
						desc.Texture2DArray.FirstArraySlice = first_array_slice;
					}
					else
					{
						desc.Texture2D.MipLevels = mip_count;
						desc.Texture2D.MostDetailedMip = mip_slice;
					}
					break;
				case ETextureType::Texture2DMultisample:
					if (array_size > 1)
					{
						desc.Texture2DMSArray.ArraySize = array_size;
						desc.Texture2DMSArray.FirstArraySlice = first_array_slice;
					}
					break;
				case ETextureType::Texture3D:
					assert(array_size == 1);
					desc.Texture3D.MipLevels = mip_count;
					desc.Texture3D.MostDetailedMip = mip_slice;
					break;
				case ETextureType::TextureCube:
					if (array_size > 1)
					{
						desc.TextureCubeArray.First2DArrayFace = 0;
						desc.TextureCubeArray.NumCubes = array_size;
						desc.TextureCubeArray.MipLevels = mip_count;
						desc.TextureCubeArray.MostDetailedMip = mip_slice;
					}
					else
					{
						desc.TextureCube.MipLevels = mip_count;
						desc.TextureCube.MostDetailedMip = mip_slice;
					}
					break;
				default:
					assert(false);
				}

				desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				return desc;
			}

			struct BufferViewMode
			{
				enum Type : u32
				{
					// Typed Buffer View.
					Typed,
					// Structured な buffer view.
					Structured,
					// 4 Byte per element な raw buffer view.
					ByteAddress
				};
			};

			D3D12_SHADER_RESOURCE_VIEW_DESC createBufferSrvDesc(const BufferDep* pBuffer
				// 初期化モード.
				, BufferViewMode::Type mode
				// 初期化モードTyped の場合の要素フォーマットタイプ.
				, EResourceFormat typed_format
				// 初期化モードStructured の場合の要素サイズ.
				, u32 structured_element_size
				, u32 element_offset, u32 element_count)
			{
				assert(pBuffer);
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
				u32 bufferElementCount = 0;
				if (BufferViewMode::Typed == mode)
				{
					// Typed.
					bufferElementCount = pBuffer->getElementCount();
					desc.Format = ConvertResourceFormat(typed_format);
				}
				else if (BufferViewMode::Structured == mode)
				{
					// Structured.
					bufferElementCount = pBuffer->getElementCount();
					desc.Format = DXGI_FORMAT_UNKNOWN;
					desc.Buffer.StructureByteStride = structured_element_size;
				}
				else
				{
					// ByteAddress.
					bufferElementCount = pBuffer->getElementCount();
					desc.Format = DXGI_FORMAT_R32_TYPELESS;
					desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
				}

				assert((element_offset + element_count) <= bufferElementCount); // Check range
				desc.Buffer.FirstElement = element_offset;
				desc.Buffer.NumElements = element_count;
				desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				return desc;
			}

			D3D12_UNORDERED_ACCESS_VIEW_DESC createBufferUavDesc(const BufferDep* p_buffer
				// 初期化モード.
				, BufferViewMode::Type mode
				// 初期化モードTyped の場合の要素フォーマットタイプ.
				, EResourceFormat typed_format
				// 初期化モードStructured の場合の要素サイズ.
				, u32 structured_element_size
				, u32 element_offset, u32 element_count)
			{
				assert(p_buffer);
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
				u32 bufferElementCount = 0;
				if (BufferViewMode::Typed == mode)
				{
					// Typed.
					bufferElementCount = p_buffer->getElementCount();
					desc.Format = ConvertResourceFormat(typed_format);
				}
				else if (BufferViewMode::Structured == mode)
				{
					// Structured.
					bufferElementCount = p_buffer->getElementCount();
					desc.Format = DXGI_FORMAT_UNKNOWN;
					desc.Buffer.StructureByteStride = structured_element_size;
				}
				else
				{
					// ByteAddress.
					bufferElementCount = p_buffer->getElementCount();
					desc.Format = DXGI_FORMAT_R32_TYPELESS;
					desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
				}

				assert((element_offset + element_count) <= bufferElementCount); // Check range
				desc.Buffer.FirstElement = element_offset;
				desc.Buffer.NumElements = element_count;
				desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				return desc;
			}
		}


		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		DepthStencilViewDep::DepthStencilViewDep()
		{
		}
		DepthStencilViewDep::~DepthStencilViewDep()
		{
			Finalize();
		}
		bool DepthStencilViewDep::Initialize(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 first_array_slice, u32 array_size)
		{
			InitializeRhiObject(p_device);

			assert(p_device);
			assert(p_texture);

			if (!check_bits(ResourceBindFlag::DepthStencil, p_texture->GetBindFlag()))
			{
				assert(false);
				return false;
			}

			// 専有Heap確保.
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			heap_desc.NumDescriptors = 1;
			heap_desc.NodeMask = 0;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if (FAILED(p_device->GetD3D12Device()->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&p_heap_))))
			{
				std::cout << "[ERROR] Create DescriptorHeap" << std::endl;
				return false;
			}

			// 専有HeapにDescriptor作成.
			D3D12_DEPTH_STENCIL_VIEW_DESC desc = createDsvDesc(p_texture, mip_slice, first_array_slice, array_size);
			p_device->GetD3D12Device()->CreateDepthStencilView(p_texture->GetD3D12Resource(), &desc, p_heap_->GetCPUDescriptorHandleForHeapStart());

			return true;
		}
		void DepthStencilViewDep::Finalize()
		{
			p_heap_->Release();
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		RenderTargetViewDep::RenderTargetViewDep()
		{
		}
		RenderTargetViewDep::~RenderTargetViewDep()
		{
			Finalize();
		}
		bool RenderTargetViewDep::Initialize(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 first_array_slice, u32 array_size)
		{
			InitializeRhiObject(p_device);

			assert(p_device);
			assert(p_texture);

			if (!check_bits(ResourceBindFlag::RenderTarget, p_texture->GetBindFlag()))
			{
				assert(false);
				return false;
			}

			// 専有Heap確保.
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heap_desc.NumDescriptors = 1;
			heap_desc.NodeMask = 0;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if (FAILED(p_device->GetD3D12Device()->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&p_heap_))))
			{
				std::cout << "[ERROR] Create DescriptorHeap" << std::endl;
				return false;
			}

			// 専有HeapにDescriptor作成.
			{
				D3D12_RENDER_TARGET_VIEW_DESC desc = createRtvDesc(p_texture, mip_slice, first_array_slice, array_size);
				p_device->GetD3D12Device()->CreateRenderTargetView(p_texture->GetD3D12Resource(), &desc, p_heap_->GetCPUDescriptorHandleForHeapStart());
			}

			return true;
		}
		// SwapChainからRTV作成.
		bool RenderTargetViewDep::Initialize(DeviceDep* p_device, SwapChainDep* p_swapchain, unsigned int buffer_index)
		{
			InitializeRhiObject(p_device);

			if (!p_device || !p_swapchain)
				return false;

			// 専有Heap確保.
			D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
			heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heap_desc.NumDescriptors = 1;
			heap_desc.NodeMask = 0;
			heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if (FAILED(p_device->GetD3D12Device()->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&p_heap_))))
			{
				std::cout << "[ERROR] Create DescriptorHeap" << std::endl;
				return false;
				
			}

			auto* buffer = p_swapchain->GetD3D12Resource(buffer_index);
			if (!buffer)
			{
				std::cout << "[ERROR] Invalid Buffer Index" << std::endl;
				return false;
			}

			auto handle_head = p_heap_->GetCPUDescriptorHandleForHeapStart();
			p_device->GetD3D12Device()->CreateRenderTargetView(buffer, nullptr, handle_head);


			return true;
		}
		void RenderTargetViewDep::Finalize()
		{
			p_heap_.Reset();
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		UnorderedAccessViewDep::UnorderedAccessViewDep()
		{
		}
		UnorderedAccessViewDep::~UnorderedAccessViewDep()
		{
			Finalize();
		}
		template<typename ResourceType>
		bool _Initialize(DeviceDep* p_device, const ResourceType* p_buffer, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc, PersistentDescriptorInfo& out_desc_info)
		{
			// Descriptor確保.
			auto&& descriptor_allocator = p_device->GetPersistentDescriptorAllocator();
			out_desc_info = descriptor_allocator->Allocate();
			if (!out_desc_info.IsValid())
			{
				std::cout << "[ERROR] ShaderResourceViewDep::InitializeAsStructured" << std::endl;
				assert(false);
				return false;
			}
			constexpr ID3D12Resource* p_counter_resource = nullptr;
			p_device->GetD3D12Device()->CreateUnorderedAccessView(p_buffer->GetD3D12Resource(), p_counter_resource, &desc, out_desc_info.cpu_handle);

			return true;
		}
		// TextureのView.
		bool UnorderedAccessViewDep::Initialize(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 first_array_slice, u32 array_size)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_texture);
			if (!check_bits(ResourceBindFlag::UnorderedAccess, p_texture->GetBindFlag()))
			{
				assert(false);
				return false;
			}

			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = createUavDesc(p_texture, mip_slice, first_array_slice, array_size);
			return _Initialize(p_device, p_texture, desc, view_);
		}
		// BufferのStructuredBufferView.
		bool UnorderedAccessViewDep::InitializeAsStructured(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_size, u32 element_offset, u32 element_count)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_buffer);
			if (!check_bits(ResourceBindFlag::UnorderedAccess, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = createBufferUavDesc(p_buffer, BufferViewMode::Structured, {}, element_size, element_offset, element_count);
			return _Initialize(p_device, p_buffer, desc, view_);
		}
		// BufferのTypedBufferView.
		bool UnorderedAccessViewDep::InitializeAsTyped(DeviceDep* p_device, const BufferDep* p_buffer, EResourceFormat format, u32 element_offset, u32 element_count)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_buffer);
			if (!check_bits(ResourceBindFlag::UnorderedAccess, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = createBufferUavDesc(p_buffer, BufferViewMode::Typed, format, {}, element_offset, element_count);
			return _Initialize(p_device, p_buffer, desc, view_);
		}
		// BufferのByteAddressBufferView.
		bool UnorderedAccessViewDep::InitializeAsRaw(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_offset, u32 element_count)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_buffer);
			if (!check_bits(ResourceBindFlag::UnorderedAccess, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = createBufferUavDesc(p_buffer, BufferViewMode::ByteAddress, {}, {}, element_offset, element_count);
			return _Initialize(p_device, p_buffer, desc, view_);
		}

		void UnorderedAccessViewDep::Finalize()
		{
			auto&& descriptor_allocator = view_.allocator;
			if (descriptor_allocator)
			{
				descriptor_allocator->Deallocate(view_);
			}
			view_ = {};
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------
		// -------------------------------------------------------------------------------------------------------------------------------------------------
		ShaderResourceViewDep::ShaderResourceViewDep()
		{
		}
		ShaderResourceViewDep::~ShaderResourceViewDep()
		{
			Finalize();
		}
		template<typename ResourceType>
		bool _Initialize(DeviceDep* p_device, const ResourceType* p_buffer, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc, PersistentDescriptorInfo& out_desc_info)
		{
			// Descriptor確保.
			auto&& descriptor_allocator = p_device->GetPersistentDescriptorAllocator();
			out_desc_info = descriptor_allocator->Allocate();
			if (!out_desc_info.IsValid())
			{
				std::cout << "[ERROR] ShaderResourceViewDep::InitializeAsStructured" << std::endl;
				assert(false);
				return false;
			}

			if (D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE == desc.ViewDimension)
			{
				// ASの場合はCreateShaderResourceViewの第一引数はnullptrで, descのメンバでGPU Addressが指定される.
				p_device->GetD3D12Device()->CreateShaderResourceView(nullptr, &desc, out_desc_info.cpu_handle);
			}
			else
			{
				assert(p_buffer);
				p_device->GetD3D12Device()->CreateShaderResourceView(p_buffer->GetD3D12Resource(), &desc, out_desc_info.cpu_handle);
			}
			return true;
		}
		// TextureのView.
		bool ShaderResourceViewDep::InitializeAsTexture(DeviceDep* p_device, const TextureDep* p_texture, u32 mip_slice, u32 mip_count, u32 first_array_slice, u32 array_size)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_texture);
			if (!p_device || !p_texture)
				return false;

			if (!check_bits(ResourceBindFlag::ShaderResource, p_texture->GetBindFlag()))
			{
				assert(false);
				return false;
			}

			// clamp mip, array range.
			if (mip_slice >= p_texture->GetMipCount())
			{
				mip_slice = p_texture->GetMipCount() - 1;
				mip_count = 1;
			}
			else if ((mip_count == 0) || (mip_count > p_texture->GetMipCount() - mip_slice))
			{
				mip_count = p_texture->GetMipCount() - mip_slice;
			}
			if (first_array_slice >= p_texture->GetArraySize())
			{
				first_array_slice = p_texture->GetArraySize() - 1;
				array_size = 1;
			}
			else if ((array_size == 0) || (array_size > p_texture->GetArraySize() - first_array_slice))
			{
				array_size = p_texture->GetArraySize() - first_array_slice;
			}

			// Desc生成.
			auto desc = createTextureSrvDesc(p_texture, mip_slice, mip_count, first_array_slice, array_size);
			return _Initialize(p_device, p_texture, desc, view_);
		}

		// BufferのStructuredBufferView.
		bool ShaderResourceViewDep::InitializeAsStructured(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_size, u32 element_offset, u32 element_count)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_buffer);
			if (!p_device || !p_buffer)
				return false;
			if (!check_bits(ResourceBindFlag::ShaderResource, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			D3D12_SHADER_RESOURCE_VIEW_DESC desc = createBufferSrvDesc(p_buffer, BufferViewMode::Structured, {}, element_size, element_offset, element_count);
			return _Initialize(p_device, p_buffer, desc, view_);
		}
		// BufferのTypedBufferView.
		bool ShaderResourceViewDep::InitializeAsTyped(DeviceDep* p_device, const BufferDep* p_buffer, EResourceFormat format, u32 element_offset, u32 element_count)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_buffer);
			if (!p_device || !p_buffer)
				return false;
			if (!check_bits(ResourceBindFlag::ShaderResource, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			D3D12_SHADER_RESOURCE_VIEW_DESC desc = createBufferSrvDesc(p_buffer, BufferViewMode::Typed, format, {}, element_offset, element_count);
			return _Initialize(p_device, p_buffer, desc, view_);
		}
		// BufferのByteAddressBufferView.
		bool ShaderResourceViewDep::InitializeAsRaw(DeviceDep* p_device, const BufferDep* p_buffer, u32 element_offset, u32 element_count)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_buffer);
			if (!p_device || !p_buffer)
				return false;
			if (!check_bits(ResourceBindFlag::ShaderResource, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			D3D12_SHADER_RESOURCE_VIEW_DESC desc = createBufferSrvDesc(p_buffer, BufferViewMode::ByteAddress, {}, {}, element_offset, element_count);
			return _Initialize(p_device, p_buffer, desc, view_);
		}
		// BufferのRaytracingAccelerationStructureView.
		bool ShaderResourceViewDep::InitializeAsRaytracingAccelerationStructure(DeviceDep* p_device, const BufferDep* p_buffer)
		{
			InitializeRhiObject(p_device);

			assert(p_device && p_buffer);
			if (!p_device || !p_buffer)
				return false;
			if (!check_bits(ResourceBindFlag::ShaderResource, p_buffer->GetDesc().bind_flag))
			{
				assert(false);
				return false;
			}

			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;// RaytracingAS.
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.RaytracingAccelerationStructure.Location = p_buffer->GetD3D12Resource()->GetGPUVirtualAddress();// RaytracingASの場合はdescにGPU Address指定する.
			return _Initialize(p_device, p_buffer, desc, view_);
		}

		void ShaderResourceViewDep::Finalize()
		{
			auto&& descriptor_allocator = view_.allocator;
			if (descriptor_allocator)
			{
				descriptor_allocator->Deallocate(view_);
			}
			view_ = {};
		}

		// -------------------------------------------------------------------------------------------------------------------------------------------------

	}
}