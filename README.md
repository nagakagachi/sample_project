# sample_project

DirectX12 Toy Renderer (WIP).

<img src="https://github.com/user-attachments/assets/27dd00d2-3c3e-4328-940e-12b93a8f1640" width="50%"><img src="https://github.com/user-attachments/assets/b433838c-75eb-42b5-8b86-418a29040ee1" width="50%">

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
  - build ngl_v001.sln (Visual Studio 2022)

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
ここで h_depth_ は内部的に割り当てられる単なるIDです. 後述するCompileによってこのIDに実際のリソースが割り当てられ, Passが問い合わせできるようになります.<br/>
https://github.com/nagakagachi/sample_projct/blob/2517e77d16df00e03a779febc52884ade85293eb/ngl_v001/ngl/src/render/test_render_path.cpp#L76

## Setup RTG
Pass毎に利用するリソースを宣言, 登録しそのハンドルを保持します.<br/>
リソースハンドルに対してレンダリング処理でどのように利用するかをrtg_builderにレコードします.<br/>
リソースを使用する実際のレンダリング処理をLambdaで登録します(後述).<br/>
```c++
struct TaskDepthPass : public rtg::IGraphicsTaskNode
{
  rtg::RtgResourceHandle h_depth_{};

  void Setup(rtg::RenderTaskGraphBuilder& rtg_builder)
  {
    // use new (on currend frame) texture resouce used for DEPTH_TARGET. 
    h_depth_ = rtg_builder.RecordResourceAccess(*this, rtg_builder.CreateResource(depth_desc), rtg::access_type::DEPTH_TARGET);

    // Register RenderFunction.
    builder.RegisterTaskNodeRenderFunction(this,
      [this](rtg::RenderTaskGraphBuilder& rtg_builder, rhi::GraphicsCommandListDep* gfx_commandlist)
      {
        // Do Render.
      });
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

    // Register RenderFunction.
    builder.RegisterTaskNodeRenderFunction(this,
      [this](rtg::RenderTaskGraphBuilder& rtg_builder, rhi::GraphicsCommandListDep* gfx_commandlist)
      {
        // Do Render.
      });
  }
};
```
Setup時点では実際のリソースは割り当てられず, アクセスもできません. スケジューリング用の情報が登録されるだけです.<br/>
https://github.com/nagakagachi/sample_projct/blob/2517e77d16df00e03a779febc52884ade85293eb/ngl_v001/ngl/include/render/test_pass.h#L41

## Compile And Execute
"Compile" と "Execute" によって登録情報から適切なリソース割り当てと, Passのレンダリング処理が実行されます.<br/>
Compileによってリソースステート解決(Barrier)が行われるため, Passは自身のレンダリング処理のみに注力できます.<br/>
```c++
rtg_manager.Compile(rtg_builder);

rtg_builder.Execute(out_graphics_cmd, out_compute_cmd, p_job_system);
```
Compileによってリソーススケジューリングが確定されるため, Pass毎に並列でマルチスレッドレンダリングが可能です.<br/>
RTGリソース以外の部分でのマルチスレッド対応はユーザの責任となります.<br/>
https://github.com/nagakagachi/sample_projct/blob/2517e77d16df00e03a779febc52884ade85293eb/ngl_v001/ngl/src/render/test_render_path.cpp#L300

## Rendering RTG
Passのレンダリング処理はLambdaとして RegisterTaskNodeRenderFunction() で登録します.(Pass自身のポインタは登録キー).<br/>
登録したLambdaはRTGによってExecute()中に呼び出され, Lambda内ではハンドルに割り当てられたリソースにアクセスできます.<br/>
これらのリソースのステート遷移はRTGシステムの責任で実行されるため, Pass側ではRecordで宣言したステートとなっている前提でレンダリングを記述します.<br/>
なお, Passのレンダリング完了の段階で終了ステートになってさえいればRTGとして破綻はしないため, 手動でステート遷移をすることも可能です.<br/>

```c++
struct TaskDepthPass : public rtg::IGraphicsTaskNode
{
  void Setup(rtg::RenderTaskGraphBuilder& rtg_builder)
  {
    ...

    // Register RenderFunction.
    builder.RegisterTaskNodeRenderFunction(this,
      [this](rtg::RenderTaskGraphBuilder& rtg_builder, rhi::GraphicsCommandListDep* gfx_commandlist)
      {
        // Get allocated resources via handle.
        auto res_depth = rtg_builder.GetAllocatedResource(this, h_depth_);
        // Do Render.

      });
  }
};

struct TaskGBufferPass : public rtg::IGraphicsTaskNode
{
  void Setup(rtg::RenderTaskGraphBuilder& rtg_builder)
  {
    ...

    // Register RenderFunction.
    builder.RegisterTaskNodeRenderFunction(this,
      [this](rtg::RenderTaskGraphBuilder& rtg_builder, rhi::GraphicsCommandListDep* gfx_commandlist)
      {
        // Get allocated resources via handle.
        auto res_depth = rtg_builder.GetAllocatedResource(this, h_depth_);
        auto res_gb_a = rtg_builder.GetAllocatedResource(this, h_gbuffer_a_);
        // Do Render.

      });
  }
};
```
https://github.com/nagakagachi/sample_projct/blob/2517e77d16df00e03a779febc52884ade85293eb/ngl_v001/ngl/include/render/test_pass.h#L66
https://github.com/nagakagachi/sample_projct/blob/2517e77d16df00e03a779febc52884ade85293eb/ngl_v001/ngl/include/render/test_pass.h#L153

## Sample Code
個々のレンダリングパスの実装は以下.<br/>
https://github.com/nagakagachi/sample_projct/blob/2517e77d16df00e03a779febc52884ade85293eb/ngl_v001/ngl/include/render/test_pass.h#L41

レンダリングパイプラインの構築と実行は以下.<br/>
https://github.com/nagakagachi/sample_projct/blob/2517e77d16df00e03a779febc52884ade85293eb/ngl_v001/ngl/src/render/test_render_path.cpp#L76

# Concurrent Binary Trees Based Software Tessellation Demo
CBT based ComputeShader Tessellation の検証コードが含まれます.</br>
既定で2-Triの矩形メッシュを対象としていますが, 任意メッシュに利用可能です.</br>
CBTをメモリアロケータとして利用し, HalfEdge/Bisectorベースで細分化をするソフトウェアテッセレーション手法です.</br>
任意のメッシュに適応可能で, T-Junctionを発生させない分割等の特徴があります.</br>
Concurrent Binary Trees for Large-Scale Game Components. </br>
- https://arxiv.org/pdf/2407.02215
<img width="720" alt="Image" src="https://github.com/user-attachments/assets/53a5800e-a7fc-4dc3-8c87-c4be37c18fc7" />


# Third Party
  - Assimp
    - https://github.com/assimp/assimp 
  - DirectXTex
    - https://github.com/microsoft/DirectXTex
  - tinyxml2
    - https://github.com/leethomason/tinyxml2
  - Dear ImGui
    - https://github.com/ocornut/imgui
# NuGet
- DirectX Shader Compiler
  - https://www.nuget.org/packages/Microsoft.Direct3D.DXC
- WinPixEventRuntime
  - https://www.nuget.org/packages/WinPixEventRuntime
- DirectX Agility SDK
  - https://www.nuget.org/packages/Microsoft.Direct3D.D3D12


# References
- https://learnopengl.com/
- https://www.pbrt.org/
- https://google.github.io/filament/Filament.html
- https://github.com/EpicGames/UnrealEngine
- https://sites.google.com/site/monshonosuana/directx%E3%81%AE%E8%A9%B1
- https://github.com/dubiousconst282/VoxelRT/tree/alt-renderers
  


