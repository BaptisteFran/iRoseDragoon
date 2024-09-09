/*
	$Header: /7HeartsOnline/LIB_Server/ioDataPOOL.h 1     04-03-25 11:20a Icarus $
*/
#ifndef	__IODATA_POOL_H
#define	__IODATA_POOL_H
#include "LIB_Util.h"
#include "DLLIST.h"
#include "CDataPOOL.h"
#include "classPACKET.h"

typedef enum
{
	ioREAD,
	ioWRITE
	//  ClientIoWrite,
	//  ClientQoS
} IO_MODE, IO_OPERATION;

struct tagIO_DATA
{
	OVERLAPPED					m_Overlapped;
	IO_MODE						m_IOmode;
	DWORD						m_dwIOBytes;
	classPACKET				   *m_pCPacket;
	classDLLNODE<tagIO_DATA>   *m_pNODE;
};
typedef	classDLLNODE<tagIO_DATA>	IODATANODE;
typedef	classDLLNODE<tagIO_DATA>*	LPIODATANODE;

inline classPACKET *Packet_AllocOnly ()						
{
	auto packet = new classPACKET{};
	packet->SetRefCnt(0);
	packet->SetLength(0);
	return packet;
}

inline void Packet_ReleaseOnly (classPACKET *pCPacket)		
{
	delete pCPacket;
}

inline classPACKET *Packet_AllocNLock ()					
{
	auto packet = new classPACKET{};
	packet->SetRefCnt(1);
	packet->SetLength(0);
	return packet;
}

inline void Packet_ReleaseNUnlock (classPACKET *pCPacket)	
{
	delete pCPacket;
}

inline void Packet_DecRefCount (classPACKET *pCPacket)		
{	
	if(pCPacket->DecRefCnt() <= 0)
		delete pCPacket;
}
#endif
