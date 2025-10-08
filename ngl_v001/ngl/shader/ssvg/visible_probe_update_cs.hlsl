
#if 0

visible_probe_update_cs.hlsl

#endif


#define INDIRECT_MODE 1
#define RAY_SAMPLE_COUNT_PER_VOXEL 8

#define PROBE_UPDATE_TEMPORAL_RATE  (0.1)
#include "probe_update_base.hlsli"
