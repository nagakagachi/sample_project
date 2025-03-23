#pragma once

#ifndef _NGL_UTIL_NON_COPYABLE_
#define _NGL_UTIL_NON_COPYABLE_

namespace ngl
{
	// Template版コピー禁止
	template <class T>
	class NonCopyableTp {
	protected:
		NonCopyableTp() {}
		virtual ~NonCopyableTp() {}
	private:
		NonCopyableTp(const NonCopyableTp&){}
		NonCopyableTp& operator=(const NonCopyableTp &) {}
	};
}
#endif // _NGL_UTIL_NON_COPYABLE_