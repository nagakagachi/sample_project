#pragma once

#include <vector>

#include "math/math.h"
#include "util/noncopyable.h"
#include "util/singleton.h"


#include "rhi/d3d12/device.d3d12.h"
#include "rhi/d3d12/shader.d3d12.h"

#include "resource/resource.h"

namespace ngl
{
	namespace res
	{
		class IResourceRenderUpdater;
	}


	namespace gfx
	{
		// Mesh Resource 実装.
		class ResShader : public res::Resource
		{
			NGL_RES_MEMBER_DECLARE(ResShader)

		public:
			struct LoadDesc
			{
				const char* entry_point_name = nullptr;
				// シェーダステージ.
				rhi::EShaderStage		stage = rhi::EShaderStage::Vertex;
				// シェーダモデル文字列.
				// "4_0", "5_0", "5_1" etc.
				const char* shader_model_version = nullptr;
			};

			ResShader()
			{
			}
			~ResShader()
			{
			}

			void RenderThreadInitialize(rhi::DeviceDep* p_device, rhi::GraphicsCommandListDep* p_commandlist) override
			{
			}

			rhi::ShaderDep data_ = {};
		};
	}
}
