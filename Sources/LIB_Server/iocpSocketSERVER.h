#ifndef	__IOCPSOCKETSERVER_H
#define	__IOCPSOCKETSERVER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "CAcceptTHREAD.h"
#include "CIocpTHREAD.h"
#include "classINDEX.h"
#include "classIOCP.h"
#include "classTIME.h"
#include "iocpSOCKET.h"

class IOCPSocketAcceptTHREAD;
class IOCPSocketWorkerTHREAD;
class IOCPSocketSERVER
{
protected:
	CIOCP			m_IOCP;
	DWORD			m_dwWorkerThreadCNT;
	DWORD			m_dwMaxSocketCNT;
	CStrVAR			m_ServerName;

	IOCPSocketAcceptTHREAD *m_pAcceptTHREAD;
	IOCPSocketWorkerTHREAD**m_ppWorkerTHREAD;

	std::unordered_map<int, std::unique_ptr<iocpSOCKET, std::function<void(iocpSOCKET*)>>> allSockets;
	std::unordered_map<int, iocpSOCKET*> sockets;
	std::atomic<size_t> lastSocket;
	bool m_bManageSocketVerify;
	std::mutex lock;

public	:
	// worker thread 갯수 = CPU갯수 * btMulCPUT + cAddCPUT
	IOCPSocketSERVER (char *szName, BYTE btMulCPU, char cAddCPU, bool bManageSocketVerify);
	~IOCPSocketSERVER ();

	char *GetServerNAME()
	{
		return this->m_ServerName.Get();
	}

	virtual iocpSOCKET*	AllocClientSOCKET()=0;					// 메모리할당
	virtual void InitClientSOCKET( iocpSOCKET *pCLIENT ) {}		// 접속 완료.. 초기화 할거 있음 해라..
	virtual void FreeClientSOCKET( iocpSOCKET *pCLIENT )=0;		// 검증없이 메모리 해제
	virtual void ClosedClientSOCKET( iocpSOCKET *pCLIENT )=0;	// 소켓이 삭제됐다.. 알아서 메모리 해제할것...

	inline iocpSOCKET* GetSOCKET( int iSocketIDX )		
	{	
		auto const it = allSockets.find(iSocketIDX);
		if ( it != allSockets.cend() )
			return it->second.get();
		return nullptr;
	}

	bool New_SOCKET (SOCKET hSocket, sockaddr_in &SockADDR);
	void Del_SOCKET ( int iSocketIDX );

	void On_TRUE (LPOVERLAPPED lpOverlapped, DWORD dwCompletionKey, DWORD dwBytesIO);
	void On_FALSE(LPOVERLAPPED lpOverlapped, DWORD dwCompletionKey);

	int  GetUsedSocketCNT()		
	{	
		return sockets.size();
	}
	// 모든 쓰레드 동작...
	bool Active (int iListenTCPPortNO, int iKeepAliveSec);
	void Shutdown ()
	{
		this->ShutdownACCEPT ();
		this->ShutdownWORKER ();
		this->ShutdownSOCKET ();
	}

	// 접속된 소켓 모두 종료
	void ShutdownSOCKET ();

	void StartACCEPT (int iListenTCPPortNO, int iKeepAliveSec);
	void ShutdownACCEPT ();

	void StartWORKER ();
	void ShutdownWORKER ();

	DWORD GetMaxSocketCOUNT()	{	return m_dwMaxSocketCNT;	}
	void  CloseIdleSCOKET (DWORD dwIdelMilliSec);
};

class IOCPSocketAcceptTHREAD: public CAcceptTHREAD
{
public:
	IOCPSocketAcceptTHREAD(IOCPSocketSERVER *pServer)
		: CAcceptTHREAD(true)
		, m_pIOCPSocketSERVER(pServer)
	{
	}

private:
	IOCPSocketSERVER   *m_pIOCPSocketSERVER;

    inline bool AcceptSOCKET(SOCKET hSocket, sockaddr_in &SockADDR)
	{
		return m_pIOCPSocketSERVER->New_SOCKET( hSocket, SockADDR );
	}
};

class IOCPSocketWorkerTHREAD: public CIocpTHREAD
{
public:
	IOCPSocketWorkerTHREAD(IOCPSocketSERVER *pServer)
		: CIocpTHREAD(true)
		, m_pIOCPSocketSERVER(pServer)
	{
	}

private:
	IOCPSocketSERVER   *m_pIOCPSocketSERVER;

    inline void STATUS_ReturnTRUE(
		LPOVERLAPPED lpOverlapped,
		DWORD dwCompletionKey,
		DWORD dwBytesIO)
	{
		m_pIOCPSocketSERVER->On_TRUE(lpOverlapped, dwCompletionKey, dwBytesIO);
	}
    inline void STATUS_ReturnFALSE(LPOVERLAPPED lpOverlapped, DWORD dwCompletionKey)
	{
		m_pIOCPSocketSERVER->On_FALSE(lpOverlapped, dwCompletionKey);
	}
};
#endif
