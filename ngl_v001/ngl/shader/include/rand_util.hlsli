/*
    rand_util.hlsli
*/

#ifndef NGL_SHADER_RANDOM_UTIL_H
#define NGL_SHADER_RANDOM_UTIL_H

#include "math_util.hlsli"

// https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/914b91596cd119eda85fbc1d3c7ee6ac391b1452/src/core/src/components/random_number_generator/random_number_generator.hlsl
class RandomInstance
{
    uint rngState;

    /**
     * Generate a random uint.
     * @return The new random number (range  [0, 2^32)).
     */
    uint randInt()
    {
        // Using PCG hash function
        uint state = rngState;
        rngState = rngState * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word = (word >> 22u) ^ word;
        return word;
    }

    /**
     * Generate a random uint between 0 and a requested maximum value.
     * @return The new random number (range  [0, max)).
     */
    uint randInt(uint max)
    {
        uint ret = randInt() % max; // Has some bias depending on value of max
        return ret;
    }

    /**
     * Generate a random number.
     * @return The new random number (range [0->1.0)).
     */
    float rand()
    {
        // Note: Use the upper 24 bits to avoid a bias due to floating point rounding error.
        float ret = (float)(randInt() >> 8) * 0x1.0p-24f;
        return ret;
    }

    /**
     * Generate 2 random numbers.
     * @return The new numbers (range [0->1.0)).
     */
    float2 rand2()
    {
        return float2(rand(), rand());
    }

    /**
     * Generate 3 random numbers.
     * @return The new numbers (range [0->1.0)).
     */
    float3 rand3()
    {
        return float3(rand(), rand(), rand());
    }
    
    /**
     * Generate 4 random numbers.
     * @return The new numbers (range [0->1.0)).
     */
    float4 rand4()
    {
        return float4(rand(), rand(), rand(), rand());
    }
};


//--------------------------------------------------------------------------------
#define NGL_F32_PRESISION (2.3283064365386962890625e-10) // 2^-32
// https://www.shadertoy.com/view/4tXyWN
uint hash_uint32_iq(uint2 p)  
{  
    p *= uint2(73333, 7777);  
    p ^= uint2(3333777777, 3333777777) >> (p >> 28);  
    uint n = p.x * p.y;  
    return n ^ n >> 15;  
}
float noise_float_to_float(float pos)  
{  
    uint value = hash_uint32_iq(asuint(pos.xx));  
    return value * NGL_F32_PRESISION;
}
float noise_float_to_float(float2 pos)  
{  
    uint value = hash_uint32_iq(asuint(pos.xy));  
    return value * NGL_F32_PRESISION;
}
float noise_float_to_float(float3 pos)  
{  
    uint value = hash_uint32_iq(asuint(pos.xy)) + hash_uint32_iq(asuint(pos.zz));
    return value * NGL_F32_PRESISION;  
}
float noise_float_to_float(float4 pos)  
{  
    uint value = hash_uint32_iq(asuint(pos.xy)) + hash_uint32_iq(asuint(pos.zw));  
    return value * NGL_F32_PRESISION;  
}
float2 noise_float3_to_float2(float3 pos)
{  
    const uint3 uint_v3 = asuint(pos);
    const uint seed0 = uint_v3.x + (uint_v3.y ^ uint_v3.z);
    const uint seed1 = uint_v3.y + (uint_v3.z ^ uint_v3.x);
    const uint seed2 = uint_v3.z + (uint_v3.x ^ uint_v3.y);
    uint value0 = hash_uint32_iq(uint2(seed0, seed1));
    uint value1 = hash_uint32_iq(uint2(seed1, seed2));
    return uint2(value0, value1) * NGL_F32_PRESISION;  
}
float3 noise_float4_to_float3(float4 pos)
{  
    const uint4 uint_v4 = asuint(pos);
    const uint seed0 = uint_v4.x + (uint_v4.y ^ uint_v4.z);
    const uint seed1 = uint_v4.y + (uint_v4.z ^ uint_v4.w);
    const uint seed2 = uint_v4.z + (uint_v4.w ^ uint_v4.x);
    const uint seed3 = uint_v4.w + (uint_v4.x ^ uint_v4.y);
    uint value0 = hash_uint32_iq(uint2(seed0, seed1));
    uint value1 = hash_uint32_iq(uint2(seed1, seed2));
    uint value2 = hash_uint32_iq(uint2(seed2, seed3));
    return uint3(value0, value1, value2) * NGL_F32_PRESISION;  
}

float3 random_unit_vector3(float2 seed)
{
    const float angleY = noise_float_to_float(seed.xyxy) * NGL_2PI;
    const float angleX = asin(noise_float_to_float(seed.yyxx)*2.0 - 1.0);
    float3 sample_ray_dir;
    sample_ray_dir.y = sin(angleX);
    sample_ray_dir.x = cos(angleX) * cos(angleY);
    sample_ray_dir.z = cos(angleX) * sin(angleY);
    return sample_ray_dir;
}
float3 random_unit_vector3(float3 seed)
{
    const float angleY = noise_float_to_float(seed.xyzx) * NGL_2PI;
    const float angleX = asin(noise_float_to_float(seed.yzxz)*2.0 - 1.0);
    float3 sample_ray_dir;
    sample_ray_dir.y = sin(angleX);
    sample_ray_dir.x = cos(angleX) * cos(angleY);
    sample_ray_dir.z = cos(angleX) * sin(angleY);
    return sample_ray_dir;
}
// Interleaved Gradient Noise
// テクセル座標から疑似ランダムノイズを生成
// Jorge Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
float interleaved_gradient_noise(float2 pixel_coord)
{
    const float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel_coord, magic.xy)));
}
// Gold Noise
// https://www.shadertoy.com/view/ltB3zD
// Optimized hash function for golden ratio based noise
// Divergentな値で特定の値(994, 581)などを与えるとNaNになる謎の不具合があるため注意.
float gold_noise(float2 xy, float seed)
{
    return frac(tan(distance(xy * NGL_PHI, xy) * seed) * xy.x);
}

float gold_noise(float2 xy)
{
    return gold_noise(xy, 1.0);
}

float gold_noise(float3 xyz, float seed)
{
    return frac(tan(distance(xyz.xy * NGL_PHI, xyz.yz) * seed) * xyz.x);
}

float gold_noise(float3 xyz)
{
    return gold_noise(xyz, 1.0);
}



#endif  // NGL_SHADER_RANDOM_UTIL_H