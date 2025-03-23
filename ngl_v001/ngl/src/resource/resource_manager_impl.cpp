
#include "resource/resource_manager.h"

// マテリアルテクスチャパスの有効チェック等用.
#include <filesystem>

#include "gfx/mesh_loader_assimp.h"
#include "gfx/texture_loader_directxtex.h"

namespace ngl
{
namespace res
{
	// Shader Load 実装部.
	bool ResourceManager::LoadResourceImpl(rhi::DeviceDep* p_device, gfx::ResShader* p_res, gfx::ResShader::LoadDesc* p_desc)
	{
		ngl::rhi::ShaderDep::InitFileDesc desc = {};
		desc.entry_point_name = p_desc->entry_point_name;
		desc.shader_file_path = p_res->GetFileName();
		desc.shader_model_version = p_desc->shader_model_version;
		desc.stage = p_desc->stage;
		if (!p_res->data_.Initialize(p_device, desc))
		{
			assert(false);
			return false;
		}

		return true;
	}
	// Mesh Load 実装部.
	bool ResourceManager::LoadResourceImpl(rhi::DeviceDep* p_device, gfx::ResMeshData* p_res, gfx::ResMeshData::LoadDesc* p_desc)
	{
		// glTFなどに含まれるマテリアル情報も読み取り. テクスチャの読み込みは別途にするか.
		std::vector<assimp::MaterialTextureSet> material_array = {};
		std::vector<int> shape_material_index_array = {};
		const bool result_load_mesh = assimp::LoadMeshData(p_res->data_, material_array, shape_material_index_array, p_device, p_res->GetFileName());
		if(!result_load_mesh)
			return false;

		
		// -------------------------------------------------------------------------
		// Material情報.
			p_res->shape_material_index_array_.resize(shape_material_index_array.size());
			p_res->material_data_array_.resize(material_array.size());
			for(int i = 0; i < shape_material_index_array.size(); ++i)
			{
				p_res->shape_material_index_array_[i] = shape_material_index_array[i];
			}

			auto FindDirPathLenght = [](const char* file_path)
			{
				const int base_file_len = static_cast<int>(strlen(file_path));
				for(int i = base_file_len-1; i >= 0; --i)
				{
					if(file_path[i] == '/' || file_path[i] == '\\')
						return i;
				}
				return 0;
			};

			const int dir_path_length = FindDirPathLenght(p_res->GetFileName());
			if(0 >= dir_path_length)
			{
				assert(false);
				return false;
			}
			std::string dir_path = std::string(p_res->GetFileName(), dir_path_length);

			for(int i = 0; i < material_array.size(); ++i)
			{
				const std::string path_part = dir_path + '/';
				auto SetValidTexturePath = [path_part](const std::string& tex_name) -> std::string
				{
					if(0 < tex_name.length())
					{
						std::filesystem::path tex_path = tex_name;
						if(std::filesystem::path(tex_name).is_relative())
						{
							// 相対パスの仮解決.
							tex_path = path_part + tex_name;
						}
						
						{
							// パスとして無効な場合.
							if(!exists(tex_path))
							{
								return "";// 現状は無効パスとして返す. 相対パスからなんとかして解決してもいいかもしれないが....
							}
						}
						
						return tex_path.string();
					}
					return {};
				};
				
				p_res->material_data_array_[i].tex_basecolor = SetValidTexturePath(material_array[i].tex_base_color).c_str();
				p_res->material_data_array_[i].tex_normal = SetValidTexturePath(material_array[i].tex_normal).c_str();
				p_res->material_data_array_[i].tex_occlusion = SetValidTexturePath(material_array[i].tex_occlusion).c_str();
				p_res->material_data_array_[i].tex_roughness = SetValidTexturePath(material_array[i].tex_roughness).c_str();
				p_res->material_data_array_[i].tex_metalness = SetValidTexturePath( material_array[i].tex_metalness).c_str();
			}

		return true;
	}

	// Texture Load 実装部.
	bool ResourceManager::LoadResourceImpl(rhi::DeviceDep* p_device, gfx::ResTexture* p_res, gfx::ResTexture::LoadDesc* p_desc)
	{
		rhi::TextureDep::Desc load_img_desc = {};
		
		if(p_desc && gfx::ResTexture::ECreateMode::FROM_FILE ==  p_desc->mode)
		{
			// Fileロード.
			
			DirectX::ScratchImage image_data;// ピクセルデータ.
			DirectX::TexMetadata meta_data;

			bool is_dds = true;
			{
				const auto* file_name = p_res->GetFileName();
				const auto file_name_lenght = strlen(file_name);
				if(4 < file_name_lenght)
				{
					is_dds = (0==strncmp((file_name + file_name_lenght-4), ".dds", 4));
				}
			}

			if(is_dds)
			{
				// DDS ロード.
				if(!directxtex::LoadImageData_DDS(image_data, meta_data, p_device, p_res->GetFileName()))
					return false;
			}
			else
			{
				// WIC ロード.
				if(!directxtex::LoadImageData_WIC(image_data, meta_data, p_device, p_res->GetFileName()))
					return false;
			}

			const rhi::EResourceFormat image_format = rhi::ConvertResourceFormat(meta_data.format);

			// Texture Desc セットアップ.
			{
				load_img_desc.format = rhi::ConvertResourceFormat(meta_data.format);
				load_img_desc.width = static_cast<u32>(meta_data.width);
				load_img_desc.height = static_cast<u32>(meta_data.height);
				load_img_desc.depth = static_cast<u32>(meta_data.depth);
				{
					if(meta_data.IsCubemap())
						load_img_desc.type = rhi::ETextureType::TextureCube;
					else if(DirectX::TEX_DIMENSION_TEXTURE1D == meta_data.dimension)
						load_img_desc.type = rhi::ETextureType::Texture1D;
					else if(DirectX::TEX_DIMENSION_TEXTURE2D == meta_data.dimension)
						load_img_desc.type = rhi::ETextureType::Texture2D;
					else if(DirectX::TEX_DIMENSION_TEXTURE3D == meta_data.dimension)
						load_img_desc.type = rhi::ETextureType::Texture3D;
				}
				load_img_desc.array_size = static_cast<s32>(meta_data.arraySize);
				load_img_desc.mip_count = static_cast<s32>(meta_data.mipLevels);
			}
			
			// Upload用CPU側データ.
			{
				// メモリ確保.
				p_res->upload_pixel_memory_.resize(image_data.GetPixelsSize());
				// ピクセルデータを作業用にコピー.
				memcpy(p_res->upload_pixel_memory_.data(), image_data.GetPixels(), image_data.GetPixelsSize());

				// Sliceのデータ.
				p_res->upload_subresource_info_array.resize(image_data.GetImageCount());
				// 適切なSubresource情報収集.
				// Cubemapの場合は面1つがarray要素1つに対応するため, array sizeは6の倍数.
				for(int array_index = 0; array_index < meta_data.arraySize; ++array_index)
				{
					for(int mip_index = 0; mip_index < meta_data.mipLevels; ++mip_index)
					{
						for(int depth_index = 0; depth_index < meta_data.depth; ++depth_index)
						{
							const auto& image_plane = *image_data.GetImage(mip_index, array_index, depth_index);
						
							const auto subresource_index = meta_data.CalculateSubresource(mip_index, array_index, depth_index);

							auto& image_plane_data = p_res->upload_subresource_info_array[subresource_index];
							{
								image_plane_data.array_index = array_index;
								image_plane_data.mip_index = mip_index;
								image_plane_data.slice_index = depth_index;
						
								image_plane_data.format = image_format;
								image_plane_data.width = static_cast<s32>(image_plane.width);
								image_plane_data.height = static_cast<s32>(image_plane.height);
								image_plane_data.rowPitch = static_cast<s32>(image_plane.rowPitch);
								image_plane_data.slicePitch = static_cast<s32>(image_plane.slicePitch);
								// 作業メモリ上の位置をセット.
								image_plane_data.pixels = p_res->upload_pixel_memory_.data() + std::distance(image_data.GetPixels(), image_plane.pixels);
							}
						}
					}
				}
			}
		}
		else if(p_desc && gfx::ResTexture::ECreateMode::FROM_DESC ==  p_desc->mode)
		{
			// Load Descから生成.
			
			// Texture Desc セットアップ.
			{
				load_img_desc.format = p_desc->from_desc.format;
				load_img_desc.width = p_desc->from_desc.width;
				load_img_desc.height = p_desc->from_desc.height;
				load_img_desc.depth = p_desc->from_desc.depth;
				load_img_desc.type = p_desc->from_desc.type;
				load_img_desc.array_size = p_desc->from_desc.array_size;
				load_img_desc.mip_count = p_desc->from_desc.mip_count;
			}

			// Upload用のイメージデータを移譲.
			p_res->upload_pixel_memory_ = std::move(p_desc->from_desc.upload_pixel_memory_);
			p_res->upload_subresource_info_array = std::move(p_desc->from_desc.upload_subresource_info_array);
		}
		else
		{
			// ありえない.
			assert(false);
			return true;
		}

		
		// resにオブジェクト生成.
		p_res->ref_texture_ = new rhi::TextureDep();
		p_res->ref_view_ = new rhi::ShaderResourceViewDep();
		{
			rhi::TextureDep::Desc create_tex_desc = load_img_desc;// ベースコピー.
			// heap設定等上書き.
			{
				load_img_desc.heap_type = rhi::EResourceHeapType::Default;// GPU読み取り用.
				load_img_desc.bind_flag = rhi::ResourceBindFlag::ShaderResource;// シェーダリソース用途.
			}
			// 生成.
			p_res->ref_texture_->Initialize(p_device, load_img_desc);
			p_res->ref_view_->InitializeAsTexture(p_device, p_res->ref_texture_.Get(), 0, load_img_desc.mip_count, 0, load_img_desc.array_size);
		}
		
		return true;
	}
	
}
}