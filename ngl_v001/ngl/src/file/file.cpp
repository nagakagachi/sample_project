
#include <fstream>
#include <vector>

#include "file/file.h"

namespace ngl
{
	namespace file
	{
		namespace
		{
			constexpr u64 k_fnv_offset_basis_64 = 14695981039346656037ULL;
			constexpr u64 k_fnv_prime_64 = 1099511628211ULL;

			u64 CalcFnv1aHash(const u8* data, size_t size)
			{
				u64 hash = k_fnv_offset_basis_64;
				for (size_t i = 0; i < size; ++i)
				{
					hash ^= static_cast<u64>(data[i]);
					hash *= k_fnv_prime_64;
				}
				return hash;
			}
		}

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

		bool ReadFileToBuffer(const char* filePath, std::vector<u8>& out_data)
		{
			out_data.clear();
			const u32 size = calcFileSize(filePath);
			if (0 >= size)
				return false;

			std::ifstream ifs(filePath, std::ios::binary);
			if (!ifs)
				return false;

			out_data.resize(size);
			ifs.read(reinterpret_cast<char*>(out_data.data()), size);
			return true;
		}

		bool WriteFileFromBuffer(const char* filePath, const void* data, size_t size)
		{
			if (!data || size == 0)
				return false;

			std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
			if (!ofs)
				return false;

			ofs.write(reinterpret_cast<const char*>(data), size);
			return ofs.good();
		}

		bool WriteFileFromBuffer(const char* filePath, const std::vector<u8>& data)
		{
			return WriteFileFromBuffer(filePath, data.data(), data.size());
		}

		u64 CalcFileHashFNV1a64(const char* filePath)
		{
			std::vector<u8> data;
			if (!ReadFileToBuffer(filePath, data))
				return 0;

			return CalcFnv1aHash(data.data(), data.size());
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