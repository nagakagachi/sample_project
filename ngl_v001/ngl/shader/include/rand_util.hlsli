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




#endif  // NGL_SHADER_RANDOM_UTIL_H