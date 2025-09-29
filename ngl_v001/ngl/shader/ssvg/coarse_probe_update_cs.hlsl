
#if 0

coarse_probe_update_cs.hlsl

#endif


#define INDIRECT_MODE 0
#define RAY_SAMPLE_COUNT_PER_VOXEL 4
#define FRAME_UPDATE_SKIP_THREAD_GROUP_COUNT 64
#define PROBE_UPDATE_TEMPORAL_RATE  (0.33)
#include "probe_update_base.hlsli"
