
#if 0

visible_probe_sky_visibility_sample_cs.hlsl

#endif


#define INDIRECT_MODE 1
#define RAY_SAMPLE_COUNT_PER_VOXEL 8

#define PROBE_UPDATE_TEMPORAL_RATE  (0.1)
#include "probe_sky_visibility_sample_base.hlsli"
