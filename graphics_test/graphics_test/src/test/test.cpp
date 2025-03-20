
#include <iostream>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>
#include <thread>


#include "ngl/util/time/timer.h"

#include "ngl/math/math.h"

// resource
#include "ngl/resource/resource_manager.h"

// rhi
#include "test.h"

#include "ngl/rhi/d3d12/device.d3d12.h"
#include "ngl/rhi/d3d12/command_list.d3d12.h"
#include "ngl/rhi/d3d12/descriptor.d3d12.h"
#include "ngl/rhi/d3d12/resource.d3d12.h"
#include "ngl/rhi/d3d12/resource_view.d3d12.h"


#include "ngl/thread/lockfree_stack_intrusive.h"
#include "ngl/thread/lockfree_stack_intrusive_test.h"




namespace ngl_test
{

	// 機能テスト用.
	void TestFunc00(ngl::rhi::DeviceDep* p_device)
	{
#if defined(_DEBUG) && false
		auto& device = *p_device;
		auto& ResourceMan = ngl::res::ResourceManager::Instance();

		ngl::text::FixedString<8> fixed_text0("abc");
		ngl::text::FixedString<8> fixed_text1("abcd");
		ngl::text::FixedString<8> fixed_text2("01234567");
		ngl::text::FixedString<8> fixed_text3("012345678");

		std::unordered_map<ngl::text::FixedString<8>, int> fixstr_map;
		auto in0 = fixstr_map.insert(std::pair<ngl::text::FixedString<8>, int>(fixed_text0, 0));
		auto in1 = fixstr_map.insert(std::pair<ngl::text::FixedString<8>, int>(fixed_text1, 1));
		auto in2 = fixstr_map.insert(std::pair<ngl::text::FixedString<8>, int>(fixed_text2, 2));
		auto in3 = fixstr_map.insert(std::pair<ngl::text::FixedString<8>, int>(fixed_text3, 3));

		auto find0 = fixstr_map.find("");
		auto find1 = fixstr_map.find("a");
		auto find2 = fixstr_map.find("abc");
		auto find3 = fixstr_map.find("01234567");
		auto find4 = fixstr_map.find("012345678");



		ngl::text::HashText<8> fixstr00("abc");
		constexpr ngl::text::HashText<8> fixstr01("abc");
		constexpr ngl::text::HashText<8> fixstr02("abc");
		constexpr ngl::text::HashText<8> fixstr03("abd");
		constexpr ngl::text::HashText<16> fixstr04("abc");
		constexpr ngl::text::HashText<16> fixstr05("abcde");

		constexpr bool comp00 = (fixstr01 == fixstr02);
		constexpr bool comp01 = (fixstr01 == fixstr03);
		constexpr bool comp02 = (fixstr01 == fixstr04);
		constexpr bool comp03 = (fixstr01 == fixstr05);
		constexpr bool comp03_ = (fixstr01 != fixstr05);

		const bool comp04 = (fixstr00 == fixstr01);
		const bool comp05 = (fixstr01 == fixstr02);
		const bool comp06 = (fixstr01 == fixstr05);

		//using FixedStr32 = ngl::text::FixedString<32>;
		using FixedStr32 = ngl::text::HashText<32>;
		std::unordered_map<FixedStr32, int> map00;
		map00["fdafda"] = 1;
		map00["fdafda"] = 2;

		const char* adsfadfasf = "fdafda";
		auto finditr00 = map00.find(adsfadfasf);



		ngl::math::funcAA();

		if (true)
		{
			bool is_uav_test = true;

			// Buffer生成テスト
			ngl::rhi::BufferDep buffer0;
			ngl::rhi::BufferDep::Desc buffer_desc0 = {};
			buffer_desc0.element_byte_size = sizeof(ngl::u64);
			buffer_desc0.bind_flag = (int)ngl::rhi::ResourceBindFlag::ShaderResource;
			buffer_desc0.element_count = 1;
			if (is_uav_test)
			{
				// UAV用設定.
				buffer_desc0.bind_flag |= ngl::rhi::ResourceBindFlag::UnorderedAccess;
				// UAVはDefaultHeap必須
				buffer_desc0.heap_type = ngl::rhi::EResourceHeapType::Default;
				buffer_desc0.initial_state = ngl::rhi::EResourceState::Common;// 通常はCommon.
			}
			else
			{
				// CPU->GPU Uploadリソース
				buffer_desc0.heap_type = ngl::rhi::EResourceHeapType::Upload;
				buffer_desc0.initial_state = ngl::rhi::EResourceState::General;// UploadヒープではGeneral.
			}

			if (!buffer0.Initialize(&device, buffer_desc0))
			{
				std::cout << "[ERROR] Create rhi::Buffer" << std::endl;
				assert(false);
			}

			if (auto* buffer0_map = reinterpret_cast<ngl::u64*>(buffer0.Map()))
			{
				*buffer0_map = 111u;
				buffer0.Unmap();
			}

			auto* persistent_desc_allocator = device.GetPersistentDescriptorAllocator();

			// SRV生成テスト. persistent上に作成.
			auto pd_srv = persistent_desc_allocator->Allocate();
			if (pd_srv.allocator)
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
				srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv_desc.Format = DXGI_FORMAT_UNKNOWN;
				srv_desc.Buffer.FirstElement = 0;
				srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
				srv_desc.Buffer.NumElements = 1;
				srv_desc.Buffer.StructureByteStride = sizeof(ngl::u64);

				device.GetD3D12Device()->CreateShaderResourceView(buffer0.GetD3D12Resource(), &srv_desc, pd_srv.cpu_handle);

				// 解放
				persistent_desc_allocator->Deallocate(pd_srv);
			}
			// UAV生成テスト. persistent上に作成.
			if (is_uav_test)
			{
				auto pd_uav = persistent_desc_allocator->Allocate();
				if (pd_uav.allocator)
				{
					D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
					uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
					uav_desc.Format = DXGI_FORMAT_UNKNOWN;
					uav_desc.Buffer.CounterOffsetInBytes = 0;// カウンタバッファ不使用の場合はゼロ
					uav_desc.Buffer.FirstElement = 0;
					uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
					uav_desc.Buffer.NumElements = 1;
					uav_desc.Buffer.StructureByteStride = sizeof(ngl::u64);

					device.GetD3D12Device()->CreateUnorderedAccessView(buffer0.GetD3D12Resource(), nullptr, &uav_desc, pd_uav.cpu_handle);

					// 解放
					persistent_desc_allocator->Deallocate(pd_uav);
				}
			}
		}

		if (true)
		{
			// Textureテスト
			ngl::rhi::TextureDep::Desc tex_desc00 = {};
			tex_desc00.heap_type = ngl::rhi::EResourceHeapType::Default;
			tex_desc00.type = ngl::rhi::ETextureType::Texture3D;
			tex_desc00.width = 64;
			tex_desc00.height = 64;
			tex_desc00.depth = 64;
			tex_desc00.format = ngl::rhi::EResourceFormat::Format_R16_FLOAT;
			tex_desc00.bind_flag = (int)ngl::rhi::ResourceBindFlag::ShaderResource;

			//tex_desc00.bind_flag |= ngl::rhi::ResourceBindFlag::RenderTarget;
			tex_desc00.bind_flag |= ngl::rhi::ResourceBindFlag::UnorderedAccess;// UAV

			ngl::rhi::TextureDep tex00;
			if (!tex00.Initialize(&device, tex_desc00))
			{
				std::cout << "[ERROR] Create rhi::TextureDep" << std::endl;
				assert(false);
			}
		}

		if (true)
		{
			// Samplerテスト
			ngl::rhi::SamplerDep::Desc samp_desc = {};
			samp_desc.AddressU = ngl::rhi::ETextureAddressMode::Repeat;
			samp_desc.AddressV = ngl::rhi::ETextureAddressMode::Repeat;
			samp_desc.AddressW = ngl::rhi::ETextureAddressMode::Repeat;
			samp_desc.BorderColor[0] = 0.0f;
			samp_desc.BorderColor[1] = 0.0f;
			samp_desc.BorderColor[2] = 0.0f;
			samp_desc.BorderColor[3] = 0.0f;
			samp_desc.ComparisonFunc = ngl::rhi::ECompFunc::Never;
			samp_desc.Filter = ngl::rhi::ETextureFilterMode::Min_Point_Mag_Point_Mip_Linear;
			samp_desc.MaxAnisotropy = 0;
			samp_desc.MaxLOD = FLT_MAX;
			samp_desc.MinLOD = 0.0f;
			samp_desc.MipLODBias = 0.0f;

			// ダミー用Descriptorの1個分を除いた最大数まで確保するテスト.
			ngl::rhi::SamplerDep samp[1];
			for (auto&& e : samp)
			{
				if (!e.Initialize(&device, samp_desc))
				{
					std::cout << "[ERROR] Create rhi::SamplerDep" << std::endl;
					assert(false);
				}
			}
		}

		if (true)
		{


			ngl::res::ResourceHandle <ngl::gfx::ResShader> res_shader_sample_vs_;
			ngl::res::ResourceHandle <ngl::gfx::ResShader> res_shader_sample_ps_;
			{
				ngl::gfx::ResShader::LoadDesc loaddesc = {};
				loaddesc.entry_point_name = "main_vs";
				loaddesc.stage = ngl::rhi::EShaderStage::Vertex;
				loaddesc.shader_model_version = "6_3";
				res_shader_sample_vs_ = ResourceMan.LoadResource<ngl::gfx::ResShader>(&device, "./src/ngl/data/shader/sample_vs.hlsl", &loaddesc);
			}
			{
				ngl::gfx::ResShader::LoadDesc loaddesc = {};
				loaddesc.entry_point_name = "main_ps";
				loaddesc.stage = ngl::rhi::EShaderStage::Pixel;
				loaddesc.shader_model_version = "6_3";
				res_shader_sample_ps_ = ResourceMan.LoadResource<ngl::gfx::ResShader>(&device, "./src/ngl/data/shader/sample_ps.hlsl", &loaddesc);
			}


			// PSO
			{
				ngl::rhi::GraphicsPipelineStateDep pso;

				ngl::rhi::GraphicsPipelineStateDep::Desc desc = {};
				desc.vs = &res_shader_sample_vs_->data_;
				desc.ps = &res_shader_sample_ps_->data_;

				desc.num_render_targets = 1;
				desc.render_target_formats[0] = ngl::rhi::EResourceFormat::Format_R10G10B10A2_UNORM;

				desc.depth_stencil_state.depth_enable = false;
				desc.depth_stencil_state.stencil_enable = false;

				desc.blend_state.target_blend_states[0].blend_enable = false;;

				// 入力レイアウト
				std::array<ngl::rhi::InputElement, 2> input_elem_data;
				desc.input_layout.num_elements = static_cast<ngl::u32>(input_elem_data.size());
				desc.input_layout.p_input_elements = input_elem_data.data();
				input_elem_data[0].semantic_name = "POSITION";
				input_elem_data[0].semantic_index = 0;
				input_elem_data[0].format = ngl::rhi::EResourceFormat::Format_R32G32B32A32_FLOAT;
				input_elem_data[0].stream_slot = 0;
				input_elem_data[0].element_offset = 0;
				input_elem_data[1].semantic_name = "TEXCOORD";
				input_elem_data[1].semantic_index = 0;
				input_elem_data[1].format = ngl::rhi::EResourceFormat::Format_R32G32_FLOAT;
				input_elem_data[1].stream_slot = 0;
				input_elem_data[1].element_offset = sizeof(float) * 4;

				if (!pso.Initialize(&device, desc))
				{
					std::cout << "[ERROR] Create rhi::GraphicsPipelineState" << std::endl;
					assert(false);
				}

				// DescriptorSet設定テスト
				{
					// Buffer生成テスト
					struct CbTest
					{
						float cb_param0 = 1.111f;
						ngl::u32 cb_param1 = 0;
					};
					ngl::rhi::BufferDep buffer0;
					ngl::rhi::BufferDep::Desc buffer_desc0 = {};
					buffer_desc0.element_byte_size = sizeof(CbTest);
					buffer_desc0.element_count = 1;
					buffer_desc0.bind_flag = (int)ngl::rhi::ResourceBindFlag::ConstantBuffer;
					buffer_desc0.initial_state = ngl::rhi::EResourceState::General;
					// CPU->GPU Uploadリソース
					buffer_desc0.heap_type = ngl::rhi::EResourceHeapType::Upload;

					if (!buffer0.Initialize(&device, buffer_desc0))
					{
						std::cout << "[ERROR] Create rhi::Buffer" << std::endl;
						assert(false);
					}

					if (auto* buffer0_map = reinterpret_cast<CbTest*>(buffer0.Map()))
					{
						buffer0_map->cb_param0 = 2.0f;
						buffer0_map->cb_param1 = 1;
						buffer0.Unmap();
					}

					auto* persistent_desc_allocator = device.GetPersistentDescriptorAllocator();
					// CBV生成テスト. persistent上に作成.
					auto pd_cbv = persistent_desc_allocator->Allocate();
					if (pd_cbv.allocator)
					{
						D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
						cbv_desc.BufferLocation = buffer0.GetD3D12Resource()->GetGPUVirtualAddress();
						cbv_desc.SizeInBytes = buffer0.GetBufferSize();
						device.GetD3D12Device()->CreateConstantBufferView(&cbv_desc, pd_cbv.cpu_handle);

					}
					// 一応解放しておく
					persistent_desc_allocator->Deallocate(pd_cbv);
				}
			}
		}

		// シェーダテスト
		{
#if 0
			// バイナリ読み込み.
			{
				ngl::file::FileObject file_obj;
				ngl::rhi::ShaderDep	shader00;
				ngl::rhi::ShaderReflectionDep reflect00;
				file_obj.ReadFile("./data/sample_vs.cso");
				if (!shader00.Initialize(&device, ngl::rhi::ShaderStage::Vertex, file_obj.GetFileData(), file_obj.GetFileSize()))
				{
					std::cout << "[ERROR] Create rhi::ShaderDep" << std::endl;
					assert(false);
				}
				reflect00.Initialize(&device, &shader00);
			}
			// バイナリ読み込み.
			{
				ngl::file::FileObject file_obj;
				ngl::rhi::ShaderDep	shader01;
				ngl::rhi::ShaderReflectionDep reflect00;
				file_obj.ReadFile("./data/sample_ps.cso");
				if (!shader01.Initialize(&device, ngl::rhi::ShaderStage::Pixel, file_obj.GetFileData(), file_obj.GetFileSize()))
				{
					std::cout << "[ERROR] Create rhi::ShaderDep" << std::endl;
					assert(false);
				}
				reflect00.Initialize(&device, &shader01);
			}
#endif

			// HLSLからコンパイルして初期化.
			ngl::rhi::ShaderDep shader00;
			ngl::rhi::ShaderReflectionDep reflect00;
			{
				ngl::rhi::ShaderDep::InitFileDesc shader_desc = {};
				shader_desc.shader_file_path = "./src/ngl/data/shader/test/sample_ps.hlsl";
				shader_desc.entry_point_name = "main_ps";
				shader_desc.stage = ngl::rhi::EShaderStage::Pixel;
				shader_desc.shader_model_version = "5_0";

				if (!shader00.Initialize(&device, shader_desc))
				{
					std::cout << "[ERROR] Create rhi::ShaderDep" << std::endl;
					assert(false);
				}
				reflect00.Initialize(&device, &shader00);

				auto cb0 = reflect00.GetCbInfo(0);
				auto cb1 = reflect00.GetCbInfo(1);
				auto cb0_var0 = reflect00.GetCbVariableInfo(0, 0);
				auto cb0_var1 = reflect00.GetCbVariableInfo(0, 1);
				auto cb0_var2 = reflect00.GetCbVariableInfo(0, 2);
				auto cb1_var0 = reflect00.GetCbVariableInfo(1, 0);

				float default_value;
				reflect00.GetCbDefaultValue(0, 0, default_value);
				std::cout << "cb default value " << default_value << std::endl;
			}
			// HLSLからコンパイルして初期化.
			ngl::rhi::ShaderDep shader01;
			ngl::rhi::ShaderReflectionDep reflect01;
			{
				ngl::rhi::ShaderDep::InitFileDesc shader_desc = {};
				shader_desc.shader_file_path = "./src/ngl/data/shader/test/sample_ps.hlsl";
				shader_desc.entry_point_name = "main_ps";
				shader_desc.stage = ngl::rhi::EShaderStage::Pixel;
				shader_desc.shader_model_version = "6_0";

				if (!shader01.Initialize(&device, shader_desc))
				{
					std::cout << "[ERROR] Create rhi::ShaderDep" << std::endl;
					assert(false);
				}
				reflect01.Initialize(&device, &shader01);

				float default_value;
				reflect01.GetCbDefaultValue(0, 0, default_value);
				std::cout << "cb default value " << default_value << std::endl;
			}

			// HLSLからコンパイルして初期化.
			ngl::rhi::ShaderDep shader02;
			ngl::rhi::ShaderReflectionDep reflect02;
			{
				ngl::rhi::ShaderDep::InitFileDesc shader_desc = {};
				shader_desc.shader_file_path = "./src/ngl/data/shader/sample_vs.hlsl";
				shader_desc.entry_point_name = "main_vs";
				shader_desc.stage = ngl::rhi::EShaderStage::Vertex;
				shader_desc.shader_model_version = "5_0";

				if (!shader02.Initialize(&device, shader_desc))
				{
					std::cout << "[ERROR] Create rhi::ShaderDep" << std::endl;
					assert(false);
				}
				reflect02.Initialize(&device, &shader02);

				float default_value;
				reflect02.GetCbDefaultValue(0, 0, default_value);
				std::cout << "cb default value " << default_value << std::endl;
			}

		}

		// PersistentDescriptorAllocatorテスト. 確保と解放を繰り返すテスト.
		{
			ngl::time::Timer::Instance().StartTimer("PersistentDescriptorAllocatorTest");

			ngl::rhi::PersistentDescriptorAllocator* persistent_desc_allocator = device.GetPersistentDescriptorAllocator();

			std::vector<ngl::rhi::PersistentDescriptorInfo> debug_alloc_pd;
			for (ngl::u32 i = 0u; i < (100000); ++i)
			{
				auto pd0 = persistent_desc_allocator->Allocate();

				if (0 != pd0.allocator)
				{
					debug_alloc_pd.push_back(pd0);
				}

				const auto dealloc_index = std::rand() % debug_alloc_pd.size();
#if 1
				if (debug_alloc_pd[dealloc_index].allocator)
				{
					// ランダムに選んだものがまだDeallocされていなければDealloc
					persistent_desc_allocator->Deallocate(debug_alloc_pd[dealloc_index]);
					debug_alloc_pd[dealloc_index] = {};
				}
#endif
			}

			std::cout << "PersistentDescriptorAllocatorTest time -> " << ngl::time::Timer::Instance().GetElapsedSec("PersistentDescriptorAllocatorTest") << std::endl;
		}
		
		if (false)
		{
			ngl::thread::test::LockfreeStackIntrusiveTest();
		}


		constexpr auto ce_str = ConstexprString("abc");
		const auto ce_char = ce_str.buf;
		constexpr auto test_count = ce_str.k_length;
		
#endif
	}
}
