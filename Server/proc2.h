
#pragma once

#ifndef _PROCESSOR_H
#define _PROCESSOR_H

#include <stdio.h>
#include <conio.h>
#include <list>
#include <winsock2.h>
#include <Windows.h>
#include <time.h>
#include "sqlite3.h"

#define OPA_REAUTH			100
#define OPA_REQ_COOKIES		101
#define OPA_USERNAME		110
#define OPA_PASSWORD		111
#define OPA_LOGOUT			112

#define OPG_GROUP_USE		200
#define OPG_GROUP_JOIN		201
#define OPG_GROUP_LEAVE		202
#define OPG_GROUP_NEW		210

#define OPB_FILE_LIST		300
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
#define OPS_ERR_FORBIDDEN	953
#define OPS_ERR_NOTFOUND	954
#define OPS_ERR_LOCKED		960
#define OPS_ERR_WRONGPASS	961
#define OPS_ERR_ANOTHERCLIENT	962
#define OPS_ERR_NOTLOGGEDIN	963

#define GROUPNAME_SIZE 500
#define FILENAME_SIZE  500
#define DIGEST_SIZE 33
#define CRE_MAXLEN	256
#define COOKIE_LEN		33

#define TIME_1_DAY				86400
#define TIME_1_HOUR				3600
#define ATTEMPT_LIMIT			3

typedef struct {
	int opcode;
	unsigned int length;
	int offset;
	int burst;
	char payload[2056];
} MESSAGE, LPMESSAGE;

typedef struct {
	int opcode;
	unsigned int length;
	int offset;
	int burst;
	char payload[2056];
} MESSAGE, *LPMESSAGE;       // 1 goi tin

typedef struct _SOCKET_OBJ {
	SOCKET    s;
	int       af,
		bClosing;
	volatile LONG      OutstandingRecv,
		OutstandingSend, PendingSend;
	CRITICAL_SECTION   SockCritSec;
	LPFILE_TRANSFER_PROPERTY fileTransferProp; // Them cai nay
	struct _SOCKET_OBJ  *next;
} SOCKET_OBJ;

typedef struct {
	SOCKET_OBJ*		socket; // Sua thanh con tro toi SOCKET_OBJ
	LPMESSAGE   message;
	LPPER_QUEUE_ITEM next;
} PER_QUEUE_ITEM, *LPPER_QUEUE_ITEM;
// struct de tang tren voi tang duoi lien lac voi nhau

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
	bool		isTransfering = false;
	short		filePart = 0;
	Group*      group;
} FILE_TRANSFER_PROPERTY, *LPFILE_TRANSFER_PROPERTY;

typedef struct {
	int		uid;
	char	username[CRE_MAXLEN];
	char	password[CRE_MAXLEN];
	char	cookie[COOKIE_LEN];
	bool	isLocked;
	bool	isActive;
	time_t	lastActive;
	HANDLE	mutex;
	Group*	workingGroup;
} Account;

typedef struct {
	Account*	account;
	SOCKET      socket;
	int			numOfAttempts;
	time_t		lastAtempt;
} Attempt;

std::list<Attempt> attemptList;
std::list<Account> accountList;
std:unordered_map<SOCKET, Account*> socketAccountMap;

CRITICAL_SECTION attemptCritSec;

int initializeData(char* dbName) {
	// Open dbs
	InitializeCriticalSection(&attemptCritSec);
}

void packMessage(LPMESSAGE message, int opcode, int length, int offset, int burst, char* payload) {
	message->opcode = OPS_ERR_NOTFOUND;
	message->length = 0;
	message->offset = 0;
	message->burst = 0;
	strcpy(message->payload, payload);
}

/*
Extract information from request and call corresponding process function
Function returns 1 if a response is ready to be sent, else returns 0
[IN] sock:		the socket that's sending the request
[IN/OUT] buff:	a char array which contains the request, and stores response
after the request has been processed
[OUT] len:		the length of the payload of the output
*/
int parseAndProcess(LPPER_QUEUE_ITEM queueItem) {
	LPMESSAGE message = queueItem->message;

	// Process request
	switch (message->opcode) {
		// Authentication operations
	case OPA_REAUTH:

	case OPA_REQ_COOKIES:
	case OPA_USERNAME:
	case OPA_PASSWORD:
	case OPA_LOGOUT:

	case OPG_GROUP_USE:
	case OPG_GROUP_JOIN:
	case OPG_GROUP_LEAVE:
	case OPG_GROUP_NEW:

	case OPB_FILE_LIST:
	case OPB_FILE_CD:
	case OPB_FILE_COUNT:
	case OPB_DIR_COUNT:
	case OPB_FILE_NAME:
	case OPB_DIR_NAME:
	case OPB_FILE_DEL:
	case OPB_DIR_DEL:
	case OPB_DIR_NEW:

	case OPT_CONNECT:
	case OPT_FILE_DOWN:
	case OPT_FILE_UP:
	case OPT_FILE_DIGEST:
	case OPT_FILE_DATA:

	default:
		printf("bad request at default\n");
		packMessage(queueItem->message, OPS_ERR_BADREQUEST, 0, 0, 0, "");
		return 1;
	}
}

/*
Generate a new unique cookie.
[OUT] cookie:		a char array to store newly created cookie
*/
void generateCookies(char* cookie) {
	static char* characters = "1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefjhijklmnopqrstuvwxyz";
	srand((unsigned int)time(NULL));

	bool duplicate = false;
	// Generate new cookie until it's unique
	do {
		// Randomly choose a character
		for (int i = 0; i < COOKIE_LEN - 1; i++) {
			cookie[i] = characters[rand() % 62];
		}
		cookie[COOKIE_LEN - 1] = 0;

		// Check if duplicate
		for (std::list<Session>::iterator it = sessionList.begin(); it != sessionList.end(); it++) {
			if (strcmp(it->account->cookie, cookie) == 0) {
				duplicate = true;
				break;
			}
		}
	} while (duplicate);
}

/*
Process login from cookie, username and password and construct response
[IN] sock:		the socket that's sending the request
[OUT] buff:		a char array to store the constructed response message
[IN] cookie:		a char array containing the SID to be processed
[IN] username:	a char array containing the username to be processed
[IN] password:	a char array containing the password to be processed
*/
int processOpLogIn(LPPER_QUEUE_ITEM item) {
	LPMESSAGE message = item->message;
	Account* account = NULL;
	Attempt* thisAttempt = NULL;
	time_t now = time(0);

	// Find attempt
	for (auto iterator = attemptList.begin(); iterator != attemptList.end(); iterator++) {
		if (iterator->socket == item->socket->s) {
			thisAttempt = &((Attempt)*iterator);
			break;
		}
	}
	if (thisAttempt == NULL) {
		Attempt newAttempt;
		newAttempt.account == NULL;
		newAttempt.socket = item->socket->s;
		attemptList.push_back(newAttempt);
		thisAttempt = &(attemptList.back());
	}

	// If client sends username, find account in account list and attach to session.
	if (message->opcode == OPA_USERNAME) {
		for (auto it = accountList.begin(); it != accountList.end(); it++) {
			if (strcmp(it->username, message->payload) == 0) {
				account = &(*it);
			}
			thisAttempt->account = account;
			return 0;
		}
		// Client sends password
	else {
		account = thisAttempt->account;
		// If there isn't an account attached to session, inform not found error
		if (account == NULL) {
			// printf("Account not found!\n");
			packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
			return 1;
		}

		WaitForSingleObject(account->mutex, INFINITE);
		// Check if session is already associated with an active account
		if (account->isActive) {
			printf("This account is already logged in somewhere.\n");
			packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
			ReleaseMutex(account->mutex);
			return 1;
		}

		// Check if account is locked
		if (account->isLocked) {
			// printf("Account is locked.\n");
			packMessage(message, OPS_ERR_LOCKED, 0, 0, 0, "");
			ReleaseMutex(account->mutex);
			return 1;
		}

		// Check password
		if (strcmp(account->password, message->payload) != 0) {
			printf("Wrong password!\n");

			// Check if has attempt before
			EnterCriticalSection(&attemptCritSec);
			Attempt* attempt = NULL;
			for (auto it = attemptList.begin(); it != attemptList.end(); it++) {
				if (it->account == account) {
					attempt = &((Attempt)*it);
					break;
				}
			}
			if (attempt != NULL) {
				// If last attempt is more than 1 hour before then reset number of attempts
				if (now - attempt->lastAtempt > TIME_1_HOUR)
					attempt->numOfAttempts = 1;
				else
					attempt->numOfAttempts++;
				attempt->lastAtempt = now;

				// If number of attempts exceeds limit then block account and update database 
				if (attempt->numOfAttempts > ATTEMPT_LIMIT) {
					account->isLocked = true;
					dbLockAccount(account);
					printf("Account locked. Database updated.\n");
					packMessage(message, OPS_ERR_LOCKED, 0, 0, 0, "");
					LeaveCriticalSection(&attemptCritSec);
					ReleaseMutex(account->mutex);
					return 1;
				}
			}
			// if there is no previous attempt, add attempt
			else {
				Attempt newAttempt;
				newAttempt.lastAtempt = now;
				newAttempt.numOfAttempts = 1;
				newAttempt.account = account;
				attemptList.push_back(newAttempt);
			}
			packMessage(message, OPS_ERR_WRONGPASS, 0, 0, 0, "");
			LeaveCriticalSection(&attemptCritSec);
			ReleaseMutex(account->mutex);
			return 1;
		}

		// Passed all checks. Update active time and session account info
		account->lastActive = now;
		account->isActive = true;
		printf("Login successful.\n");
		packMessage(message, OPS_OK, 0, 0, 0, "");
		ReleaseMutex(account->mutex);

		return 1;
	}
	}

	/*
	Process log out and construct response
	[OUT] buff:		a char array to store the constructed response message
	[IN] cookie:		a char array containing the SID to be processed
	*/
	int processOpLogOut(LPPER_QUEUE_ITEM item) {
		LPMESSAGE message = item->message;
		Session* thisAttempt = NULL;
		time_t now = time(0);

		// Find session
		for (auto iterator = sessionList.begin(); iterator != sessionList.end(); iterator++) {
			if (iterator->controlSock == item->socket->s) {
				thisAttempt = &((Session)*iterator);
				break;
			}
		}

		// If session not found, inform bad request
		if (thisAttempt == NULL) {
			packMessage(message, OPS_ERR_BADREQUEST, 0, 0, 0, "");
			return 1;
		}

		// If there isn't an account attached to session => deny
		if (thisAttempt->account == NULL || thisAttempt->account->isActive == false) {
			printf("Account is not logged in. Deny log out.\n");
			packMessage(message, OPS_ERR_NOTLOGGEDIN, 0, 0, 0, "");
			return 1;
		}
		// All checks out! Allow log out
		printf("Log out OK.\n");
		thisAttempt->account->lastActive = now;
		thisAttempt->account->isActive = false;
		thisAttempt->account->workingGroup = NULL;
		thisAttempt->account->cookie[0] = 0;

		sessionList.remove(*thisAttempt);
		packMessage(message, OPS_OK, 0, 0, 0, "");
		printf("Log out successful.\n");
		return 1;
	}

	/*
	Process reauthentication request and construct response
	[IN] sock:		the socket that's sending the request
	[OUT] buff:		a char array to store the constructed response message
	[IN] cookie:		a char array containing the SID to be processed
	*/
	int processOpReauth(LPPER_QUEUE_ITEM item) {
		LPMESSAGE message = item->message;
		time_t now = time(0);
		Account* account;
		Session* thisAttempt = NULL;

		// Check if cookie's length is correct
		if (message->length != COOKIE_LEN) {
			packMessage(message, OPS_ERR_BADREQUEST, 0, 0, 0, "");
			return 1;
		}

		// Find account with cookie
		for (auto it = accountList.begin(); it != accountList.end(); it++)
			if (strcmp(message->payload, it->cookie) == 0) {
				account = (Account*)&(*it);
				break;
			}
		// If account does not exists
		if (account == NULL) {
			printf("Session not found.\n");
			packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
			return;
		}

		// Check if lastActive is more than 1 day ago
		if (now - account->lastActive > TIME_1_DAY) {
			printf("Login session timeout. Deny reauth.\n");
			account->pAcc = NULL;
			snprintf(buff, BUFSIZE, "%d", RES_RA_FOUND_NOTLI);
			return;
		}

		// Check if account is disabled
		if (account->isLocked) {
			printf("Account is locked. Reauth failed.\n");
			packMessage(message, OPS_ERR_BADREQUEST, 0, 0, 0, "");

			pThisSession->pAcc = NULL;
			snprintf(buff, BUFSIZE, "%d", RES_LI_ACC_LOCKED);
			return;
		}
		// Check if account is logged in on another device
		WaitForSingleObject(sessionListMutex, INFINITE);
		for (it = sessionList.begin(); it != sessionList.end(); it++) {
			if (((Session*)&(*it) != pThisSession)
				&& (it->pAcc == pThisSession->pAcc)
				&& (it->socket != 0)
				&& (now - it->lastActive < TIME_1_DAY))
			{
				printf("This account is logged in on another socks.\n");
				pThisSession->pAcc = NULL;
				snprintf(buff, BUFSIZE, "%d", RES_LI_ELSEWHERE);
				ReleaseMutex(sessionListMutex);
				return;
			}
		}
		// All checks out!
		printf("Session exists. Allow reauth.\n");
		pThisSession->lastActive = time(0);
		pThisSession->socket = sock;
		snprintf(buff, BUFSIZE, "%d %s", RES_OK, pThisSession->cookie);
		ReleaseMutex(sessionListMutex);
		return;
	}

	/*
	Process cookie request and construct response
	[IN] sock:		the socket that's sending the request
	[OUT] buff:		a char array to store the constructed response message
	[OUT] responseLen: a short to store the length of the response payload
	*/
	void processOpRequestSid(SOCKET sock, char* buff) {
		char cookie[COOKIE_LEN];

		WaitForSingleObject(sessionListMutex, INFINITE);
		generateCookies(sid);

		// Create new session and add to sessionList
		Session newSession;
		newSession.pAcc = NULL;
		newSession.lastActive = time(0);
		newSession.socket = sock;
		strcpy_s(newSession.cookie, COOKIE_LEN, cookie);
		sessionList.push_back(newSession);

		// Construct response
		snprintf(buff, BUFSIZE, "%d %s", RES_OK, newSession.cookie);
		ReleaseMutex(sessionListMutex);
		printf("Generated new SID for socks: %s.\n", sid);
	}

	/*
	Remove socket information from any Session previously associated with it
	[IN] sock:	the socket which has been disconnected
	*/
	void disconnect(SOCKET sock) {
		for (auto it = sessionList.begin(); it != sessionList.end(); it++) {
			if (it->socket == sock)
				it->socket = 0;
		}
	}


	int processOpGroup(LPPER_QUEUE_ITEM queueItem) {
		/*
		#define OPG_GROUP_USE		200
		#define OPG_GROUP_JOIN		201
		#define OPG_GROUP_LEAVE		202
		#define OPG_GROUP_NEW		210

		*/
		LPMESSAGE message = queueItem->message;
		switch (message->opcode) {
		case OPG_GROUP_USE:
			socketAccountM
		}

	}


#endif