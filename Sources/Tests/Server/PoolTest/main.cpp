#include <cassert>
#include <cstdio>

#include <Windows.h>
#include <CDataPOOL.h>

struct TestData
{
	TestData()
		: flag(0)
	{
	}

	void Raise()
	{
		++flag;
	}

	bool Set() const
	{
		return flag;
	}

	int flag;
};

void WriteLOG(char *string)
{
	puts(string);
}

int main()
{
	CDataPOOL<TestData> pool((char*)"Test", 3, 8);

	TestData *first = pool.Pool_Alloc();
	TestData *second = pool.Pool_Alloc();

	first->Raise();
	second->Raise();
	second->Raise();

	pool.Pool_Free(first);
	pool.Pool_Free(second);

	second = pool.Pool_Alloc();
	first = pool.Pool_Alloc();

	assert(!second->Set());
	assert(!first->Set());

	pool.Pool_Free(first);
	pool.Pool_Free(second);

	return 0;
}
