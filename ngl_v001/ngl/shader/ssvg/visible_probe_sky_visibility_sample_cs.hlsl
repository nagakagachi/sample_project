
#if 0

visible_probe_sky_visibility_sample_cs.hlsl

// 可視Probe SkyVisibilityサンプルの取得とワークバッファへの蓄積. 後段のstore処理で本体のバッファにフィードバック.

#endif


#define INDIRECT_MODE 1
#define RAY_SAMPLE_COUNT_PER_VOXEL 8

#define PROBE_UPDATE_TEMPORAL_RATE  (0.1)
#include "probe_sky_visibility_sample_base.hlsli"
