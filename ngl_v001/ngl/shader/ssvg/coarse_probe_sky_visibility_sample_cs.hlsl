
#if 0

coarse_probe_sky_visibility_sample_cs.hlsl

#endif


#define INDIRECT_MODE 0
#define RAY_SAMPLE_COUNT_PER_VOXEL 1

#define PROBE_UPDATE_TEMPORAL_RATE  (0.25)
#include "probe_sky_visibility_sample_base.hlsli"
