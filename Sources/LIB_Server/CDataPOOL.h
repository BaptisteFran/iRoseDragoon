#ifndef	__CDATAPOOL_H
#define	__CDATAPOOL_H

#include <cassert>
#include <cstddef>

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include "classLOG.h"
#include "classSTR.h"

template <class t_DATA>
class CDataPOOL
{
public:
	char *GetPoolNAME()
	{
		return	poolName.Get();
	}

	size_t GetUsedCNT()
	{
		return size;
	}

	CDataPOOL(char *szName, size_t initialCount, size_t blockSize)
		: maxSize(initialCount)
		, size(0)
		, blockSize(blockSize)
		, poolName()
		, blocks()
		, lastFree(0, 0)
		, lock()
	{
		blocks.reserve(initialCount);
		poolName.Set(szName);

		Grow(initialCount);
	}

	virtual ~CDataPOOL() = default;

	t_DATA *Pool_Alloc()
	{
		std::lock_guard<std::mutex> guard(lock);

		size_t const x = lastFree.first, y = lastFree.second;

		if(blocks[x].FreeAt(y))
		{
			++size;
			blocks[x].freeList[y] = 0;
			t_DATA *ret = new (blocks[x].data[y].memory) t_DATA();

			g_LOG.CS_ODS(
				0xffff,
				"$$$ Notice:: Pool[ %s ] Allocating: 0x%p.\n",
				poolName.Get(),
				ret);

			return ret;
		}

		for(size_t i = x; i < blocks.size(); ++i)
		{
			Chunk &chunk = blocks[i];

			auto it = std::find_if(
				chunk.freeList.begin(),
				chunk.freeList.end(),
				[](int open)
				{
					return open == 1;
				});

			if(it != chunk.freeList.end())
			{
				IncLastFree();
				++size;
				*it = 0;
				size_t const idx = std::distance(chunk.freeList.begin(), it);
				t_DATA *ret = new (chunk.data[idx].memory) t_DATA();

				g_LOG.CS_ODS(
					0xffff,
					"$$$ Notice:: Pool[ %s ] Allocating: 0x%p.\n",
					poolName.Get(),
					ret);

				return ret;
			}
		}

		try
		{
			size_t lastEnd = blocks.size();
			Grow(blockSize);
			++size;
			blocks[lastEnd].freeList[0] = 0;
			t_DATA *ret = new (blocks[lastEnd].data[0].memory) t_DATA();

			g_LOG.CS_ODS(
				0xffff,
				"$$$ Notice:: Pool[ %s ] Allocating: 0x%p.\n",
				poolName.Get(),
				ret);

			return ret;
		}
		catch(std::bad_alloc &e)
		{
			g_LOG.CS_ODS(
				0xffff,
				">>>> ERROR:: Pool[ %s ] Out of memory.\n",
				poolName.Get());
		}

		return nullptr;
	}

	void Pool_Free(t_DATA *pDATA)
	{
		std::lock_guard<std::mutex> guard(lock);

		g_LOG.CS_ODS(
			0xffff,
			"$$$ Notice:: Pool[ %s ] Deallocating: 0x%p.\n",
			poolName.Get(),
			pDATA);

		unsigned char *memory = reinterpret_cast<unsigned char*>(pDATA);
		DataBlock *block = reinterpret_cast<DataBlock *>(memory);

		assert(block->parentIdx < blocks.size());
		Chunk &parent = blocks[block->parentIdx];

		size_t const idx = parent.BlockIndex(poolName, block);
		if(parent.FreeAt(idx))
		{
			g_LOG.CS_ODS(
				0xffff,
				">>>> ERROR:: Pool[ %s ] Duplicated free, size: %d\n",
				poolName.Get(),
				size);

			std::abort();
		}

		parent.freeList[idx] = 1;

		// Do we need to run destructors here?
		// Logic would deem this to be so, but the original code doesn't do it
		// either. Making me think the destructor is either not run at all,
		// or it's run before calling Pool_Free()...
		//pDATA.~t_DATA();

		lastFree = std::make_pair(block->parentIdx, idx);

		--size;
	}

private:
	struct Chunk;

	struct DataBlock
	{
		DataBlock(size_t parentIdx)
			: memory()
			, parentIdx(parentIdx)
		{
		}

		unsigned char memory[sizeof(t_DATA)];
		size_t parentIdx;
	};

	struct Chunk
	{
		Chunk(size_t size, size_t idx)
			: data(size, idx)
			, freeList(size, 1)
		{
		}

		bool Full() const
		{
			return std::all_of(
				freeList.cbegin(),
				freeList.cend(),
				[](int open)
				{
					return open == 0;
				});
		}

		bool FreeAt(size_t idx)
		{
			assert(idx < freeList.size());
			return freeList[idx];
		}

		size_t BlockIndex(CStrVAR const &poolName, DataBlock const *block)
		{
			ptrdiff_t diff = block - data.data();

			if(diff < 0)
			{
				g_LOG.CS_ODS(
					0xffff,
					">>>> ERROR:: Pool[ %s ] Attempt to find non-existent data block.\n",
					poolName.Get());

				std::abort();
			}

			size_t idx = size_t(diff);
			if(idx < data.size())
				return idx;

			g_LOG.CS_ODS(
				0xffff,
				">>>> ERROR:: Pool[ %s ] Attempt to find non-existent data block.\n",
				poolName.Get());

			std::abort();
		}

		std::vector<DataBlock> data;
		std::vector<int> freeList;
	};

	size_t maxSize, size, blockSize;
	CStrVAR poolName;
	std::vector<Chunk> blocks;
	std::pair<size_t, size_t> lastFree;

	std::mutex lock;

	void IncLastFree()
	{
		++lastFree.second;
		if(lastFree.second >= blockSize)
		{
			lastFree.second = 0;
			++lastFree.first;
		}

		if(lastFree.first >= blocks.size())
			lastFree.first = 0;
	}

protected:
	void Grow(size_t increase)
	{
		size_t left = increase;
		bool lastFreeSet = false;

		while(left > 0)
		{
			blocks.emplace_back(blockSize, blocks.size());

			if(!lastFreeSet)
			{
				lastFree = std::make_pair(blocks.size() - 1, size_t(0));
				lastFreeSet = true;
			}

			maxSize += blockSize;
			left -= std::min(blockSize, left);
		}

		g_LOG.CS_ODS(
			0xffff,
			">>>> Pool[ %s ] Increase %d data total: %d\n",
			poolName.Get(),
			increase,
			maxSize);
	}
};
#endif