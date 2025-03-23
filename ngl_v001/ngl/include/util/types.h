#pragma once

#ifndef _NGL_UTIL_TYPES_
#define _NGL_UTIL_TYPES_

namespace ngl
{
	namespace types
	{
		typedef __int8			  s8;
		typedef __int16			 s16;
		typedef __int32			 s32;
		typedef __int64			 s64;
		typedef unsigned __int8	 u8;
		typedef unsigned __int16	u16;
		typedef unsigned __int32	u32;
		typedef unsigned __int64	u64;

		typedef float			   f32;
		typedef double			  f64;

	}
	using namespace types;
}

#define NGL_ARRAY_SIZE(p) sizeof(p)/sizeof(p[0])

#endif // _NGL_UTIL_TYPES_