
#include "resource/resource_manager.h"

// マテリアルテクスチャパスの有効チェック等用.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "file/file.h"
#include "gfx/resource/mesh_loader_assimp.h"
#include "gfx/resource/texture_loader_directxtex.h"

namespace
{
	constexpr char k_mesh_cache_magic[4] = {'N', 'G', 'L', 'M'};
	constexpr ngl::u32 k_mesh_cache_version = 1;
	constexpr const char* k_mesh_cache_dir = "../ngl/data/cache";

	struct MeshCacheHeader
	{
		char magic[4] = {};
		ngl::u32 version = 0;
		ngl::u64 source_hash = 0;
		ngl::u32 raw_data_size = 0;
		ngl::u32 shape_count = 0;
		ngl::u32 material_count = 0;
	};

	void WriteBytes(std::vector<ngl::u8>& out, const void* data, size_t size)
	{
		const auto* ptr = reinterpret_cast<const ngl::u8*>(data);
		out.insert(out.end(), ptr, ptr + size);
	}

	template<typename T>
	void WritePod(std::vector<ngl::u8>& out, const T& value)
	{
		WriteBytes(out, &value, sizeof(T));
	}

	bool ReadBytes(const std::vector<ngl::u8>& data, size_t& offset, void* dst, size_t size)
	{
		if (offset + size > data.size())
			return false;
		memcpy(dst, data.data() + offset, size);
		offset += size;
		return true;
	}

	template<typename T>
	bool ReadPod(const std::vector<ngl::u8>& data, size_t& offset, T& out_value)
	{
		return ReadBytes(data, offset, &out_value, sizeof(T));
	}

	void WriteString(std::vector<ngl::u8>& out, const std::string& str)
	{
		const ngl::u16 len = static_cast<ngl::u16>(std::min<size_t>(str.size(), 0xFFFF));
		WritePod(out, len);
		if (len > 0)
			WriteBytes(out, str.data(), len);
	}

	bool ReadString(const std::vector<ngl::u8>& data, size_t& offset, std::string& out)
	{
		ngl::u16 len = 0;
		if (!ReadPod(data, offset, len))
			return false;
		if (offset + len > data.size())
			return false;
		out.assign(reinterpret_cast<const char*>(data.data() + offset), len);
		offset += len;
		return true;
	}

	void WriteLayout(std::vector<ngl::u8>& out, const ngl::gfx::MeshShapeLayout& layout)
	{
		WritePod(out, layout.total_size_in_byte);
		WritePod(out, layout.num_primitive);
		WritePod(out, layout.num_vertex);
		WritePod(out, layout.offset_position);
		WritePod(out, layout.offset_normal);
		WritePod(out, layout.offset_tangent);
		WritePod(out, layout.offset_binormal);
		WritePod(out, layout.num_color_ch);
		for (int i = 0; i < layout.offset_color.size(); ++i)
			WritePod(out, layout.offset_color[i]);
		WritePod(out, layout.num_uv_ch);
		for (int i = 0; i < layout.offset_uv.size(); ++i)
			WritePod(out, layout.offset_uv[i]);
		WritePod(out, layout.offset_index);
	}

	bool ReadLayout(const std::vector<ngl::u8>& data, size_t& offset, ngl::gfx::MeshShapeLayout& out_layout)
	{
		if (!ReadPod(data, offset, out_layout.total_size_in_byte))
			return false;
		if (!ReadPod(data, offset, out_layout.num_primitive))
			return false;
		if (!ReadPod(data, offset, out_layout.num_vertex))
			return false;
		if (!ReadPod(data, offset, out_layout.offset_position))
			return false;
		if (!ReadPod(data, offset, out_layout.offset_normal))
			return false;
		if (!ReadPod(data, offset, out_layout.offset_tangent))
			return false;
		if (!ReadPod(data, offset, out_layout.offset_binormal))
			return false;
		if (!ReadPod(data, offset, out_layout.num_color_ch))
			return false;
		for (int i = 0; i < out_layout.offset_color.size(); ++i)
		{
			if (!ReadPod(data, offset, out_layout.offset_color[i]))
				return false;
		}
		if (!ReadPod(data, offset, out_layout.num_uv_ch))
			return false;
		for (int i = 0; i < out_layout.offset_uv.size(); ++i)
		{
			if (!ReadPod(data, offset, out_layout.offset_uv[i]))
				return false;
		}
		if (!ReadPod(data, offset, out_layout.offset_index))
			return false;
		return true;
	}

	bool BuildMeshCachePath(const char* src_path, ngl::u64 src_hash, std::filesystem::path& out_path)
	{
		if (!src_path || src_hash == 0)
			return false;
		const auto SanitizeCacheBaseName = [](const std::filesystem::path& path)
		{
			std::string name = path.filename().string();
			if (name.empty())
				name = "mesh";
			for (char& ch : name)
			{
				if (ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*')
					ch = '_';
			}
			for (auto it = name.rbegin(); it != name.rend(); ++it)
			{
				if (*it == '.' || *it == ' ')
					*it = '_';
				else
					break;
			}
			return name;
		};
		std::filesystem::path cache_dir(k_mesh_cache_dir);
		std::error_code ec;
		std::filesystem::create_directories(cache_dir, ec);
		if (ec)
			return false;

		const std::filesystem::path src_fs_path(src_path);
		const std::string base_name = SanitizeCacheBaseName(src_fs_path);
		char hash_text[32] = {};
		std::snprintf(hash_text, sizeof(hash_text), "%016llx", static_cast<unsigned long long>(src_hash));
		out_path = cache_dir / (base_name + "_" + hash_text + ".meshcache");
		return true;
	}

	bool LoadMeshCache(
		const std::filesystem::path& cache_path,
		ngl::u64 expected_hash,
		ngl::gfx::MeshData& out_mesh,
		std::vector<ngl::assimp::MaterialTextureSet>& out_material,
		std::vector<int>& out_shape_material_index)
	{
		std::vector<ngl::u8> file_data;
		if (!ngl::file::ReadFileToBuffer(cache_path.string().c_str(), file_data))
			return false;

		size_t offset = 0;
		MeshCacheHeader header{};
		if (!ReadBytes(file_data, offset, header.magic, sizeof(header.magic)))
			return false;
		if (!ReadPod(file_data, offset, header.version))
			return false;
		if (!ReadPod(file_data, offset, header.source_hash))
			return false;
		if (!ReadPod(file_data, offset, header.raw_data_size))
			return false;
		if (!ReadPod(file_data, offset, header.shape_count))
			return false;
		if (!ReadPod(file_data, offset, header.material_count))
			return false;

		if (memcmp(header.magic, k_mesh_cache_magic, sizeof(k_mesh_cache_magic)) != 0)
			return false;
		if (header.version != k_mesh_cache_version)
			return false;
		if (header.source_hash != expected_hash)
			return false;

		out_mesh.shape_layout_array_.clear();
		out_mesh.shape_layout_array_.resize(header.shape_count);
		for (ngl::u32 i = 0; i < header.shape_count; ++i)
		{
			if (!ReadLayout(file_data, offset, out_mesh.shape_layout_array_[i]))
				return false;
		}

		out_shape_material_index.clear();
		out_shape_material_index.resize(header.shape_count);
		for (ngl::u32 i = 0; i < header.shape_count; ++i)
		{
			int index = 0;
			if (!ReadPod(file_data, offset, index))
				return false;
			out_shape_material_index[i] = index;
		}

		out_material.clear();
		out_material.resize(header.material_count);
		for (ngl::u32 i = 0; i < header.material_count; ++i)
		{
			std::string tex;
			if (!ReadString(file_data, offset, tex))
				return false;
			out_material[i].tex_base_color = tex;
			if (!ReadString(file_data, offset, tex))
				return false;
			out_material[i].tex_normal = tex;
			if (!ReadString(file_data, offset, tex))
				return false;
			out_material[i].tex_occlusion = tex;
			if (!ReadString(file_data, offset, tex))
				return false;
			out_material[i].tex_roughness = tex;
			if (!ReadString(file_data, offset, tex))
				return false;
			out_material[i].tex_metalness = tex;
		}

		out_mesh.raw_data_mem_.resize(header.raw_data_size);
		if (!ReadBytes(file_data, offset, out_mesh.raw_data_mem_.data(), header.raw_data_size))
			return false;

		return true;
	}

	bool SaveMeshCache(
		const std::filesystem::path& cache_path,
		ngl::u64 src_hash,
		const ngl::gfx::MeshData& mesh,
		const std::vector<ngl::assimp::MaterialTextureSet>& material,
		const std::vector<int>& shape_material_index)
	{
		MeshCacheHeader header{};
		memcpy(header.magic, k_mesh_cache_magic, sizeof(k_mesh_cache_magic));
		header.version = k_mesh_cache_version;
		header.source_hash = src_hash;
		header.raw_data_size = static_cast<ngl::u32>(mesh.raw_data_mem_.size());
		header.shape_count = static_cast<ngl::u32>(mesh.shape_layout_array_.size());
		header.material_count = static_cast<ngl::u32>(material.size());

		std::vector<ngl::u8> out_data;
		out_data.reserve(sizeof(MeshCacheHeader) + header.raw_data_size);

		WriteBytes(out_data, header.magic, sizeof(header.magic));
		WritePod(out_data, header.version);
		WritePod(out_data, header.source_hash);
		WritePod(out_data, header.raw_data_size);
		WritePod(out_data, header.shape_count);
		WritePod(out_data, header.material_count);

		for (const auto& layout : mesh.shape_layout_array_)
			WriteLayout(out_data, layout);

		for (int i = 0; i < shape_material_index.size(); ++i)
		{
			const int index = shape_material_index[i];
			WritePod(out_data, index);
		}

		for (const auto& mtl : material)
		{
			WriteString(out_data, mtl.tex_base_color);
			WriteString(out_data, mtl.tex_normal);
			WriteString(out_data, mtl.tex_occlusion);
			WriteString(out_data, mtl.tex_roughness);
			WriteString(out_data, mtl.tex_metalness);
		}

		WriteBytes(out_data, mesh.raw_data_mem_.data(), mesh.raw_data_mem_.size());

		return ngl::file::WriteFileFromBuffer(cache_path.string().c_str(), out_data);
	}
}

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
		std::vector<gfx::MeshShapeLayout> shape_layout_array = {};

		const u64 src_hash = file::CalcFileHashFNV1a64(p_res->GetFileName());
		std::filesystem::path cache_path;
		bool cache_hit = false;
		if (BuildMeshCachePath(p_res->GetFileName(), src_hash, cache_path))
		{
			cache_hit = LoadMeshCache(cache_path, src_hash, p_res->data_, material_array, shape_material_index_array);
		}

		if (cache_hit)
		{
			if (!gfx::InitializeMeshDataFromLayout(p_res->data_, p_device))
				return false;
		}
		else
		{
			const bool result_load_mesh = assimp::LoadMeshData(p_res->data_, material_array, shape_material_index_array, shape_layout_array, p_device, p_res->GetFileName());
			if (!result_load_mesh)
				return false;

			if (BuildMeshCachePath(p_res->GetFileName(), src_hash, cache_path))
			{
				SaveMeshCache(cache_path, src_hash, p_res->data_, material_array, shape_material_index_array);
			}
		}

		
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
			bool is_hdr = false;
			{
				using ExtNameStr = ngl::text::HashText<16>;
				constexpr ExtNameStr dds_ext = ".dds";
				constexpr ExtNameStr hdr_ext = ".hdr";


				const auto* file_name = p_res->GetFileName();
				const int file_name_length = static_cast<int>(strlen(file_name));


				constexpr auto CheckExt = [](const ExtNameStr& ext, const char* file_name, int file_name_length)
					{
						return ext.Length() < file_name_length && 0 == strncmp((file_name + file_name_length - ext.Length()), ext.Get(), ext.Length());
					};

				is_dds = CheckExt(dds_ext, file_name, file_name_length);
				is_hdr = CheckExt(hdr_ext, file_name, file_name_length);
			}

			if(is_dds)
			{
				// DDS ロード.
				if(!directxtex::LoadImageData_DDS(image_data, meta_data, p_device, p_res->GetFileName()))
					return false;
			}
			else if (is_hdr)
			{
				// HDR ロード.
				if (!directxtex::LoadImageData_HDR(image_data, meta_data, p_device, p_res->GetFileName()))
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
		p_res->ref_texture_.Reset(new rhi::TextureDep());
		p_res->ref_view_.Reset(new rhi::ShaderResourceViewDep());
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