# sample_projct

DirectX12 Toy Renderer (WIP).
![Image](https://github.com/user-attachments/assets/31ded6f9-f69a-4df2-b1c3-23433d0c02af)

This repository is currently implementing the RenderTaskGraph (RenderGraph). </br>
The following reference materials are available on RenderGraph. </br>
https://nagakagachi.notion.site/RenderGraph-54f0cf4284c7466697b99cc0df81be80
</br>

 
# Build
  - clone
  - build third_party/assimp (cmake)
    - cd assimp
    - cmake CMakeLists.txt 
    - cmake --build . --config Release
  - build third_party/DirectXTex (cmake)
    - cd DirectXTex
    - cmake CMakeLists.txt 
    - cmake --build . --config Release
  - build graphics_test.sln (Visual Studio 2022)

# Render Task Graph
マルチスレッド/非同期Compute対応レンダリングパイプラインを構築するためにRenderTaskGraph(RTG)というものを実装しています.<br/>
UE5のRDGに類似したシンプルなRenderGraphのようなものになります.<br/>
DirectX12世代で必要になるリソースステート追跡を含めた, レンダリングリソース依存解決を簡単にコーディングすることを目的にしています.<br/>
![Image](https://github.com/user-attachments/assets/2178ba19-f7b9-4730-bdf7-e3d6db524eda)

現行では
- Graphicsパス
  - IGraphicsTaskNode
- 非同期Computeパス
  - IComputeTaskNode
- リソースの外部化
  - PropagateResouceToNextFrame
  - 過去フレームのリソース利用(ヒストリバッファ等)
  - 別のGraphからリソース利用(マルチビューレンダリング等)
 
に対応.<br/>

## Rendering Pipeline
以下は単純な依存関係を持つレンダリングパイプライン構築のコードです.<br/>
DepthPassで描画したDepthTextureを, 引き続き利用するGBufferPassという構成です.<br/>
```c++
// 1. register depth pass.
auto* task_depth = rtg_builder.AppendTaskNode<TaskDepthPass>();
task_depth->Setup(rtg_builder);

// 2. register gbuffer pass. Declares the use of rendered depth textures in the depth path.
auto* task_gbuffer = rtg_builder.AppendTaskNode<TaskGBufferPass>();
task_gbuffer->Setup(rtg_builder, task_depth->h_depth_);
```
上記のように, Passのメンバ変数(例えば h_depth_)を利用して, 直接的にわかりやすくPass間のリソース依存関係を記述できます.<br/>
ここで h_depth_ はリソース登録の単なる整数IDです. 後述するCompile後にこのIDでこのPassに割り当てられた実際のリソースを取得できます.<br/>
https://github.com/nagakagachi/sample_projct/blob/b04d2f1f881c190715f92f694ef0dea8a549b092/graphics_test/graphics_test/src/ngl/render/test_render_path.cpp#L76

## Setup RTG
Passが利用するリソースをrtg_builderにレコードします.<br/>
(新規リソース/別Pass由来リソース, dsv/rtv/uav).<br/>
```c++
struct TaskDepthPass : public rtg::IGraphicsTaskNode
{
  rtg::RtgResourceHandle h_depth_{};

  void Setup(rtg::RenderTaskGraphBuilder& rtg_builder)
  {
    // use new (on currend frame) texture resouce used for DEPTH_TARGET. 
    h_depth_ = rtg_builder.RecordResourceAccess(*this, rtg_builder.CreateResource(depth_desc), rtg::access_type::DEPTH_TARGET);
  }
};

struct TaskGBufferPass : public rtg::IGraphicsTaskNode
{
  rtg::RtgResourceHandle h_depth_{};
  rtg::RtgResourceHandle h_gbuffer_a_{};

  void Setup(rtg::RenderTaskGraphBuilder& rtg_builder, rtg::RtgResourceHandle h_depth_from_prev_pass)
  {
    // use a depth texture rendered in another pass (passed in the argument) as DEPTH_TARGET.
    h_depth_ = rtg_builder.RecordResourceAccess(*this, h_depth_from_prev_pass, rtg::access_type::DEPTH_TARGET);

    // use new (on currend frame) texture resouce used for DEPTH_TARGET. 
    h_gbuffer_a_ = rtg_builder.RecordResourceAccess(*this, rtg_builder.CreateResource(gbuffer_a_desc), rtg::access_type::RENDER_TARGET);
  }
};
```
Setup時点では実際のリソースは割り当てられていません. スケジューリング用の情報が登録されるだけです.<br/>
https://github.com/nagakagachi/sample_projct/blob/b04d2f1f881c190715f92f694ef0dea8a549b092/graphics_test/graphics_test/src/ngl/render/test_pass.h#L36

## Compile And Execute
"Compile" と "Execute" によって登録情報から適切なリソース割り当てと, Passのレンダリング処理が実行されます.<br/>
Compileによってリソースステート解決(Barrier)が行われるため, Passは自身のレンダリング処理のみに注力できます.<br/>
```c++
rtg_manager.Compile(rtg_builder);

rtg_builder.Execute(out_graphics_cmd, out_compute_cmd, p_job_system);
```
Compileによってリソーススケジューリングが確定されるため, Pass毎に並列でマルチスレッドレンダリングが可能です.<br/>
RTGリソース以外の部分でのマルチスレッド対応はユーザの責任となります.<br/>
https://github.com/nagakagachi/sample_projct/blob/b04d2f1f881c190715f92f694ef0dea8a549b092/graphics_test/graphics_test/src/ngl/render/test_render_path.cpp#L300

## Rendering RTG
Execute で呼び出されるPassのレンダリング処理Run()で, 割当済みリソースをrtg_builderから取得できます.<br/>
これらのリソースのステート解決はRTGの役割であるため, Pass側でStateBarrierCommandを発行する必要はありません.<br/>
(GetAllocatedResourceの戻り値が curr_state_ を持っているため, 最終的にそのステートになるようにすれば独自のステート遷移も可能です.)<br/>

```c++
struct TaskDepthPass : public rtg::IGraphicsTaskNode
{
  void Run(rtg::RenderTaskGraphBuilder& rtg_builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
  {
    auto res_depth = rtg_builder.GetAllocatedResource(this, h_depth_);
    // rendering mesh to res_depth.tex_
  }
};

struct TaskGBufferPass : public rtg::IGraphicsTaskNode
{
  void Run(rtg::RenderTaskGraphBuilder& rtg_builder, rhi::GraphicsCommandListDep* gfx_commandlist) override
  {
    auto res_depth = rtg_builder.GetAllocatedResource(this, h_depth_);
    auto res_gb_a = rtg_builder.GetAllocatedResource(this, h_gbuffer_a_);
  }
};
```
https://github.com/nagakagachi/sample_projct/blob/b04d2f1f881c190715f92f694ef0dea8a549b092/graphics_test/graphics_test/src/ngl/render/test_pass.h#L64

## Sample Code
個々のレンダリングパスの実装は以下.<br/>
https://github.com/nagakagachi/sample_projct/blob/b04d2f1f881c190715f92f694ef0dea8a549b092/graphics_test/graphics_test/src/ngl/render/test_pass.h#L36

レンダリングパイプラインの構築と実行は以下.<br/>
https://github.com/nagakagachi/sample_projct/blob/b04d2f1f881c190715f92f694ef0dea8a549b092/graphics_test/graphics_test/src/ngl/render/test_render_path.cpp#L76

# Third Party
  - Assimp
    - https://github.com/assimp/assimp 
  - DirectXTex
    - https://github.com/microsoft/DirectXTex
  - tinyxml2
    - https://github.com/leethomason/tinyxml2
  - Dear ImGui
    - https://github.com/ocornut/imgui





