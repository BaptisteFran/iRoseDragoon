#include "iocpSocketSERVER.h"

#include <algorithm>
#include <cassert>

#include <windows.h>
#include <tchar.h>

#include "classLOG.h"
#include "classSTR.h"
#include "classUTIL.h"
#include "CRandom.h"
#include "ioDataPOOL.h"

// spin lock 4000으로 설정...
IOCPSocketSERVER::IOCPSocketSERVER(
	char *szName,
	BYTE btMulCPU,
	char cAddCPU,
	bool bManageSocketVerify) 
	: CCriticalSection( 4000 )
	, m_IOCP()
	, m_dwWorkerThreadCNT(0)
	, m_ServerName()
	, m_pAcceptTHREAD(nullptr)
	, m_ppWorkerTHREAD(nullptr)
	, sockets()
	, lastSocket(0)
	, m_bManageSocketVerify(bManageSocketVerify)
{
	m_ServerName.Set( szName );

	SYSTEM_INFO	sSystemInfo;
	::GetSystemInfo (&sSystemInfo);

#ifndef _DEBUG
	m_dwWorkerThreadCNT = sSystemInfo.dwNumberOfProcessors * btMulCPU + cAddCPU;
	if ( m_dwWorkerThreadCNT > 32 )
		m_dwWorkerThreadCNT = 32;
#else
	m_dwWorkerThreadCNT = 1;
#endif

	m_IOCP.OpenPort( m_dwWorkerThreadCNT );
}

IOCPSocketSERVER::~IOCPSocketSERVER ()
{
	this->Shutdown ();
	m_IOCP.ClosePort ();
}

bool IOCPSocketSERVER::Active(int iListenTCPPortNO, int iKeepAliveSec)
{
	if ( !m_IOCP.GetHANDLE() || allSockets.size() )
		return false;

	this->StartWORKER ();
	this->StartACCEPT (iListenTCPPortNO, iKeepAliveSec);

	return true;
}

void IOCPSocketSERVER::ShutdownSOCKET()
{
	for(auto &socketKVP : allSockets)
	{
		Del_SOCKET(socketKVP.first);
	}
}

void IOCPSocketSERVER::StartACCEPT(int iListenTCPPortNO, int iKeepAliveSec)
{
	if(NULL == m_pAcceptTHREAD)
	{
		m_pAcceptTHREAD = new IOCPSocketAcceptTHREAD(this);                // suspend mode
		if(!m_pAcceptTHREAD->Init(iListenTCPPortNO, iKeepAliveSec))
		{
			SAFE_DELETE(m_pAcceptTHREAD);
			return;// false;
		}
		m_pAcceptTHREAD->Resume();
	}
}

void IOCPSocketSERVER::ShutdownACCEPT()
{
	if(m_pAcceptTHREAD)
	{
		m_pAcceptTHREAD->Free();
		SAFE_DELETE(m_pAcceptTHREAD);
	}
}

void IOCPSocketSERVER::StartWORKER()
{
	if(NULL == m_ppWorkerTHREAD)
	{
		m_ppWorkerTHREAD = new IOCPSocketWorkerTHREAD*[m_dwWorkerThreadCNT];
		for(DWORD dwI = 0; dwI < m_dwWorkerThreadCNT; dwI++)
		{
			m_ppWorkerTHREAD[dwI] = new IOCPSocketWorkerTHREAD(this);
			m_ppWorkerTHREAD[dwI]->Init(m_IOCP.GetHANDLE(), dwI);
			m_ppWorkerTHREAD[dwI]->Resume();
		}
	}
}

void IOCPSocketSERVER::ShutdownWORKER()
{
	if(m_ppWorkerTHREAD)
	{
		for(DWORD dwI = 0; dwI < m_dwWorkerThreadCNT; dwI++)
		{
			m_ppWorkerTHREAD[dwI]->Terminate();

			if(0 == ::PostQueuedCompletionStatus(m_IOCP.GetHANDLE(), -1, 0, nullptr))
			{
				g_LOG.CS_ODS(
					0xffff,
					"ThreadWORKER::Free() PostQueuedCompletionStatus() return 0"
					", LastERROR: %d(0x%x)",
					GetLastError(),
					GetLastError());
			}
		}

		DWORD dwDead = 0;
		do
		{
			dwDead = 0;
			for(DWORD dwI = 0; dwI < m_dwWorkerThreadCNT; dwI++)
			{
				dwDead++;
				if(m_ppWorkerTHREAD[dwI])
				{
					if(m_ppWorkerTHREAD[dwI]->IsFinished())
					{
						SAFE_DELETE(m_ppWorkerTHREAD[dwI]);
					}
					else
					{
						dwDead--;
						::Sleep(10);
					}
				}
			}
		} while(dwDead < m_dwWorkerThreadCNT);

		SAFE_DELETE_ARRAY(m_ppWorkerTHREAD);
	}
}

void IOCPSocketSERVER::CloseIdleSCOKET(DWORD dwIdleMilliSec)
{
	DWORD dwCurTime = ::timeGetTime();

	this->Lock();
	{
		for(auto const &socketKVP : sockets)
		{
			auto const &socket = socketKVP.second;

			if(socket->m_bVerified)
			{
				Del_SOCKET(socketKVP.first);
				continue;
			}

			if(dwCurTime - socket->m_dwConnTIME >= dwIdleMilliSec)
			{
				g_LOG.CS_ODS(
					0xffff,
					"Close Idle SOCK:: %s:%d=%d-%d, %s\n",
					GetServerNAME(),
					dwCurTime - socket->m_dwConnTIME,
					dwCurTime,
					socket->m_dwConnTIME,
					socket->Get_IP());

				//socket->CloseSocket();

				Del_SOCKET(socketKVP.first);
			}
		}
	}
	this->Unlock();
}

bool IOCPSocketSERVER::New_SOCKET(SOCKET hSocket, sockaddr_in &SockADDR)
{
	char *szIP = ::inet_ntoa(SockADDR.sin_addr);
	t_HASHKEY ipHashKEY = CStrVAR::GetHASH(szIP);

	iocpSOCKET *pSOCKET = this->AllocClientSOCKET();
	if(!pSOCKET)
		return false;

	pSOCKET->Init_SCOKET();
	this->Lock();
	int completionKey = lastSocket.fetch_add(1) + 1;
	this->Unlock();

	pSOCKET->m_Socket = hSocket;
	pSOCKET->m_IP.Set(szIP);
	pSOCKET->m_HashKeyIP = ipHashKEY;

	if(!m_IOCP.LinkPort((HANDLE)hSocket, completionKey)
		|| eRESULT_PACKET_OK != pSOCKET->Recv_Start())
	{
		this->Lock();
		{
			// @버그 수정 : 2004. 7. 16 iSocketIDX대신
			// pSOCKET->m_iSocketIDX로 사용했던 실수가 있었음.
			// 아미 메모리 풀이 중복 해제되는 원인으루 추정됨...
			lastSocket.fetch_sub(1);
			// lock 외부에 있던거 안으로...
			// Shutdown함수호출시에서 Accept, Worker쓰레드가 종료되어 Free..함수가 호출될경우
			// ShutdownSocket과 충돌...
			this->FreeClientSOCKET(pSOCKET);

			g_LOG.CS_ODS(0xffff, "Failed to create new socket.\n");
		}
		this->Unlock();

		return false;
	}

	g_LOG.CS_ODS(0xffff, "Made new socket: %p, completion key: %d\n", pSOCKET, completionKey);

	if(m_bManageSocketVerify)
	{
		pSOCKET->m_dwConnTIME = ::timeGetTime();

		this->Lock();
		{
			sockets.emplace(completionKey, pSOCKET);
		}
		this->Unlock();
	}

	this->Lock();
	{
		auto deleter = [this](iocpSOCKET *socket)
		{
			FreeClientSOCKET(socket);
		};
		decltype(allSockets)::value_type::second_type ptr(pSOCKET, deleter);
		allSockets.emplace(completionKey, std::move(ptr));
	}
	this->Unlock();

	pSOCKET->m_iSocketIDX = completionKey;
	this->InitClientSOCKET(pSOCKET);

	return true;
}

// IOCPSocketSERVER::ShutdownSOCKET()
// IOCPSocketSERVER::On_FALSE()
// IOCPSocketSERVER::On_TRUE() 에서 호출됨
void IOCPSocketSERVER::Del_SOCKET(int iSocketIDX)
{
	this->Lock();
	{
		auto it = allSockets.find(iSocketIDX);
		if(it != allSockets.end())
		{
			auto &socket = it->second;
			sockets.erase(socket->m_iSocketIDX);
			socket->m_iSocketIDX = 0;
			socket->CloseSocket();
			allSockets.erase(it);
		}
		else
		{
			g_LOG.CS_ODS(0xffff, "Unable to delete socket: %d.\n", iSocketIDX);
		}
	}
	this->Unlock();
}

// 소켓 종료..
void IOCPSocketSERVER::On_FALSE(LPOVERLAPPED lpOverlapped, DWORD dwCompletionKey)
{
	tagIO_DATA *pIOData = (tagIO_DATA*)lpOverlapped;

	switch(pIOData->m_IOmode)
	{
	case ioREAD:
		// ioWRITE 일경우에는 pUSER->m_SendList에 노드가 이미 등록되어 있어 SubUser()에서 풀림으로 ioREAD일 때만...
		//iocpSOCKET::Free_RecvIODATA(pIOData);
		if(allSockets.size())
			allSockets[lastSocket]->PopRecvIO(pIOData);
		break;
	case ioWRITE:
		break;
	default:
		//assert(false);
		;
	}

	this->Del_SOCKET(dwCompletionKey);
}

void IOCPSocketSERVER::On_TRUE(
	LPOVERLAPPED lpOverlapped,
	DWORD dwCompletionKey,
	DWORD dwBytesIO)
{
	iocpSOCKET *pSOCKET = nullptr;
	tagIO_DATA *pIOData = (tagIO_DATA*)lpOverlapped;

	pSOCKET = this->GetSOCKET(dwCompletionKey);
	g_LOG.CS_ODS(0xffff, "Socket: %p, completion key: %d\n", pSOCKET, dwCompletionKey);

	if(pSOCKET)
	{
		// ** pUSER의 소켓이 종료된 상태에서도 이곳으로 온다 ㅡㅡ;
		pIOData->m_dwIOBytes += dwBytesIO;
		switch(pIOData->m_IOmode)
		{
		case ioREAD:
			switch(pSOCKET->Recv_Complete(pIOData))
			{
			case eRESULT_PACKET_DISCONNECT:
				// 짤러라 !!!
				g_LOG.CS_ODS(0xffff, "Deleting socket...");
				this->Del_SOCKET(dwCompletionKey);
				break;
			}
			break;
		case ioWRITE:	// 보내기 완료 !!!
			if(!pSOCKET->Send_Complete(pIOData))
			{
				// 짤러라 !!!
				this->Del_SOCKET(dwCompletionKey);
			}
			break;
		default:
			assert(false);
		}
	}
}
