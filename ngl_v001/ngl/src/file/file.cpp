
#include <fstream>

#include "file/file.h"

namespace ngl
{
	namespace file
	{
		u32 calcFileSize(const char* filePath)
		{
			std::ifstream ifs(filePath, std::ios::binary);
			if (!ifs)
				return 0;

			std::streampos fhead = ifs.tellg();
			ifs.seekg(0, std::ios::end);
			std::streampos ftail = ifs.tellg();
			ifs.close();

			std::streampos fsize = ftail - fhead;
			return ( 0 < fsize) ? static_cast<u32>(fsize) : 0;
		}



		FileObject::FileObject()
		{
		}
		FileObject::FileObject(const char* filePath)
			: FileObject()
		{
			ReadFile(filePath);
		}
		FileObject::~FileObject()
		{
			Release();
		}
		void FileObject::Release()
		{
			fileData_.reset();
			fileSize_ = 0;
		}
		bool FileObject::ReadFile(const char* filePath)
		{
			Release();
			u32 size = calcFileSize(filePath);
			if (0 >= size)
				return false;

			std::ifstream ifs(filePath, std::ios::binary);
			if (!ifs)
				return false;

			fileSize_ = size;
			fileData_.reset(new u8[fileSize_]);
			ifs.read(reinterpret_cast<char*>(fileData_.get()), fileSize_);
			return true;
		}
	}
}