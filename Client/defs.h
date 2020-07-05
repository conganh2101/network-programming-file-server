#pragma once

#ifndef DEFS_H_
#define DEFS_H_

#define OPA_REAUTH			100
#define OPA_REQ_COOKIES		101
#define OPA_USERNAME		110
#define OPA_PASSWORD		111
#define OPA_LOGOUT			112

#define OPG_GROUP_USE		200
#define OPG_GROUP_JOIN		201
#define OPG_GROUP_LEAVE		202
#define OPG_GROUP_NEW		210

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
#define OPS_ERR_BADREQUEST	950
#define OPS_ERR_SERVERFAIL	951
#define OPS_ERR_FORBIDDEN	953
#define OPS_ERR_NOTFOUND	954

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
#define BUFF_SIZE		2048


typedef struct {
	int opcode;
	unsigned int length;
	long offset;
	int burst;
	char payload[2048];
} MESSAGE, *LPMESSAGE;

typedef struct _SOCKET_INFORMATION {
	WSAOVERLAPPED overlapped;
	SOCKET sockfd;
	CHAR buff[DATA_BUFSIZE];
	WSABUF dataBuff;
	DWORD sentBytes;
	DWORD recvBytes;
	DWORD operation;
} SOCKET_INFORMATION, *LPSOCKET_INFORMATION;

typedef struct _FILE_INFORMATION {
	char fileName[100];
	char		digest[DIGEST_SIZE];
	FILE*		file = NULL;
	int fileLen;
	int idx;
	int nLeft;
	char *fileBuffer;
}FILE_INFORMATION, *LPFILE_INFORMATION;

typedef struct message {
	char opcode;
	unsigned int length;
	long offset;
	char payload[BUFF_SIZE];
} message;

typedef enum messType {
	sendingData,
	uploadFile,
	downloadFile,
	fileNameDownload,
	fileNameUpload,
	success,
	fileExist,
	noFileExist,
	md5Code,
	fileCorrupted
} Type;

#endif