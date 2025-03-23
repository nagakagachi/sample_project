#pragma once

#include <memory>

#include "rhi/d3d12/device.d3d12.h"

#include "DirectXTex/DirectXTex.h"

namespace ngl
{
namespace directxtex
{
	
	bool LoadImageData_DDS(DirectX::ScratchImage& image_data, DirectX::TexMetadata& meta_data, rhi::DeviceDep* p_device, const char* filename);
	bool LoadImageData_WIC(DirectX::ScratchImage& image_data, DirectX::TexMetadata& meta_data, rhi::DeviceDep* p_device, const char* filename);
	
}
}