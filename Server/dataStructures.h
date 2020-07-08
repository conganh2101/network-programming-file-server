#pragma once
#ifndef _DATA_STRUCTURES_H
#define _DATA_STRUCTURES_H

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <time.h>

#define OPA_REAUTH			100
#define OPA_REQ_COOKIES		101
#define OPA_LOGIN			110
#define OPA_LOGOUT			112

#define OPG_GROUP_USE		200
#define OPG_GROUP_LIST		201
#define OPG_GROUP_JOIN		202
#define OPG_GROUP_LEAVE		203
#define OPG_GROUP_NEW		204
#define OPG_GROUP_COUNT		210
#define OPG_GROUP_NAME		211

#define OPB_LIST			300
#define OPB_FILE_CD			301
#define OPB_FILE_COUNT		310
#define OPB_DIR_COUNT		320
#define OPB_FILE_NAME		311
#define OPB_DIR_NAME		321
#define OPB_FILE_DEL		330
#define OPB_DIR_DEL			331
#define OPB_DIR_NEW			340

#define OPT_CONNECT			400
#define OPT_FILE_DOWN		401
#define OPT_FILE_UP			402
#define OPT_FILE_DIGEST		403
#define OPT_FILE_DATA		404

#define OPS_OK				900
#define OPS_SUCCESS			901
#define OPS_CONTINUE		902
#define OPS_ERR_BADREQUEST	950
#define OPS_ERR_SERVERFAIL	951
#define OPS_ERR_FORBIDDEN	953
#define OPS_ERR_NOTFOUND	954
#define OPS_ERR_FILE_CORRUPTED 955

#define OPS_ERR_LOCKED		960
#define OPS_ERR_WRONGPASS	961
#define OPS_ERR_ANOTHERCLIENT	962
#define OPS_ERR_NOTLOGGEDIN	963
#define OPS_ERR_ALREADYINGROUP	970
#define OPS_ERR_GROUPEXISTS	971
#define OPS_ERR_ALREADYEXISTS 980

#define GROUPNAME_SIZE 500
#define FILENAME_SIZE  500
#define DIGEST_SIZE 33
#define CRE_MAXLEN	256
#define COOKIE_LEN		33

#define STORAGE_LOCATION "/"

#define TIME_1_DAY				86400
#define TIME_1_HOUR				3600
#define ATTEMPT_LIMIT			3

typedef struct {
	int opcode;
	unsigned int length;
	long offset;
	int burst;
	char payload[2048];
} MESSAGE, *LPMESSAGE;

typedef struct _MESSAGE_LIST {
	MESSAGE mess;
	struct _MESSAGE_LIST* next = NULL;
} MESSAGE_LIST, *LPMESSAGE_LIST;

typedef struct {
	int         gid;
	char        groupName[GROUPNAME_SIZE];
	char        pathName[GROUPNAME_SIZE];
	int         ownerId;
} Group;

typedef struct {
	char		fileName[FILENAME_SIZE];
	char		digest[DIGEST_SIZE];
	FILE*		file = NULL;
	long        fileLen;
	long        idx;
	long        nLeft;
	char        *fileBuffer;
	bool		isTransfering = false;
	short		filePart = 0;
	Group*      group;
} FILE_TRANSFER_PROPERTY, *LPFILE_TRANSFER_PROPERTY;

typedef struct {
	int		uid;
	char	username[CRE_MAXLEN];
	char	password[CRE_MAXLEN];
	char	cookie[COOKIE_LEN];
	bool	isLocked = 0;
	bool	isActive = 0;
	time_t	lastActive = 0;
	HANDLE	mutex;
	char	workingDir[MAX_PATH];
	Group*	workingGroup = NULL;
	LPMESSAGE_LIST queuedMess = NULL;
} Account;

typedef struct {
	Account*	account;
	SOCKET      socket;
	int			numOfAttempts = 0;
	time_t		lastAtempt;
} Attempt;


// This is our per socket buffer. It contains information about the socket handle
// which is returned from each GetQueuedCompletionStatus call.
typedef struct _SOCKET_OBJ
{
	SOCKET    s;               // Socket handle
	int       af,              // Address family of socket (AF_INET, AF_INET6)
		bClosing;        // Is the socket closing?
	FILE_TRANSFER_PROPERTY fileTransfer;
	MESSAGE mess;
	volatile LONG      OutstandingRecv, // Number of outstanding overlapped ops on
		OutstandingSend, PendingSend;
	CRITICAL_SECTION   SockCritSec;     // Protect access to this structure
	struct _SOCKET_OBJ  *next;
} SOCKET_OBJ;

// This is our per I/O buffer. It contains a WSAOVERLAPPED structure as well
// as other necessary information for handling an IO operation on a socket.
typedef struct _BUFFER_OBJ
{
	WSAOVERLAPPED       ol;
	SOCKET              sclient;       // Used for AcceptEx client socket
	HANDLE              PostAccept;
	char				*buf;          // Buffer for recv/send/AcceptEx
	int                 buflen;        // Length of the buffer
	int                 operation;     // Type of operation issued
#define OP_ACCEPT       0                // AcceptEx
#define OP_READ         1                   // WSARecv/WSARecvFrom
#define OP_WRITE        2                   // WSASend/WSASendTo

	SOCKADDR_STORAGE     addr;
	int                  addrlen;
	struct _SOCKET_OBJ  *sock;
	struct _BUFFER_OBJ  *next;
} BUFFER_OBJ;

typedef struct _LISTEN_OBJ
{
	SOCKET          s;
	int             AddressFamily;
	BUFFER_OBJ     *PendingAccepts; // Pending AcceptEx buffers
	volatile long   PendingAcceptCount;
	int             HiWaterMark, LoWaterMark;
	HANDLE          AcceptEvent;
	HANDLE          RepostAccept;
	volatile long   RepostCount;

	// Pointers to Microsoft specific extensions.
	LPFN_ACCEPTEX             lpfnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;
	CRITICAL_SECTION ListenCritSec;
	struct _LISTEN_OBJ *next;
} LISTEN_OBJ;

typedef struct  _PER_QUEUE_READ_ITEM
{
	struct _SOCKET_OBJ		*socket; // Sua thanh con tro toi SOCKET_OBJ
									 //LPMESSAGE   message;
	struct _PER_QUEUE_READ_ITEM *next;
} PER_QUEUE_READ_ITEM;

#endif