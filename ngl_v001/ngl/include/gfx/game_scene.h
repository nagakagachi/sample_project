#pragma once

#include <memory>

#include "framework/gfx_scene.h"
#include "util/noncopyable.h"
#include "math/math.h"
#include "render/standard_render_model.h"

#include "resource/resource_mesh.h"

namespace ngl
{
namespace gfx
{
	// 簡易シーン.
	class SceneRepresentation
	{
	public:
		SceneRepresentation() {}
		~SceneRepresentation() {}


		// GfxScene
		fwk::GfxScene*							gfx_scene_{};
		// GfxSceneEntityMesh-Proxy ID array.
		std::vector<fwk::GfxSceneEntityId>		mesh_proxy_id_array_ = {};
		
		// Gfx SceneSkyBox.
		fwk::GfxSceneEntityId					skybox_proxy_id_{};
		
		enum class EDebugMode
		{
			None,
			SrcCubemap,
			IblSpecular,
			IblDiffuse,

			_MAX
		};
		EDebugMode sky_debug_mode_ = EDebugMode::None;
		float sky_debug_mip_bias_ = 0.0f;
	};

}
}
