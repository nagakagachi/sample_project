
#include "gfx/resource/mesh_loader_assimp.h"

#include <numeric>

#include "math/math.h"

// rhi
#include "rhi/d3d12/resource.d3d12.h"
#include "rhi/d3d12/resource_view.d3d12.h"

// assimp.
#include <assimp/postprocess.h>  // Post processing flags
#include <assimp/scene.h>        // Output data structure

#include <assimp/Importer.hpp>  // C++ importer interface

namespace ngl
{
    namespace assimp
    {

        namespace
        {
            constexpr int CalcAlignedSize(int size, int align)
            {
                return ((size + (align - 1)) / align) * align;
            }
        }  // namespace

        // Assimpを利用してファイルから1Meshを構成するShape群を生成.
        //	ファイル内の頂点はすべてPreTransformeされ, 配置情報はベイクされる(GLTFやUSD等の内部に配置情報を含むものはそれらによる複製配置等がすべてジオメトリとして生成される).
        bool LoadMeshData(
            gfx::MeshData& out_mesh,
            std::vector<MaterialTextureSet>& out_material_tex_set,
            std::vector<int>& out_shape_material_index,
            rhi::DeviceDep* p_device, const char* filename)
        {
            // ReadFileで読み込まれたメモリ等はAssimp::Importerインスタンスの寿命でクリーンアップされる.

            unsigned int ai_mesh_read_options = 0u;
            {
                ai_mesh_read_options |= aiProcess_CalcTangentSpace;
                ai_mesh_read_options |= aiProcess_Triangulate;
                ai_mesh_read_options |= aiProcess_JoinIdenticalVertices;
                ai_mesh_read_options |= aiProcess_SortByPType;

                // ファイルのジオメトリの配置情報をフラット化してすべて変換済み頂点にする.
                //	GLTF等の同一MeshをTransformで複数配置できる仕組みを使っているファイルはその分ベイクされてロードされるジオメトリが増加する.
                //	MeshData は配置情報を含まない単純なジオメトリ情報という役割であるため, ここではすべて変換済みにする.
                ai_mesh_read_options |= aiProcess_PreTransformVertices;

                // アプリ側が左手系採用のため.
                //	このフラグには 左手座標系へのジオメトリの変換, UVのY反転, TriangleFaceのIndex順反転 が含まれる.
                ai_mesh_read_options |= aiProcess_ConvertToLeftHanded;
            }

            Assimp::Importer asimporter;
            const aiScene* ai_scene = asimporter.ReadFile(
                filename,
                ai_mesh_read_options);
            if (!ai_scene)
            {
                std::cout << "[ERROR][LoadMeshData] load failed " << filename << std::endl;
                assert(false);
                return false;
            }

            struct ShapeDataOffsetInfo
            {
                ShapeDataOffsetInfo()
                {
                    total_size_in_byte = 0;

                    num_prim   = 0;
                    num_vertex = 0;

                    num_color_ch = 0;
                    num_uv_ch    = 0;

                    offset_position = -1;
                    offset_normal   = -1;
                    offset_tangent  = -1;
                    offset_binormal = -1;

                    offset_color.fill(-1);
                    offset_uv.fill(-1);

                    offset_index = -1;
                }

                int total_size_in_byte = 0;

                int num_prim   = 0;
                int num_vertex = 0;

                int offset_position = -1;
                int offset_normal   = -1;
                int offset_tangent  = -1;
                int offset_binormal = -1;

                int num_color_ch;
                std::array<int, 8> offset_color;
                int num_uv_ch;
                std::array<int, 8> offset_uv;

                int offset_index = -1;
            };

            std::vector<ShapeDataOffsetInfo> offset_info;
            std::vector<assimp::MaterialTextureSet> material_info_array;
            std::vector<int> shape_material_index_array;

            offset_info.clear();
            material_info_array.clear();
            shape_material_index_array.clear();

            // 総数計算.
            constexpr int vtx_align = 16;
            int total_size_in_byte  = 0;
            for (auto mesh_i = 0u; mesh_i < ai_scene->mNumMeshes; ++mesh_i)
            {
                const auto* p_ai_mesh = ai_scene->mMeshes[mesh_i];

                // mesh shape.
                {
                    const int num_prim   = p_ai_mesh->mNumFaces;
                    const int num_vertex = p_ai_mesh->mNumVertices;

                    const int num_position = p_ai_mesh->mVertices ? num_vertex : 0;
                    const int num_normal   = p_ai_mesh->mNormals ? num_vertex : 0;
                    const int num_tangent  = p_ai_mesh->mTangents ? num_vertex : 0;
                    const int num_binormal = p_ai_mesh->mBitangents ? num_vertex : 0;
                    const int num_color_ch = std::min(p_ai_mesh->GetNumColorChannels(), (uint32_t)gfx::MeshVertexSemantic::SemanticCount(gfx::EMeshVertexSemanticKind::COLOR));  // Colorのサポート最大数でクランプ.
                    const int num_uv_ch    = std::min(p_ai_mesh->GetNumUVChannels(), (uint32_t)gfx::MeshVertexSemantic::SemanticCount(gfx::EMeshVertexSemanticKind::TEXCOORD));  // Texcoordのサポート最大数でクランプ.

                    offset_info.push_back({});
                    auto& info        = offset_info.back();
                    info.num_prim     = num_prim;
                    info.num_vertex   = num_vertex;
                    info.num_color_ch = num_color_ch;
                    info.num_uv_ch    = num_uv_ch;

                    // 総サイズとオフセット計算.
                    info.total_size_in_byte = 0;
                    // Position.
                    {
                        info.offset_position    = info.total_size_in_byte + total_size_in_byte;
                        info.total_size_in_byte = CalcAlignedSize(info.total_size_in_byte, vtx_align) + num_position * sizeof(ngl::math::Vec3);
                    }
                    // Normal.
                    if (0 < num_normal)
                    {
                        info.offset_normal      = info.total_size_in_byte + total_size_in_byte;
                        info.total_size_in_byte = CalcAlignedSize(info.total_size_in_byte, vtx_align) + num_normal * sizeof(ngl::math::Vec3);
                    }
                    // Tangent.
                    if (0 < num_tangent)
                    {
                        info.offset_tangent     = info.total_size_in_byte + total_size_in_byte;
                        info.total_size_in_byte = CalcAlignedSize(info.total_size_in_byte, vtx_align) + num_tangent * sizeof(ngl::math::Vec3);
                    }
                    // Binormal.
                    if (0 < num_binormal)
                    {
                        info.offset_binormal    = info.total_size_in_byte + total_size_in_byte;
                        info.total_size_in_byte = CalcAlignedSize(info.total_size_in_byte, vtx_align) + num_binormal * sizeof(ngl::math::Vec3);
                    }
                    // Color.
                    for (auto ci = 0; ci < num_color_ch; ++ci)
                    {
                        info.offset_color[ci]   = info.total_size_in_byte + total_size_in_byte;
                        info.total_size_in_byte = CalcAlignedSize(info.total_size_in_byte, vtx_align) + num_vertex * sizeof(ngl::gfx::VertexColor);
                    }
                    // UV.
                    for (auto ci = 0; ci < num_uv_ch; ++ci)
                    {
                        info.offset_uv[ci]      = info.total_size_in_byte + total_size_in_byte;
                        info.total_size_in_byte = CalcAlignedSize(info.total_size_in_byte, vtx_align) + num_vertex * sizeof(ngl::math::Vec2);
                    }
                    // Index.
                    info.offset_index       = info.total_size_in_byte + total_size_in_byte;
                    info.total_size_in_byte = CalcAlignedSize(info.total_size_in_byte, vtx_align) + num_prim * sizeof(uint32_t) * 3;

                    // ジオメトリ総サイズ更新.
                    total_size_in_byte += info.total_size_in_byte;
                }

                // ShapeのMaterial.
                {
                    shape_material_index_array.push_back({});
                    auto& shape_material_index = shape_material_index_array.back();

                    shape_material_index = static_cast<int>(p_ai_mesh->mMaterialIndex);
                }

                // Material情報収集.
                {
                    auto func_get_ai_material_texture = [](std::string& out_texture_path, const aiScene* ai_scene, unsigned int material_index, aiTextureType texture_type)
                        -> bool
                    {
                        if (ai_scene->mNumMaterials > material_index)
                        {
                            const auto* p_ai_material = ai_scene->mMaterials[material_index];

                            aiString tex_path;
                            if (aiReturn::aiReturn_SUCCESS == p_ai_material->GetTexture(texture_type, 0, &tex_path))
                            {
                                out_texture_path = tex_path.C_Str();
                                return true;
                            }
                        }
                        out_texture_path = {};  // 無効はクリア.
                        return false;
                    };

                    const auto ai_material_index = p_ai_mesh->mMaterialIndex;

                    material_info_array.push_back({});
                    auto& material = material_info_array.back();
                    {
                        // BaseColor
                        if (!func_get_ai_material_texture(material.tex_base_color, ai_scene, ai_material_index, aiTextureType_BASE_COLOR))
                        {
                            func_get_ai_material_texture(material.tex_base_color, ai_scene, ai_material_index, aiTextureType_DIFFUSE);  // 失敗したら別のBaseColor相当のタイプでリトライ.
                        }

                        // Normal.
                        func_get_ai_material_texture(material.tex_normal, ai_scene, ai_material_index, aiTextureType_NORMALS);

                        bool is_specular_texture = true;
                        if (!func_get_ai_material_texture(material.tex_metalness, ai_scene, ai_material_index, aiTextureType_METALNESS))
                        {
                            // Specularとして取れた場合も, RGBにORMが格納されたテクスチャが取れると思われる.
                            // 普通?は aiTextureType_DIFFUSE_ROUGHNESS 等で同じ値が取れるはずだが, Specularとして取得できる場合は aiTextureType_DIFFUSE_ROUGHNESS で有効値がとれない場合がある.
                            // その場合は自前で Roughness(ORM) として利用するように処理を追加する(後段).
                            is_specular_texture = func_get_ai_material_texture(material.tex_metalness, ai_scene, ai_material_index, aiTextureType_SPECULAR);  // 失敗したら別のMetalness相当のタイプでリトライ.
                        }
                        bool is_valid_roughness_texture = true;
                        if (!func_get_ai_material_texture(material.tex_roughness, ai_scene, ai_material_index, aiTextureType_DIFFUSE_ROUGHNESS))
                        {
                            is_valid_roughness_texture = func_get_ai_material_texture(material.tex_roughness, ai_scene, ai_material_index, aiTextureType_SHININESS);  // 失敗したら別のRoughness相当のタイプでリトライ.
                        }

                        //	METALNESS相当のテクスチャが aiTextureType_SPECULAR が取得できた場合はそのテクスチャのRGBチャンネルが ORM となっている模様(Lumberyard Bistro).
                        //	その場合はaiTextureType_DIFFUSE_ROUGHNESS が有効値(同じORMテクスチャパス)が取得出来ないことがあるため, 自前でRoughnessにSpecular(ORM)を設定する.
                        if (!is_valid_roughness_texture && is_specular_texture)
                        {
                            material.tex_roughness = material.tex_metalness;
                        }

                        func_get_ai_material_texture(material.tex_occlusion, ai_scene, ai_material_index, aiTextureType_AMBIENT_OCCLUSION);
                    }
                }
            }

            // このメッシュの全情報を格納するメモリを確保.
            out_mesh.raw_data_mem_.resize(total_size_in_byte);
            // 必要分のShapeを確保.
            out_mesh.shape_array_.resize(offset_info.size());



            // セットアップ用のメッシュ初期化情報構築.
            std::vector<gfx::MeshShapeInitializeSourceData> init_source_data;
            init_source_data.resize(offset_info.size());
            for (int i = 0; i < offset_info.size(); ++i)
            {
                const auto& info = offset_info[i];
                uint8_t* ptr = out_mesh.raw_data_mem_.data();// 実メモリ.
                
                auto& init_data = init_source_data[i];

                // マッピング.
                {
                    init_data.num_primitive_ = info.num_prim;
                    init_data.num_vertex_    = info.num_vertex;

                    init_data.position_ = (ngl::math::Vec3*)&ptr[info.offset_position];
                    if (0 <= info.offset_normal)
                        init_data.normal_ = (ngl::math::Vec3*)&ptr[info.offset_normal];
                    if (0 <= info.offset_tangent)
                        init_data.tangent_ = (ngl::math::Vec3*)&ptr[info.offset_tangent];
                    if (0 <= info.offset_binormal)
                        init_data.binormal_ = (ngl::math::Vec3*)&ptr[info.offset_binormal];

                    for (int ci = 0; ci < info.num_color_ch; ++ci)
                    {
                        init_data.color_.push_back({});
                        init_data.color_.back() = (ngl::gfx::VertexColor*)&ptr[info.offset_color[ci]];
                    }
                    for (int ci = 0; ci < info.num_uv_ch; ++ci)
                    {
                        init_data.texcoord_.push_back({});
                        init_data.texcoord_.back() = (ngl::math::Vec2*)&ptr[info.offset_uv[ci]];
                    }
                    
                    init_data.index_ = (uint32_t*)&ptr[info.offset_index];
                }

                // データコピー.
                {
                    if (init_data.position_)
                    {
                        memcpy(init_data.position_, ai_scene->mMeshes[i]->mVertices, sizeof(float) * 3 * info.num_vertex);
                    }
                    if (init_data.normal_)
                    {
                        memcpy(init_data.normal_, ai_scene->mMeshes[i]->mNormals, sizeof(float) * 3 * info.num_vertex);
                    }
                    if (init_data.tangent_)
                    {
                        memcpy(init_data.tangent_, ai_scene->mMeshes[i]->mTangents, sizeof(float) * 3 * info.num_vertex);
                    }
                    if (init_data.binormal_)
                    {
                        memcpy(init_data.binormal_, ai_scene->mMeshes[i]->mBitangents, sizeof(float) * 3 * info.num_vertex);
                    }

                    // AssimpのVertexColorは32bit float のため変換.
                    for (int ci = 0; ci < info.num_color_ch; ++ci)
                    {
                        auto* p_src    = ai_scene->mMeshes[i]->mColors[ci];
                        auto* data_ptr = init_data.color_[ci];
                        for (int vi = 0; vi < info.num_vertex; ++vi)
                        {
                            data_ptr[vi].r = uint8_t(p_src[vi].r * 255);
                            data_ptr[vi].g = uint8_t(p_src[vi].g * 255);
                            data_ptr[vi].b = uint8_t(p_src[vi].b * 255);
                            data_ptr[vi].a = uint8_t(p_src[vi].a * 255);
                        }
                    }

                    for (int ci = 0; ci < info.num_uv_ch; ++ci)
                    {
                        auto* p_src    = ai_scene->mMeshes[i]->mTextureCoords[ci];
                        auto* data_ptr = init_data.texcoord_[ci];
                        for (int vi = 0; vi < info.num_vertex; ++vi)
                        {
                            data_ptr[vi] = {p_src[vi].x, p_src[vi].y};
                        }
                    }

                    {
                        auto* data_ptr = init_data.index_;
                        for (uint32_t face_i = 0; face_i < ai_scene->mMeshes[i]->mNumFaces; ++face_i)
                        {
                            // 三角化前提.
                            const auto p_face_index = ai_scene->mMeshes[i]->mFaces[face_i].mIndices;

                            data_ptr[face_i * 3 + 0] = p_face_index[0];
                            data_ptr[face_i * 3 + 1] = p_face_index[1];
                            data_ptr[face_i * 3 + 2] = p_face_index[2];
                        }
                    }
                }
            }

            // Create Rhi.
            //    init_source_data を元にメッシュ初期化.
            for (int i = 0; i < offset_info.size(); ++i)
            {
                auto& mesh       = out_mesh.shape_array_[i];

                mesh.Initialize(p_device, init_source_data[i]);
            }

            // Material Infomation.
            out_material_tex_set = std::move(material_info_array);
            // Shape Material Infomation.
            out_shape_material_index = std::move(shape_material_index_array);

            return true;
        }
    }  // namespace assimp
}  // namespace ngl
