
#if 0

wcp_coarse_ray_sample_cs.hlsl

#endif


#define INDIRECT_MODE 0
#define RAY_SAMPLE_COUNT_PER_VOXEL 32

#define PROBE_UPDATE_TEMPORAL_RATE  (0.1)
#include "wcp_probe_ray_sample_base.hlsli"
