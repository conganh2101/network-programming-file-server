#include "stdafx.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#define _CRT_SECURE_NO_WARNINGS
// Link to Mswsock.lib, Microsoft specific
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h> 
#include <process.h>

#include "dataStructures.h"
#include "processor.h"
#include "resolve.h"
#include "md5.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma warning(disable : 4996)

#define DEFAULT_BUFFER_SIZE         4096           // default buffer size
#define DEFAULT_OVERLAPPED_COUNT    5      // Number of overlapped recv per socket
#define MAX_OVERLAPPED_ACCEPTS      500
#define MAX_OVERLAPPED_SENDS        200
#define MAX_OVERLAPPED_RECVS        200
#define MAX_OVERLAPPED_READS        200
#define MAX_OVERLAPPED_WRITES       200
#define MAX_COMPLETION_THREAD_COUNT 32		// Maximum number of completion threads allowed
#define BURST_ACCEPT_COUNT          100
#define BUFF_SIZE                   2048
#define DIGEST_SIZE		            33

int gAddressFamily = AF_UNSPEC,         // default to unspecified
gSocketType = SOCK_STREAM,       // default to TCP socket type
gProtocol = IPPROTO_TCP,       // default to TCP protocol
gBufferSize = DEFAULT_BUFFER_SIZE,
gInitialAccepts = DEFAULT_OVERLAPPED_COUNT,
gMaxAccepts = MAX_OVERLAPPED_ACCEPTS,
gMaxReceives = MAX_OVERLAPPED_RECVS,
gMaxSends = MAX_OVERLAPPED_SENDS,
gMaxDownloads = MAX_OVERLAPPED_READS,
gMaxUploads = MAX_OVERLAPPED_WRITES;

char *gBindAddr = NULL,         // local interface to bind to
*gBindPort = "5500";       // local port to bind to

						   // Statistics counters
volatile LONG gBytesRead = 0, gBytesSent = 0, gStartTime = 0, gBytesReadLast = 0, gBytesSentLast = 0,
gStartTimeLast = 0, gConnections = 0, gConnectionsLast = 0, gOutstandingSends = 0, gOutstandingDownloads = 0, gOutstandingUploads = 0;

// Serialize access to the free lists below
CRITICAL_SECTION gBufferListCs, gSocketListCs, gPendingCritSec, gReadingCritSec, gWritingCritSec;

// Lookaside lists for free buffers and socket objects
BUFFER_OBJ *gFreeBufferList = NULL;
SOCKET_OBJ *gFreeSocketList = NULL;
BUFFER_OBJ *gPendingSendList = NULL, *gPendingSendListEnd = NULL;
BUFFER_OBJ *gPendingReadList = NULL, *gPendingReadListEnd = NULL;
BUFFER_OBJ *gPendingWriteList = NULL, *gPendingWriteListEnd = NULL;

int isFileExists(const char *path);
int  PostSend(SOCKET_OBJ *sock, BUFFER_OBJ *sendobj);
int  PostRecv(SOCKET_OBJ *sock, BUFFER_OBJ *recvobj);
void FreeBufferObj(BUFFER_OBJ *obj);
int usage(char *progname);
void dbgprint(char *format, ...);
void EnqueuePendingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, BUFFER_OBJ *obj, int op);
BUFFER_OBJ *DequeuePendingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, int op);
void ProcessPendingOperations();
void EnqueueDownloadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, BUFFER_OBJ *obj);
BUFFER_OBJ *DequeueDownloadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end);
void ProcessDownloadingOperations();
void EnqueueUploadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, BUFFER_OBJ *obj);
BUFFER_OBJ *DequeueUploadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end);
void ProcessUploadingOperations();
void InsertPendingAccept(LISTEN_OBJ *listenobj, BUFFER_OBJ *obj);
void RemovePendingAccept(LISTEN_OBJ *listenobj, BUFFER_OBJ *obj);
BUFFER_OBJ *GetBufferObj(int buflen);
SOCKET_OBJ *GetSocketObj(SOCKET s, int af);
void FreeSocketObj(SOCKET_OBJ *obj);
void ValidateArgs(int argc, char **argv);
void PrintStatistics();
int PostAccept(LISTEN_OBJ *listen, BUFFER_OBJ *acceptobj);
void HandleIo(ULONG_PTR key, BUFFER_OBJ *buf, HANDLE CompPort, DWORD BytesTransfered, DWORD error);
DWORD WINAPI CompletionThread(LPVOID lpParam);
unsigned __stdcall workerReadThread(void *param);
unsigned __stdcall workerWriteThread(void *param);

int _tmain(int argc, char* argv[])
{
	WSADATA          wsd;
	SYSTEM_INFO      sysinfo;
	LISTEN_OBJ      *ListenSockets = NULL, *listenobj = NULL;
	SOCKET_OBJ      *sockobj = NULL;
	BUFFER_OBJ      *acceptobj = NULL;
	GUID             guidAcceptEx = WSAID_ACCEPTEX, guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD            bytes;
	HANDLE           CompletionPort, WaitEvents[MAX_COMPLETION_THREAD_COUNT], hrc;
	int              endpointcount = 0, waitcount = 0, interval, rc, i;
	struct addrinfo *res = NULL, *ptr = NULL;

	if (argc < 2)
	{
		usage(argv[0]);
		exit(1);
	}

	// Validate the command line
	ValidateArgs(argc, argv);
	// Load Winsock
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
	{
		fprintf(stderr, "unable to load Winsock!\n");
		return -1;
	}

	if (initializeData()) return 1;

	InitializeCriticalSection(&gSocketListCs);
	InitializeCriticalSection(&gBufferListCs);
	InitializeCriticalSection(&gPendingCritSec);
	InitializeCriticalSection(&gReadingCritSec);
	InitializeCriticalSection(&gWritingCritSec);

	// Create the completion port used by this server
	CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
	if (CompletionPort == NULL)
	{
		fprintf(stderr, "CreateIoCompletionPort failed: %d\n", GetLastError());
		return -1;
	}

	// Find out how many processors are on this system
	GetSystemInfo(&sysinfo);
	if (sysinfo.dwNumberOfProcessors > MAX_COMPLETION_THREAD_COUNT)
	{
		sysinfo.dwNumberOfProcessors = MAX_COMPLETION_THREAD_COUNT;
	}

	// Round the buffer size to the next increment of the page size
	if ((gBufferSize % sysinfo.dwPageSize) != 0)
	{
		gBufferSize = ((gBufferSize / sysinfo.dwPageSize) + 1) * sysinfo.dwPageSize;
	}

	printf("Buffer size = %lu (page size = %lu)\n", gBufferSize, sysinfo.dwPageSize);

	// Create the worker threads to service the completion notifications
	for (waitcount = 0; waitcount < (int)sysinfo.dwNumberOfProcessors; waitcount++)
	{
		WaitEvents[waitcount] = CreateThread(NULL, 0, CompletionThread, (LPVOID)CompletionPort, 0, NULL);
		if (WaitEvents[waitcount] == NULL)
		{
			fprintf(stderr, "CreatThread failed: %d\n", GetLastError());
			return -1;
		}
	}

	printf("Local address: %s; Port: %s; Family: %d\n", gBindAddr, gBindPort, gAddressFamily);

	if (_beginthreadex(0, 0, workerReadThread, NULL, 0, 0) == 0) {
		printf("Create business thread failed with error %d\n", GetLastError());
		return 1;
	}
	else
		printf("Read file thread created.\n");
	if (_beginthreadex(0, 0, workerWriteThread, NULL, 0, 0) == 0) {
		printf("Create business thread failed with error %d\n", GetLastError());
		return 1;
	}
	else
		printf("Read file thread created.\n");

	// Obtain the "wildcard" addresses for all the available address families
	res = ResolveAddress(gBindAddr, gBindPort, gAddressFamily, gSocketType, gProtocol);
	if (res == NULL)
	{
		fprintf(stderr, "ResolveAddress failed to return any addresses!\n");
		return -1;
	}

	// For each local address returned, create a listening/receiving socket
	ptr = res;
	while (ptr)
	{
		printf("Listening address: ");
		PrintAddress(ptr->ai_addr, ptr->ai_addrlen);
		printf("\n");
		listenobj = (LISTEN_OBJ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LISTEN_OBJ));
		if (listenobj == NULL)
		{
			fprintf(stderr, "Out of memory!\n");
			return -1;
		}

		listenobj->LoWaterMark = gInitialAccepts;
		InitializeCriticalSection(&listenobj->ListenCritSec);

		// Save off the address family of this socket
		listenobj->AddressFamily = ptr->ai_family;

		// create the socket
		listenobj->s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (listenobj->s == INVALID_SOCKET)
		{
			fprintf(stderr, "socket failed: %d\n", WSAGetLastError());
			return -1;
		}

		// Create an event to register for FD_ACCEPT events on
		listenobj->AcceptEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (listenobj->AcceptEvent == NULL)
		{
			fprintf(stderr, "CreateEvent failed: %d\n", GetLastError());
			return -1;
		}

		listenobj->RepostAccept = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (listenobj->RepostAccept == NULL)
		{
			fprintf(stderr, "CreateSemaphore failed: %d\n", GetLastError());
			return -1;
		}

		// Add the event to the list of waiting events
		WaitEvents[waitcount++] = listenobj->AcceptEvent;
		WaitEvents[waitcount++] = listenobj->RepostAccept;

		// Associate the socket and its SOCKET_OBJ to the completion port
		hrc = CreateIoCompletionPort((HANDLE)listenobj->s, CompletionPort, (ULONG_PTR)listenobj, 0);
		if (hrc == NULL)
		{
			fprintf(stderr, "CreateIoCompletionPort failed: %d\n", GetLastError());
			return -1;
		}

		// bind the socket to a local address and port
		rc = bind(listenobj->s, ptr->ai_addr, ptr->ai_addrlen);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
			return -1;
		}

		// Need to load the Winsock extension functions from each provider
		//    -- e.g. AF_INET and AF_INET6.
		rc = WSAIoctl(
			listenobj->s,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&guidAcceptEx,
			sizeof(guidAcceptEx),
			&listenobj->lpfnAcceptEx,
			sizeof(listenobj->lpfnAcceptEx),
			&bytes,
			NULL,
			NULL
		);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "WSAIoctl: SIO_GET_EXTENSION_FUNCTION_POINTER failed: %d\n", WSAGetLastError());
			return -1;
		}

		// Load the Winsock extensions from each provider
		rc = WSAIoctl(
			listenobj->s,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&guidGetAcceptExSockaddrs,
			sizeof(guidGetAcceptExSockaddrs),
			&listenobj->lpfnGetAcceptExSockaddrs,
			sizeof(listenobj->lpfnGetAcceptExSockaddrs),
			&bytes,
			NULL,
			NULL
		);

		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "WSAIoctl: SIO_GET_EXTENSION_FUNCTION_POINTER failed: %d\n", WSAGetLastError());
			return -1;
		}

		// Put the socket into listening mode
		rc = listen(listenobj->s, 200);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "listen failed: %d\n", WSAGetLastError());
			return -1;
		}

		// Register for FD_ACCEPT notification on listening socket
		rc = WSAEventSelect(listenobj->s, listenobj->AcceptEvent, FD_ACCEPT);
		if (rc == SOCKET_ERROR)
		{
			fprintf(stderr, "WSAEventSelect failed: %d\n", WSAGetLastError());
			return -1;
		}

		// Initiate the initial accepts for each listen socket
		for (i = 0; i < gInitialAccepts; i++)
		{
			acceptobj = GetBufferObj(gBufferSize);
			if (acceptobj == NULL)
			{
				fprintf(stderr, "Out of memory!\n");
				return -1;
			}

			acceptobj->PostAccept = listenobj->AcceptEvent;
			InsertPendingAccept(listenobj, acceptobj);
			PostAccept(listenobj, acceptobj);
		}

		// Maintain a list of the listening socket structures
		if (ListenSockets == NULL)
		{
			ListenSockets = listenobj;
		}
		else
		{
			listenobj->next = ListenSockets;
			ListenSockets = listenobj;
		}

		endpointcount++;
		ptr = ptr->ai_next;
	}

	// free the addrinfo structure for the 'bind' address
	freeaddrinfo(res);
	gStartTime = gStartTimeLast = GetTickCount();
	interval = 0;
	while (1)
	{
		rc = WSAWaitForMultipleEvents(waitcount, WaitEvents, FALSE, 5000, FALSE);
		if (rc == WAIT_FAILED)
		{
			fprintf(stderr, "WSAWaitForMultipleEvents failed: %d\n", WSAGetLastError());
			break;
		}
		else if (rc == WAIT_TIMEOUT)
		{
			interval++;
			PrintStatistics();
			if (interval == 36)
			{
				int optval, optlen;

				// For TCP, cycle through all the outstanding AcceptEx operations
				//   to see if any of the client sockets have been connected but
				//   haven't received any data. If so, close them as they could be
				//   a denial of service attack.
				listenobj = ListenSockets;
				while (listenobj)
				{
					EnterCriticalSection(&listenobj->ListenCritSec);
					acceptobj = listenobj->PendingAccepts;

					while (acceptobj)
					{
						optlen = sizeof(optval);
						rc = getsockopt(acceptobj->sclient, SOL_SOCKET, SO_CONNECT_TIME, (char *)&optval, &optlen);
						if (rc == SOCKET_ERROR)
						{
							fprintf(stderr, "getsockopt: SO_CONNECT_TIME failed: %d\n", WSAGetLastError());
						}
						else
						{
							// If the socket has been connected for more than 5 minutes,
							//    close it. If closed, the AcceptEx call will fail in the completion thread.
							if ((optval != 0xFFFFFFFF) && (optval > 300))
							{
								printf("closing stale handle\n");
								closesocket(acceptobj->sclient);
								acceptobj->sclient = INVALID_SOCKET;
							}
						}
						acceptobj = acceptobj->next;
					}
					LeaveCriticalSection(&listenobj->ListenCritSec);
					listenobj = listenobj->next;
				}
				interval = 0;
			}
		}
		else
		{
			int index;
			index = rc - WAIT_OBJECT_0;
			for (; index < waitcount; index++)
			{
				rc = WaitForSingleObject(WaitEvents[index], 0);
				if (rc == WAIT_FAILED || rc == WAIT_TIMEOUT)
				{
					continue;
				}
				if (index < (int)sysinfo.dwNumberOfProcessors)
				{
					// One of the completion threads exited
					//   This is bad so just bail - a real server would want
					//   to gracefully exit but this is just a sample ...
					ExitProcess(-1);
				}
				else
				{
					// An FD_ACCEPT event occurred
					listenobj = ListenSockets;
					while (listenobj)
					{
						if ((listenobj->AcceptEvent == WaitEvents[index]) ||
							(listenobj->RepostAccept == WaitEvents[index]))
							break;
						listenobj = listenobj->next;
					}

					if (listenobj)
					{
						WSANETWORKEVENTS ne;
						int              limit = 0;
						if (listenobj->AcceptEvent == WaitEvents[index])
						{
							// EnumNetworkEvents to see if FD_ACCEPT was set
							rc = WSAEnumNetworkEvents(listenobj->s, listenobj->AcceptEvent, &ne);
							if (rc == SOCKET_ERROR)
							{
								fprintf(stderr, "WSAEnumNetworkEvents failed: %d\n", WSAGetLastError());
							}
							if ((ne.lNetworkEvents & FD_ACCEPT) == FD_ACCEPT)
							{
								// We got an FD_ACCEPT so post multiple accepts to cover the burst
								limit = BURST_ACCEPT_COUNT;
							}
						}
						else if (listenobj->RepostAccept == WaitEvents[index])
						{
							// Semaphore is signaled
							limit = InterlockedExchange(&listenobj->RepostCount, 0);
							ResetEvent(listenobj->RepostAccept);
						}
						i = 0;
						while ((i++ < limit) && (listenobj->PendingAcceptCount < gMaxAccepts))
						{
							acceptobj = GetBufferObj(gBufferSize);
							if (acceptobj)
							{
								acceptobj->PostAccept = listenobj->AcceptEvent;
								InsertPendingAccept(listenobj, acceptobj);
								PostAccept(listenobj, acceptobj);
							}
						}
					}
				}
			}
		}
	}

	WSACleanup();
	return 0;
}

// Function: usage
// Description: Prints usage information and exits the process.
int usage(char *progname)
{
	fprintf(stderr, "Usage: %s [-a 4|6] [-e port] [-l local-addr] [-p udp|tcp]\n", progname);
	fprintf(stderr, "  -a  4|6     Address family, 4 = IPv4, 6 = IPv6 [default = IPv4]\n"
		"else will listen to both IPv4 and IPv6\n"
		"  -b  size    Buffer size for send/recv [default = %d]\n"
		"  -e  port    Port number [default = %s]\n"
		"  -l  addr    Local address to bind to [default INADDR_ANY for IPv4 or INADDR6_ANY for IPv6]\n"
		"  -oa count   Maximum overlapped accepts to allow\n"
		"  -os count   Maximum overlapped sends to allow\n"
		"  -or count   Maximum overlapped receives to allow\n"
		"  -o  count   Initial number of overlapped accepts to post\n",
		gBufferSize,
		gBindPort
	);
	return 0;
}

// Function: dbgprint
// Description: Prints a message if compiled with the DEBUG flag.
void dbgprint(char *format, ...)
{
#ifdef DEBUG
	va_list vl;
	char    dbgbuf[2048];
	va_start(vl, format);
	wvsprintf(dbgbuf, format, vl);
	va_end(vl);
	printf(dbgbuf);
	OutputDebugString(dbgbuf);
#endif
}

int isFileExists(const char *path)
{
	// Try to open file
	FILE *fptr = fopen(path, "r");

	// If file does not exists 
	if (fptr == NULL)
		return 0;

	// File exists hence close file and return true.
	fclose(fptr);

	return 1;
}

// Function: EnqueuePendingOperation
// Description: Enqueues a buffer object into a list (at the end).
void EnqueuePendingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, BUFFER_OBJ *obj, int op)
{
	EnterCriticalSection(&gPendingCritSec);
	if (op == OP_READ);
	else if (op == OP_WRITE)
		InterlockedIncrement(&obj->sock->PendingSend);
	obj->next = NULL;
	if (*end)
	{
		(*end)->next = obj;
		(*end) = obj;
	}
	else
	{
		(*head) = (*end) = obj;
	}
	LeaveCriticalSection(&gPendingCritSec);

	return;
}
// Function: EnqueueDownloadingOperation
// Description: Enqueues a buffer object into a list (at the end).
// IN -BUFFER_OBJ **head: pointer to the address of head of Downloading buffer queue.
//    -BUFFER_OBJ **end: pointer to the address of end of Downloading buffer queue.
//    -BUFFER_OBJ *obj: pointer to buffer object to Downloading buffer queue.
void EnqueueDownloadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, BUFFER_OBJ *obj)
{

	EnterCriticalSection(&gReadingCritSec);

	obj->next = NULL;
	if (*end)
	{
		(*end)->next = obj;
		(*end) = obj;
	}
	else
	{
		(*head) = (*end) = obj;
	}

	LeaveCriticalSection(&gReadingCritSec);

	return;
}
// Function: EnqueueUploadingOperation
// Description: Enqueues a buffer object into a list (at the end).
// IN -BUFFER_OBJ **head: pointer to the address of head of Uploading buffer queue.
//    -BUFFER_OBJ **end: pointer to the address of end of Uploading buffer queue.
//    -BUFFER_OBJ *obj: pointer to buffer object to Uploading buffer queue 
void EnqueueUploadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, BUFFER_OBJ *obj)
{

	EnterCriticalSection(&gWritingCritSec);

	obj->next = NULL;
	if (*end)
	{
		(*end)->next = obj;
		(*end) = obj;
	}
	else
	{
		(*head) = (*end) = obj;
	}

	LeaveCriticalSection(&gWritingCritSec);

	return;
}

// Function: DequeuePendingOperation
// Description: Dequeues the first entry in the list.
BUFFER_OBJ *DequeuePendingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end, int op)
{
	BUFFER_OBJ *obj = NULL;
	EnterCriticalSection(&gPendingCritSec);

	if (*head)
	{
		obj = *head;
		(*head) = obj->next;

		// If next is NULL, no more objects are in the queue
		if (obj->next == NULL)
		{
			(*end) = NULL;
		}
		if (op == OP_READ)
			;
		else if (op == OP_WRITE)
			InterlockedDecrement(&obj->sock->PendingSend);
	}
	LeaveCriticalSection(&gPendingCritSec);
	return obj;
}
// Function: DequeueDownloadingOperation
// Description: Dequeues the first entry in the list.
// IN:  -BUFFER_OBJ **head:pointer to the address of head of Downloading buffer queue.
//      -BUFFER_OBJ **end :pointer to the address of end of Downloading buffer queue.
// OUT: -BUFFER_OBJ *     : poninter to the buffer object
BUFFER_OBJ *DequeueDownloadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end)
{
	BUFFER_OBJ *obj = NULL;
	EnterCriticalSection(&gReadingCritSec);

	if (*head)
	{
		obj = *head;
		(*head) = obj->next;

		// If next is NULL, no more objects are in the queue
		if (obj->next == NULL)
		{
			(*end) = NULL;
		}
	}
	LeaveCriticalSection(&gReadingCritSec);
	return obj;
}
// Function: DequeueUploadingOperation
// Description: Dequeues the first entry in the list.
// IN: -BUFFER_OBJ **head:pointer to address of the head of Uploading buffer queue.
//    -BUFFER_OBJ **end :pointer toaddress of  the end of Uploading buffer queue.
// OUT: -BUFFER_OBJ *     : poninter to the buffer object
BUFFER_OBJ *DequeueUploadingOperation(BUFFER_OBJ **head, BUFFER_OBJ **end)
{
	BUFFER_OBJ *obj = NULL;
	EnterCriticalSection(&gWritingCritSec);

	if (*head)
	{
		obj = *head;
		(*head) = obj->next;

		// If next is NULL, no more objects are in the queue
		if (obj->next == NULL)
		{
			(*end) = NULL;
		}
	}
	LeaveCriticalSection(&gWritingCritSec);
	return obj;
}

// Function: ProcessPendingOperations
// Description:
//    This function goes through the list of pending send operations and posts them
//    as long as the maximum number of outstanding sends is not exceeded.

void ProcessPendingOperations()
{
	fprintf(stderr, "sending");
	BUFFER_OBJ *sendobj = NULL;
	while (gOutstandingSends < gMaxSends)
	{
		sendobj = DequeuePendingOperation(&gPendingSendList, &gPendingSendListEnd, OP_WRITE);
		if (sendobj)
		{
			if (PostSend(sendobj->sock, sendobj) == SOCKET_ERROR)
			{
				// Cleanup
				printf("ProcessPendingOperations: PostSend failed!\n");
				FreeBufferObj(sendobj);
				break;
			}
		}
		else
		{
			break;
		}
	}
	return;
}
// Function: ProcessDownloadingOperations
// Description:
//    This function goes through the list of pending Downloading operations 
//    and process them end then postRecv or enqueuePendingOperations if needed
//    as long as the maximum number of outstanding downloads is not exceeded.
void ProcessDownloadingOperations()
{
	BUFFER_OBJ *readobj = NULL;
	MESSAGE sendMessage;
	BUFFER_OBJ *sendobj = NULL;
	BUFFER_OBJ *recvobj = NULL;
	while (gOutstandingDownloads < gMaxDownloads)
	{
		readobj = DequeueDownloadingOperation(&gPendingReadList, &gPendingReadListEnd);
		if (readobj)
		{
			MESSAGE rcvMess;
			rcvMess = readobj->sock->mess;
			if (rcvMess.opcode == OPT_FILE_DOWN)
			{
				printf("%s\n", rcvMess.payload);
				Account* account = NULL;
				char cookie[COOKIE_LEN];
				rcvMess.payload[COOKIE_LEN - 1] = 0;
				strcpy_s(cookie, COOKIE_LEN, rcvMess.payload);

				// Find account with cookie
				for (auto it = accountList.begin(); it != accountList.end(); it++) {
					if (strcmp(it->cookie, cookie) == 0) {
						account = (Account*)&(*it);
						break;
					}
				}

				if (account == NULL) {
					sendMessage.opcode = OPS_ERR_NOTFOUND;
					sendMessage.length = 0;
				}
				else
				{
					if (strlen(account->workingDir) > 0) {
						snprintf(readobj->sock->fileTransfer.fileName, FILENAME_SIZE, "%s/%s/%s/%s",
							STORAGE_LOCATION, account->workingGroup->pathName, account->workingDir, rcvMess.payload + COOKIE_LEN);
					}
					else {
						snprintf(readobj->sock->fileTransfer.fileName, FILENAME_SIZE, "%s/%s/%s",
							STORAGE_LOCATION, account->workingGroup->pathName, rcvMess.payload + COOKIE_LEN);
					}

					fprintf(stderr, "%s\n", readobj->sock->fileTransfer.fileName);
					if (isFileExists(readobj->sock->fileTransfer.fileName)) {
						FILE *file;
						//Open file
						file = fopen(readobj->sock->fileTransfer.fileName, "rb");
						if (!file)
						{
							fprintf(stderr, "Unable to open file %s", readobj->sock->fileTransfer.fileName);
							return;
						}
						//Get file length
						fseek(file, 0, SEEK_END);
						readobj->sock->fileTransfer.fileLen = ftell(file);
						fseek(file, 0, SEEK_SET);
						readobj->sock->fileTransfer.nLeft = readobj->sock->fileTransfer.fileLen;
						readobj->sock->fileTransfer.idx = 0;
						readobj->sock->fileTransfer.fileBuffer = (char*)malloc(readobj->sock->fileTransfer.fileLen + 1);
						if (!readobj->sock->fileTransfer.fileBuffer)
						{
							fprintf(stderr, "Memory error!");
							fclose(file);
							return;
						}
						fread(readobj->sock->fileTransfer.fileBuffer, readobj->sock->fileTransfer.fileLen, 1, file);
						fclose(file);//***************************
						MD5 md5;
						sendMessage.opcode = OPT_FILE_DIGEST;
						strcpy_s(sendMessage.payload, md5.digestFile(readobj->sock->fileTransfer.fileName));
						sendMessage.length = strlen(md5.digestFile(readobj->sock->fileTransfer.fileName));
					}
					else
					{
						sendMessage.opcode = OPS_ERR_NOTFOUND;
						strcpy_s(sendMessage.payload, readobj->sock->fileTransfer.fileName);
						sendMessage.length = strlen(readobj->sock->fileTransfer.fileName);
					}
				}
				memcpy(readobj->buf, &sendMessage, sizeof(MESSAGE));
				sendobj = readobj;
				sendobj->buflen = sizeof(MESSAGE);
				sendobj->sock = readobj->sock;
				EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);

			}
			else if (rcvMess.opcode == OPT_FILE_DATA || rcvMess.opcode == OPS_OK)
			{


				sendMessage.opcode = OPT_FILE_DATA;
				if (readobj->sock->fileTransfer.nLeft > BUFF_SIZE)
				{
					for (int i = 0; i < BUFF_SIZE; i++) {
						sendMessage.payload[i] = readobj->sock->fileTransfer.fileBuffer[readobj->sock->fileTransfer.idx + i];
					}
					sendMessage.length = BUFF_SIZE;
				}
				else
				{
					for (int i = 0; i < readobj->sock->fileTransfer.nLeft; i++) {
						sendMessage.payload[i] = readobj->sock->fileTransfer.fileBuffer[readobj->sock->fileTransfer.idx + i];
					}
					sendMessage.length = readobj->sock->fileTransfer.nLeft;
				}
				sendMessage.offset = readobj->sock->fileTransfer.idx;

				readobj->sock->fileTransfer.nLeft -= sendMessage.length;
				readobj->sock->fileTransfer.idx += sendMessage.length;

				memcpy(readobj->buf, &sendMessage, sizeof(MESSAGE));


				sendobj = readobj;
				sendobj->buflen = sizeof(MESSAGE);
				sendobj->sock = readobj->sock;
				//PostSend(sockobj, sendobj);
				EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);
			}
			else if (rcvMess.opcode == OPT_FILE_DIGEST)
			{
				recvobj = readobj;
				recvobj->sock = readobj->sock;
				PostRecv(readobj->sock, recvobj);
			}
			ProcessPendingOperations();
			InterlockedIncrement(&gOutstandingDownloads);

		}
		else
		{
			break;
		}
	}
	return;
}
// Function: ProcessUploadingOperations
// Description:
//    This function goes through the list of pending Uploading operations 
//    and process them end then postRecv or enqueuePendingOperations if needed
//    as long as the maximum number of outstanding uploads is not exceeded.
void ProcessUploadingOperations() {
	BUFFER_OBJ *writeobj = NULL;
	BUFFER_OBJ *rcvobj = NULL;
	BUFFER_OBJ *sendobj = NULL;
	MESSAGE sendMessage;
	while (gOutstandingUploads < gMaxUploads)
	{
		writeobj = DequeueUploadingOperation(&gPendingWriteList, &gPendingWriteListEnd);
		if (writeobj)
		{

			fprintf(stderr, "%s", writeobj->sock->fileTransfer.fileName);
			MESSAGE rcvMess;
			rcvMess = writeobj->sock->mess;
			if (rcvMess.opcode == OPT_FILE_UP)
			{
				Account* account = NULL;
				char cookie[COOKIE_LEN];
				rcvMess.payload[COOKIE_LEN - 1] = 0;
				strcpy_s(cookie, COOKIE_LEN, rcvMess.payload);

				// Find account with cookie
				for (auto it = accountList.begin(); it != accountList.end(); it++) {
					if (strcmp(it->cookie, cookie) == 0) {
						account = (Account*)&(*it);
						break;
					}
				}

				if (account == NULL) {
					sendMessage.opcode = OPS_ERR_NOTFOUND;
					sendMessage.length = 0;
				}
				else
				{
					if (strlen(account->workingDir) > 0) {
					snprintf(writeobj->sock->fileTransfer.fileName, FILENAME_SIZE, "%s/%s/%s/%s",
						STORAGE_LOCATION, account->workingGroup->pathName, account->workingDir, rcvMess.payload + COOKIE_LEN);
					}
					else {
						snprintf(writeobj->sock->fileTransfer.fileName, FILENAME_SIZE, "%s/%s/%s",
							STORAGE_LOCATION, account->workingGroup->pathName, rcvMess.payload + COOKIE_LEN);
					}

					// strcat_s(writeobj->sock->fileTransfer.fileName, rcvMess.payload);
					fprintf(stderr, "%s\n", writeobj->sock->fileTransfer.fileName);

					if (!isFileExists(writeobj->sock->fileTransfer.fileName))
					{
						writeobj->sock->fileTransfer.file = fopen(writeobj->sock->fileTransfer.fileName, "wb");
						if (!writeobj->sock->fileTransfer.file)
						{
							fprintf(stderr, "Unable to open file ");
							return;
						}
						sendMessage.opcode = OPS_OK;
						strcpy_s(sendMessage.payload, writeobj->sock->fileTransfer.fileName);
						sendMessage.length = strlen(writeobj->sock->fileTransfer.fileName);
					}
					else
					{
						sendMessage.opcode = OPS_ERR_ALREADYEXISTS;
						strcpy_s(sendMessage.payload, writeobj->sock->fileTransfer.fileName);
						sendMessage.length = strlen(writeobj->sock->fileTransfer.fileName);
					}

				}
				memcpy(writeobj->buf, &sendMessage, sizeof(MESSAGE));
				sendobj = writeobj;
				sendobj->buflen = sizeof(MESSAGE);
				sendobj->sock = writeobj->sock;
				EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);
				
			}
			else if (rcvMess.opcode == OPS_OK)
			{
				rcvobj = writeobj;
				rcvobj->sock = writeobj->sock;
				PostRecv(writeobj->sock, rcvobj);
			}
			else if (rcvMess.opcode == OPT_FILE_DATA)
			{
				if (rcvMess.length == 0)
				{
					fclose(writeobj->sock->fileTransfer.file);
					MD5 md5;
					if (strcmp(md5.digestFile(writeobj->sock->fileTransfer.fileName), writeobj->sock->fileTransfer.digest) == 0)
					{
						fprintf(stderr, "\n%s", writeobj->sock->fileTransfer.digest);


						sendMessage.opcode = OPS_SUCCESS;
						strcpy_s(sendMessage.payload, writeobj->sock->fileTransfer.fileName);
						sendMessage.length = strlen(writeobj->sock->fileTransfer.fileName);


						memcpy(writeobj->buf, &sendMessage, sizeof(MESSAGE));
						sendobj = writeobj;
						sendobj->buflen = sizeof(MESSAGE);
						sendobj->sock = writeobj->sock;
						EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);
					}
					else
					{
						fprintf(stderr, "corrupted\n");
						fprintf(stderr, "\n%s", writeobj->sock->fileTransfer.digest);
						if (remove(writeobj->sock->fileTransfer.fileName) != 0)
						{
							fprintf(stderr, "Error deleting file");
							return;
						}
						else
							puts("File successfully deleted");

						MESSAGE sendMessage;

						sendMessage.opcode = OPS_ERR_FILE_CORRUPTED;
						strcpy_s(sendMessage.payload, writeobj->sock->fileTransfer.fileName);
						sendMessage.length = strlen(writeobj->sock->fileTransfer.fileName);


						memcpy(writeobj->buf, &sendMessage, sizeof(MESSAGE));
						sendobj = writeobj;
						sendobj->buflen = sizeof(MESSAGE);
						sendobj->sock = writeobj->sock;
						EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);
					}
				}
				else
				{
					fprintf(stderr, "writting at:%d\n", rcvMess.offset);
					fseek(writeobj->sock->fileTransfer.file, rcvMess.offset, SEEK_SET);
					fwrite(rcvMess.payload, 1, rcvMess.length, writeobj->sock->fileTransfer.file);
					rcvobj = writeobj;
					rcvobj->sock = writeobj->sock;
					PostRecv(writeobj->sock, rcvobj);
				}
			}
			else if (rcvMess.opcode = OPT_FILE_DIGEST)
			{

				strcpy_s(writeobj->sock->fileTransfer.digest, rcvMess.payload);
				fprintf(stderr, "\n%s\n", writeobj->sock->fileTransfer.digest);
				rcvobj = writeobj;
				rcvobj->sock = writeobj->sock;
				PostRecv(writeobj->sock, rcvobj);

			}
			ProcessPendingOperations();
			//ProcessPendingOperations();
			InterlockedIncrement(&gOutstandingUploads);

		}
		else
		{
			break;
		}
	}
	return;
}

// Function: InsertPendingAccept
// Description: Inserts a pending accept operation into the listening object.
void InsertPendingAccept(LISTEN_OBJ *listenobj, BUFFER_OBJ *obj)
{

	obj->next = NULL;
	EnterCriticalSection(&listenobj->ListenCritSec);
	if (listenobj->PendingAccepts == NULL)
	{
		listenobj->PendingAccepts = obj;
	}
	else
	{
		// Insert at head - order doesn't really matter
		obj->next = listenobj->PendingAccepts;
		listenobj->PendingAccepts = obj;
	}
	LeaveCriticalSection(&listenobj->ListenCritSec);
}

// Function: RemovePendingAccept
// Description:
//    Removes the indicated accept buffer object from the list of pending
//    accepts in the listening object.

void RemovePendingAccept(LISTEN_OBJ *listenobj, BUFFER_OBJ *obj)
{
	BUFFER_OBJ *ptr = NULL, *prev = NULL;
	EnterCriticalSection(&listenobj->ListenCritSec);
	// Search list until we find the object
	ptr = listenobj->PendingAccepts;
	while ((ptr) && (ptr != obj))
	{
		prev = ptr;
		ptr = ptr->next;
	}
	if (prev)
	{
		// Object is somewhere after the first entry
		prev->next = obj->next;
	}
	else
	{
		// Object is the first entry
		listenobj->PendingAccepts = obj->next;
	}
	LeaveCriticalSection(&listenobj->ListenCritSec);
}

// Function: GetBufferObj
// Description:
//    Allocate a BUFFER_OBJ. A look aside list is maintained to increase performance
//    as these objects are allocated frequently.

BUFFER_OBJ *GetBufferObj(int buflen)
{
	BUFFER_OBJ *newobj = NULL;
	EnterCriticalSection(&gBufferListCs);
	if (gFreeBufferList == NULL)
	{
		// Allocate the object
		newobj = (BUFFER_OBJ *)HeapAlloc(
			GetProcessHeap(),
			HEAP_ZERO_MEMORY,
			sizeof(BUFFER_OBJ) + (sizeof(BYTE) * buflen)
		);
		if (newobj == NULL)
		{
			fprintf(stderr, "GetBufferObj: HeapAlloc failed: %d\n", GetLastError());
		}
	}
	else
	{
		newobj = gFreeBufferList;
		gFreeBufferList = newobj->next;
		newobj->next = NULL;
	}
	LeaveCriticalSection(&gBufferListCs);

	if (newobj)
	{
		newobj->buf = (char *)(((char *)newobj) + sizeof(BUFFER_OBJ));
		newobj->buflen = buflen;
		newobj->addrlen = sizeof(newobj->addr);
	}

	return newobj;
}

// Function: FreeBufferObj
// Description: Free the buffer object. This adds the object to the free look aside list.

void FreeBufferObj(BUFFER_OBJ *obj)
{
	EnterCriticalSection(&gBufferListCs);
	memset(obj, 0, sizeof(BUFFER_OBJ) + gBufferSize);
	obj->next = gFreeBufferList;
	gFreeBufferList = obj;
	LeaveCriticalSection(&gBufferListCs);
}

// Function: GetSocketObj
// Description:
//    Allocate a socket object and initialize its members. A socket object is
//    allocated for each socket created (either by socket or accept).
//    Socket objects are returned from a look aside list if available. Otherwise, a new object is allocated.

SOCKET_OBJ *GetSocketObj(SOCKET s, int af)
{
	SOCKET_OBJ  *sockobj = NULL;
	EnterCriticalSection(&gSocketListCs);
	if (gFreeSocketList == NULL)
	{
		sockobj = (SOCKET_OBJ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SOCKET_OBJ));
		if (sockobj == NULL)
		{
			fprintf(stderr, "GetSocketObj: HeapAlloc failed: %d\n", GetLastError());
		}
		else
		{
			InitializeCriticalSection(&sockobj->SockCritSec);
		}
	}
	else
	{
		sockobj = gFreeSocketList;
		gFreeSocketList = sockobj->next;
		sockobj->next = NULL;
	}
	LeaveCriticalSection(&gSocketListCs);

	// Initialize the members
	if (sockobj)
	{
		sockobj->s = s;
		sockobj->af = af;
	}
	return sockobj;
}

// Function: FreeSocketObj
// Description: Frees a socket object. The object is added to the lookaside list.

void FreeSocketObj(SOCKET_OBJ *obj)
{
	CRITICAL_SECTION cstmp;
	BUFFER_OBJ      *ptr = NULL;

	// Close the socket if it hasn't already been closed
	if (obj->s != INVALID_SOCKET)
	{
		printf("FreeSocketObj: closing socket\n");
		closesocket(obj->s);
		obj->s = INVALID_SOCKET;
	}

	EnterCriticalSection(&gSocketListCs);
	cstmp = obj->SockCritSec;
	memset(obj, 0, sizeof(SOCKET_OBJ));
	obj->SockCritSec = cstmp;
	obj->next = gFreeSocketList;
	gFreeSocketList = obj;
	LeaveCriticalSection(&gSocketListCs);
}

// Function: ValidateArgs
// Description: Parses the command line arguments and sets up some global variables.
void ValidateArgs(int argc, char **argv)
{
	int     i;

	for (i = 1; i < argc; i++)
	{
		if (((argv[i][0] != '/') && (argv[i][0] != '-')) || (strlen(argv[i]) < 2))
			usage(argv[0]);
		else
		{
			switch (tolower(argv[i][1]))
			{
			case 'a':               // address family - IPv4 or IPv6
				if (i + 1 >= argc)
					usage(argv[0]);
				if (argv[i + 1][0] == '4')
					gAddressFamily = AF_INET;
				else if (argv[i + 1][0] == '6')
					gAddressFamily = AF_INET6;
				else
					usage(argv[0]);
				i++;
				break;

			case 'b':               // buffer size for send/recv
				if (i + 1 >= argc)
					usage(argv[0]);
				gBufferSize = atol(argv[++i]);
				break;

			case 'e':               // endpoint - port number
				if (i + 1 >= argc)
					usage(argv[0]);
				gBindPort = argv[++i];
				break;

			case 'l':               // local address for binding
				if (i + 1 >= argc)
					usage(argv[0]);
				gBindAddr = argv[++i];
				break;

			case 'o':               // overlapped count
				if (i + 1 >= argc)
					usage(argv[0]);
				if (strlen(argv[i]) == 2)       // Overlapped accept initial count
				{
					gInitialAccepts = atol(argv[++i]);
				}
				else if (strlen(argv[i]) == 3)
				{
					if (tolower(argv[i][2]) == 'a')
						gMaxAccepts = atol(argv[++i]);
					else if (tolower(argv[i][2]) == 's')
						gMaxSends = atol(argv[++i]);
					else if (tolower(argv[i][2]) == 'r')
						gMaxReceives = atol(argv[++i]);
					else
						usage(argv[0]);
				}
				else
				{
					usage(argv[0]);
				}
				break;

			default:
				usage(argv[0]);
				break;
			}
		}
	}
}

// Function: PrintStatistics
// Description: Print the send/recv statistics for the server
void PrintStatistics()
{
	ULONG       bps, tick, elapsed, cps;
	tick = GetTickCount();
	elapsed = (tick - gStartTime) / 1000;

	if (elapsed == 0)
		return;

	printf("\n");

	// Calculate average bytes per second
	bps = gBytesSent / elapsed;
	printf("Average BPS sent: %lu [%lu]\n", bps, gBytesSent);
	bps = gBytesRead / elapsed;
	printf("Average BPS read: %lu [%lu]\n", bps, gBytesRead);
	elapsed = (tick - gStartTimeLast) / 1000;

	if (elapsed == 0)
		return;

	// Calculate bytes per second over the last X seconds
	bps = gBytesSentLast / elapsed;
	printf("Current BPS sent: %lu\n", bps);
	bps = gBytesReadLast / elapsed;
	printf("Current BPS read: %lu\n", bps);
	cps = gConnectionsLast / elapsed;
	printf("Current conns/sec: %lu\n", cps);
	printf("Total connections: %lu\n", gConnections);
	InterlockedExchange(&gBytesSentLast, 0);
	InterlockedExchange(&gBytesReadLast, 0);
	InterlockedExchange(&gConnectionsLast, 0);
	gStartTimeLast = tick;
}

// Function: PostRecv
// Description: Post an overlapped receive operation on the socket.
int PostRecv(SOCKET_OBJ *sock, BUFFER_OBJ *recvobj)
{
	WSABUF  wbuf;
	DWORD   bytes, flags;
	int     rc;

	recvobj->operation = OP_READ;
	wbuf.buf = recvobj->buf;
	wbuf.len = sizeof(MESSAGE);
	flags = 0;
	EnterCriticalSection(&sock->SockCritSec);
	rc = WSARecv(sock->s, &wbuf, 1, &bytes, &flags, &recvobj->ol, NULL);

	if (rc == SOCKET_ERROR)
	{
		rc = NO_ERROR;
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			dbgprint("PostRecv: WSARecv* failed: %d\n", WSAGetLastError());
			rc = SOCKET_ERROR;
		}
	}
	if (rc == NO_ERROR)
	{
		// Increment outstanding overlapped operations
		InterlockedIncrement(&sock->OutstandingRecv);
	}
	LeaveCriticalSection(&sock->SockCritSec);
	return rc;
}

// Function: PostSend
// Description: Post an overlapped send operation on the socket.

int PostSend(SOCKET_OBJ *sock, BUFFER_OBJ *sendobj)
{
	WSABUF  wbuf;
	DWORD   bytes;
	int     rc, err;

	sendobj->operation = OP_WRITE;
	wbuf.buf = sendobj->buf;
	wbuf.len = sizeof(MESSAGE);
	EnterCriticalSection(&sock->SockCritSec);
	rc = WSASend(sock->s, &wbuf, 1, &bytes, 0, &sendobj->ol, NULL);

	if (rc == SOCKET_ERROR)
	{
		rc = NO_ERROR;
		if ((err = WSAGetLastError()) != WSA_IO_PENDING)
		{
			if (err == WSAENOBUFS)
				DebugBreak();
			dbgprint("PostSend: WSASend* failed: %d [internal = %d]\n", WSAGetLastError(), sendobj->ol.Internal);
			rc = SOCKET_ERROR;
		}
	}
	if (rc == NO_ERROR)
	{
		// Increment the outstanding operation count
		InterlockedIncrement(&sock->OutstandingSend);
		InterlockedIncrement(&gOutstandingSends);
	}
	LeaveCriticalSection(&sock->SockCritSec);
	return rc;
}

// Function: PostAccept
// Description: Post an overlapped accept on a listening socket.
int PostAccept(LISTEN_OBJ *listen, BUFFER_OBJ *acceptobj)
{
	DWORD   bytes;
	int     rc;

	acceptobj->operation = OP_ACCEPT;

	// Create the client socket for an incoming connection
	acceptobj->sclient = socket(listen->AddressFamily, SOCK_STREAM, IPPROTO_TCP);
	if (acceptobj->sclient == INVALID_SOCKET)
	{
		fprintf(stderr, "PostAccept: socket failed: %d\n", WSAGetLastError());
		return -1;
	}

	rc = listen->lpfnAcceptEx(
		listen->s,
		acceptobj->sclient,
		acceptobj->buf,
		acceptobj->buflen - ((sizeof(SOCKADDR_STORAGE) + 16) * 2),
		sizeof(SOCKADDR_STORAGE) + 16,
		sizeof(SOCKADDR_STORAGE) + 16,
		&bytes,
		&acceptobj->ol
	);
	if (rc == FALSE)
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			printf("PostAccept: AcceptEx failed: %d\n", WSAGetLastError());
			return SOCKET_ERROR;
		}
	}

	// Increment the outstanding overlapped count for this socket
	InterlockedIncrement(&listen->PendingAcceptCount);
	return NO_ERROR;
}

// Function: HandleIo
// Description:
//    This function handles the IO on a socket. In the event of a receive, the
//    completed receive is posted again. For completed accepts, another AcceptEx
//    is posted. For completed sends, the buffer is freed.

void HandleIo(ULONG_PTR key, BUFFER_OBJ *buf, HANDLE CompPort, DWORD BytesTransfered, DWORD error)
{
	LISTEN_OBJ *listenobj = NULL;
	SOCKET_OBJ *sockobj = NULL,
		*clientobj = NULL;                     // New client object for accepted connections
	BUFFER_OBJ *recvobj = NULL,       // Used to post new receives on accepted connections
		*sendobj = NULL;
	BUFFER_OBJ *readobj = NULL,
		*writeobj = NULL;
	BOOL        bCleanupSocket;

	if (error != 0)
	{
		dbgprint("OP = %d; Error = %d\n", buf->operation, error);
	}
	bCleanupSocket = FALSE;

	if (error != NO_ERROR)
	{
		// An error occurred on a TCP socket, free the associated per I/O buffer
		// and see if there are any more outstanding operations. If so we must
		// wait until they are complete as well.
		if (buf->operation != OP_ACCEPT)
		{
			sockobj = (SOCKET_OBJ *)key;
			if (buf->operation == OP_READ)
			{
				if ((InterlockedDecrement(&sockobj->OutstandingRecv) == 0) && (sockobj->OutstandingSend == 0))
				{
					dbgprint("Freeing socket obj in GetOverlappedResult\n");
					FreeSocketObj(sockobj);
				}
			}
			else if (buf->operation == OP_WRITE)
			{
				if ((InterlockedDecrement(&sockobj->OutstandingSend) == 0) && (sockobj->OutstandingRecv == 0))
				{
					dbgprint("Freeing socket obj in GetOverlappedResult\n");
					FreeSocketObj(sockobj);
				}
			}

		}
		else
		{
			listenobj = (LISTEN_OBJ *)key;
			printf("Accept failed\n");
			closesocket(buf->sclient);
			buf->sclient = INVALID_SOCKET;
		}
		FreeBufferObj(buf);
		return;
	}

	if (buf->operation == OP_ACCEPT)
	{
		HANDLE            hrc;
		SOCKADDR_STORAGE *LocalSockaddr = NULL, *RemoteSockaddr = NULL;
		int               LocalSockaddrLen, RemoteSockaddrLen;
		listenobj = (LISTEN_OBJ *)key;

		// Update counters
		InterlockedIncrement(&gConnections);
		InterlockedIncrement(&gConnectionsLast);
		InterlockedDecrement(&listenobj->PendingAcceptCount);
		InterlockedExchangeAdd(&gBytesRead, BytesTransfered);
		InterlockedExchangeAdd(&gBytesReadLast, BytesTransfered);

		// Print the client's addresses
		listenobj->lpfnGetAcceptExSockaddrs(
			buf->buf,
			buf->buflen - ((sizeof(SOCKADDR_STORAGE) + 16) * 2),
			sizeof(SOCKADDR_STORAGE) + 16,
			sizeof(SOCKADDR_STORAGE) + 16,
			(SOCKADDR **)&LocalSockaddr,
			&LocalSockaddrLen,
			(SOCKADDR **)&RemoteSockaddr,
			&RemoteSockaddrLen
		);

		RemovePendingAccept(listenobj, buf);
		// Get a new SOCKET_OBJ for the client connection
		clientobj = GetSocketObj(buf->sclient, listenobj->AddressFamily);
		if (clientobj)
		{
			// Associate the new connection to our completion port
			hrc = CreateIoCompletionPort((HANDLE)clientobj->s, CompPort, (ULONG_PTR)clientobj, 0);
			if (hrc == NULL)
			{
				fprintf(stderr, "CompletionThread: CreateIoCompletionPort failed: %d\n", GetLastError());
				return;
			}
			MESSAGE *rcvMess;
			//MESSAGE sendMessage;
			rcvMess = (MESSAGE *)buf->buf;
			fprintf(stderr, "begin\n");

			if (rcvMess->opcode == OPT_FILE_DOWN)
			{

				InterlockedDecrement(&gOutstandingDownloads);
				readobj = buf;
				//writeobj->buf = (char *)rcvMess;
				readobj->sock = clientobj;
				readobj->sock->mess = *rcvMess;
				EnqueueDownloadingOperation(&gPendingReadList, &gPendingReadListEnd, readobj);

			}
			else if (rcvMess->opcode == OPT_FILE_UP)
			{
				InterlockedDecrement(&gOutstandingUploads);
				recvobj = buf;
				recvobj->sock = clientobj;
				recvobj->sock->mess = *rcvMess;
				EnqueueUploadingOperation(&gPendingWriteList, &gPendingWriteListEnd, recvobj);

			}
			else if (rcvMess->opcode == OPA_LOGIN || rcvMess->opcode == OPA_REAUTH) {
				recvobj = buf;
				recvobj->sock = clientobj;
				recvobj->sock->mess = *rcvMess;



				if (parseAndProcess(recvobj)) {
					memcpy(buf->buf, &recvobj->sock->mess, sizeof(MESSAGE));
					sendobj = buf;
					sendobj->sock = clientobj;
					
					EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);
					ProcessPendingOperations();
				}
			}

		}
		else
		{
			// Can't allocate a socket structure so close the connection
			closesocket(buf->sclient);
			buf->sclient = INVALID_SOCKET;
			FreeBufferObj(buf);
		}

		if (error != NO_ERROR)
		{
			// Check for socket closure
			EnterCriticalSection(&clientobj->SockCritSec);
			if ((clientobj->OutstandingSend == 0) && (clientobj->OutstandingRecv == 0))
			{
				closesocket(clientobj->s);
				clientobj->s = INVALID_SOCKET;
				FreeSocketObj(clientobj);
			}
			else
			{
				clientobj->bClosing = TRUE;
			}
			LeaveCriticalSection(&clientobj->SockCritSec);
			error = NO_ERROR;
		}
		InterlockedIncrement(&listenobj->RepostCount);
		SetEvent(listenobj->RepostAccept);
	}

	else if (buf->operation == OP_READ)
	{
		sockobj = (SOCKET_OBJ *)key;
		InterlockedDecrement(&sockobj->OutstandingRecv);

		// Receive completed successfully
		if (BytesTransfered > 0)
		{
			InterlockedExchangeAdd(&gBytesRead, BytesTransfered);
			InterlockedExchangeAdd(&gBytesReadLast, BytesTransfered);
			if (BytesTransfered == sizeof(MESSAGE))
			{
				MESSAGE *rcvMess;
				rcvMess = (MESSAGE *)buf->buf;
				if (rcvMess->opcode == OPS_OK)
				{
					InterlockedDecrement(&gOutstandingDownloads);

					readobj = buf;
					readobj->buflen = sizeof(MESSAGE);
					readobj->sock = sockobj;
					readobj->sock->mess = *rcvMess;
					EnqueueDownloadingOperation(&gPendingReadList, &gPendingReadListEnd, readobj);
				}
				else if (rcvMess->opcode == OPT_FILE_DATA)
				{
					if (rcvMess->length == 0)
					{
						InterlockedDecrement(&gOutstandingUploads);
						recvobj = buf;
						recvobj->sock = sockobj;
						recvobj->sock->mess = *rcvMess;
						EnqueueUploadingOperation(&gPendingWriteList, &gPendingWriteListEnd, recvobj);

					}
					else
					{
						InterlockedDecrement(&gOutstandingUploads);
						writeobj = buf;
						writeobj->sock = sockobj;
						writeobj->sock->mess = *rcvMess;
						EnqueueUploadingOperation(&gPendingWriteList, &gPendingWriteListEnd, writeobj);
					}
				}
				else if (rcvMess->opcode == OPT_FILE_DIGEST)
				{
					InterlockedDecrement(&gOutstandingUploads);
					recvobj = buf;
					recvobj->sock = sockobj;
					recvobj->sock->mess = *rcvMess;
					EnqueueUploadingOperation(&gPendingWriteList, &gPendingWriteListEnd, recvobj);
				}
				else {
					recvobj = buf;
					recvobj->sock = sockobj;
					recvobj->sock->mess = *rcvMess;

					if (parseAndProcess(recvobj)) {
						memcpy(buf->buf, &recvobj->sock->mess, sizeof(MESSAGE));
						sendobj = buf;
						sendobj->sock = sockobj;

						MESSAGE m = (MESSAGE) sendobj->sock->mess;
						
						EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);
						printf("\nSending code %d to client %d", m.opcode, sockobj->s);
						ProcessPendingOperations();
					}

				}

			}
		}
		else
		{
			dbgprint("Got 0 byte receive\n");
			// Graceful close - the receive returned 0 bytes read
			sockobj->bClosing = TRUE;
			// Free the receive buffer
			FreeBufferObj(buf);

			// If this was the last outstanding operation on socket, clean it up
			EnterCriticalSection(&sockobj->SockCritSec);

			if ((sockobj->OutstandingSend == 0) && (sockobj->OutstandingRecv == 0))
			{
				bCleanupSocket = TRUE;
			}
			LeaveCriticalSection(&sockobj->SockCritSec);
		}
	}
	else if (buf->operation == OP_WRITE)
	{
		fprintf(stderr, "wrtting:");
		sockobj = (SOCKET_OBJ *)key;
		InterlockedDecrement(&sockobj->OutstandingSend);
		InterlockedDecrement(&gOutstandingSends);
		// Update the counters
		InterlockedExchangeAdd(&gBytesSent, BytesTransfered);
		InterlockedExchangeAdd(&gBytesSentLast, BytesTransfered);
		buf->buflen = gBufferSize;
		if (sockobj->bClosing == FALSE)
		{
			MESSAGE *queueMessage;
			queueMessage = (MESSAGE *)buf->buf;
			if (queueMessage->opcode == OPT_FILE_DATA)
			{
				MESSAGE sendMessage;
				if (sockobj->fileTransfer.nLeft > 0)
				{
					InterlockedDecrement(&gOutstandingDownloads);
					readobj = buf;
					readobj->buflen = sizeof(MESSAGE);
					readobj->sock = sockobj;
					readobj->sock->mess = *queueMessage;
					EnqueueDownloadingOperation(&gPendingReadList, &gPendingReadListEnd, readobj);
				}
				else if (sockobj->fileTransfer.nLeft == 0)
				{
					fprintf(stderr, "ending");
					sendMessage.opcode = OPT_FILE_DATA;
					sendMessage.payload[0] = 0;
					sendMessage.length = 0;
					sockobj->bClosing = TRUE;
					memcpy(buf->buf, &sendMessage, sizeof(MESSAGE));
					buf->buflen = sizeof(MESSAGE);
					sendobj = buf;
					sendobj->buflen = sizeof(MESSAGE);
					sendobj->sock = sockobj;

					EnqueuePendingOperation(&gPendingSendList, &gPendingSendListEnd, sendobj, OP_WRITE);
					ProcessPendingOperations();
				}

			}
			else if (queueMessage->opcode == OPT_FILE_DIGEST)
			{
				InterlockedDecrement(&gOutstandingDownloads);
				recvobj = buf;
				recvobj->sock = sockobj;
				recvobj->sock->mess = *queueMessage;
				EnqueueDownloadingOperation(&gPendingReadList, &gPendingReadListEnd, recvobj);
			}
			else if (queueMessage->opcode == OPS_OK)
			{
				InterlockedDecrement(&gOutstandingUploads);
				fprintf(stderr, "checking upload");
				recvobj = buf;
				recvobj->sock = sockobj;
				recvobj->sock->mess = *queueMessage;
				EnqueueUploadingOperation(&gPendingWriteList, &gPendingWriteListEnd, recvobj);
			}
			else {
				buf->sock = sockobj;
				PostRecv(sockobj, buf);
			}
		}
	}

	//ProcessPendingOperations();
	if (sockobj)
	{
		if (error != NO_ERROR)
		{
			printf("err = %d\n", error);
			sockobj->bClosing = TRUE;
		}

		// Check to see if socket is closing
		if ((sockobj->OutstandingSend == 0) && (sockobj->OutstandingRecv == 0) && (sockobj->bClosing))
		{
			bCleanupSocket = TRUE;
		}

		if (bCleanupSocket)
		{
			closesocket(sockobj->s);
			sockobj->s = INVALID_SOCKET;
			FreeSocketObj(sockobj);
		}
	}

	return;
}

// Function: CompletionThread
// Description:
//    This is the completion thread which services our completion port. One of
//    these threads is created per processor on the system. The thread sits in
//    an infinite loop calling GetQueuedCompletionStatus and handling socket IO that completed.

DWORD WINAPI CompletionThread(LPVOID lpParam)
{
	ULONG_PTR    Key;
	SOCKET       s;
	BUFFER_OBJ  *bufobj = NULL;                   // Per I/O object for completed I/O
	OVERLAPPED  *lpOverlapped = NULL;     // Pointer to overlapped structure for completed I/O
	HANDLE       CompletionPort;                    // Completion port handle
	DWORD        BytesTransfered,                   // Number of bytes transferred
		Flags;                                     // Flags for completed I/O
	int          rc, error;

	CompletionPort = (HANDLE)lpParam;
	while (1)
	{
		error = NO_ERROR;
		rc = GetQueuedCompletionStatus(CompletionPort, &BytesTransfered, (PULONG_PTR)&Key, &lpOverlapped, INFINITE);
		bufobj = CONTAINING_RECORD(lpOverlapped, BUFFER_OBJ, ol);
		if (rc == FALSE)
		{
			// If the call fails, call WSAGetOverlappedResult to translate the
			//    error code into a Winsock error code.
			if (bufobj->operation == OP_ACCEPT)
			{
				s = ((LISTEN_OBJ *)Key)->s;
			}
			else
			{
				s = ((SOCKET_OBJ *)Key)->s;
			}
			dbgprint("CompletionThread: GetQueuedCompletionStatus failed: %d [0x%x]\n", GetLastError(), lpOverlapped->Internal);
			rc = WSAGetOverlappedResult(s, &bufobj->ol, &BytesTransfered, FALSE, &Flags);
			if (rc == FALSE)
			{
				error = WSAGetLastError();
			}
		}
		// Handle the IO operation
		HandleIo(Key, bufobj, CompletionPort, BytesTransfered, error);
	}

	ExitThread(0);
	return 0;
}
// Function: workerReadThread
// Description:
//    This is the  thread which services our Download file protocol . One of
//    these threads is created  on the system. The thread sits in
//    an infinite loop calling ProcessDownloadingOperations to process file operation needed .
unsigned __stdcall workerReadThread(void *param)
{
	while (TRUE)
	{
		ProcessDownloadingOperations();
	}
}
// Function: workerWriteThread
// Description:
//    This is the  thread which services our Upload file protocol . One of
//    these threads is created  on the system. The thread sits in
//    an infinite loop calling ProcessUploadingOperations to process file operation needed .
unsigned __stdcall workerWriteThread(void *param)
{
	while (TRUE)
	{
		ProcessUploadingOperations();
	}
}

