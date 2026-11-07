/* Pinned source/DT policy, not observed hardware. */
#include <stdint.h>
#define SHENG_GPU_SMMU_BASE UINT64_C(0x3da0000)
#define SHENG_GPU_SMMU_BYTES 262144u
#define SHENG_GPU_SMMU_GPU_STREAMS 2u
#define SHENG_GPU_SMMU_GMU_SID 5u
static const uint16_t sheng_gpu_smmu_sids[2]={0,1};
