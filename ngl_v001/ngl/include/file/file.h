#pragma once
#ifndef _NGL_FILE_H_
#define _NGL_FILE_H_

#include <cstddef>
#include <memory>
#include <vector>

#include "util/types.h"

namespace ngl
{
	namespace file
	{
		u32 calcFileSize(const char* filePath);

		bool ReadFileToBuffer(const char* filePath, std::vector<u8>& out_data);
		bool WriteFileFromBuffer(const char* filePath, const void* data, size_t size);
		bool WriteFileFromBuffer(const char* filePath, const std::vector<u8>& data);
		u64 CalcFileHashFNV1a64(const char* filePath);

		class FileObject
		{
		public:
			FileObject();
			FileObject( const char* filePath );
			~FileObject();
			// ファイルリード
			bool ReadFile(const char* filePath);
			// 解放
			void Release();

			u32 GetFileSize() const
			{
				return fileSize_;
			}
			const u8* GetFileData() const
			{
				return fileData_.get();
			}

		private:
			u32 fileSize_				= 0;
			std::unique_ptr<u8[]> fileData_ = {};
		};
	}
}

#endif //_NGL_FILE_H_