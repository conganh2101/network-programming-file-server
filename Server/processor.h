#pragma once

#ifndef _PROCESSOR_H
#define _PROCESSOR_H

#include <list>
#include <unordered_map>
#include <winsock2.h>
#include "dbUtils.h"

std::list<Attempt> attemptList;
std::list<Account> accountList;
std::list<Group> groupList;

std::unordered_map<SOCKET, Account*> socketAccountMap;

CRITICAL_SECTION attemptCritSec;

int initializeData() {
	if (openDb()) return 1;
	if (readAccountDb(accountList)) return 1;
	if (readGroupDb(groupList)) return 1;

	InitializeCriticalSection(&attemptCritSec);
	return 0;
}

void packMessage(LPMESSAGE message, int opcode, int length, int offset, int burst, char* payload) {
	message->opcode = opcode;
	message->length = length;
	message->offset = offset;
	message->burst = burst;
	strcpy(message->payload, payload);
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
		for (auto it = accountList.begin(); it != accountList.end(); it++) {
			if (strcmp(it->cookie, cookie) == 0) {
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
int processOpLogIn(BUFFER_OBJ* bufferObj) {
	LPMESSAGE message = &(bufferObj->sock->mess);
	Account* account = NULL;
	Attempt* thisAttempt = NULL;
	time_t now = time(0);

	// Find attempt
	for (auto iterator = attemptList.begin(); iterator != attemptList.end(); iterator++) {
		if (iterator->socket == bufferObj->sock->s) {
			thisAttempt = &((Attempt)*iterator);
			break;
		}
	}
	if (thisAttempt == NULL) {
		Attempt newAttempt;
		newAttempt.account = NULL;
		newAttempt.socket = bufferObj->sock->s;
		attemptList.push_back(newAttempt);
		thisAttempt = &(attemptList.back());
	}

	// Parse username and password
	char* username = NULL;
	char* password = NULL;

	username = message->payload;
	for (unsigned int i = 0; i < message->length; i++) {
		if (message->payload[i] == ' ') {
			message->payload[i] = 0;
			password = message->payload + i + 1;
		}
	}

	if (password == NULL) {
		packMessage(message, OPS_ERR_BADREQUEST, 0, 0, 0, "");
		return 1;
	}

	// Find account in account list and attach to session.
	for (auto it = accountList.begin(); it != accountList.end(); it++) {
		if (strcmp(it->username, message->payload) == 0) {
			account = &(*it);
			thisAttempt->account = account;
			break;
		}
	}
	account = thisAttempt->account;
	// If there isn't an account attached to session, inform not found error
	if (account == NULL) {
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
		packMessage(message, OPS_ERR_LOCKED, 0, 0, 0, "");
		ReleaseMutex(account->mutex);
		return 1;
	}

	// Check password
	if (strcmp(account->password, password) != 0) {
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
				lockAccountDb(account);
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
	socketAccountMap[bufferObj->sock->s] = account;
	printf("Login successful.\n");
	packMessage(message, OPS_OK, 0, 0, 0, "");
	ReleaseMutex(account->mutex);

	return 1;
}

/*
Process log out and construct response
[OUT] buff:		a char array to store the constructed response message
[IN] cookie:		a char array containing the SID to be processed
*/
int processOpLogOut(BUFFER_OBJ* bufferObj) {
	LPMESSAGE message = &(bufferObj->sock->mess);
	Attempt* thisAttempt = NULL;
	std::list<Attempt>::iterator iterator;
	time_t now = time(0);

	// Find attempt
	for (iterator = attemptList.begin(); iterator != attemptList.end(); iterator++) {
		if (iterator->socket == bufferObj->sock->s) {
			thisAttempt = &((Attempt)*iterator);
			break;
		}
	}

	// If attempt not found, inform bad request
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

	// attemptList.remove(*thisAttempt);
	attemptList.erase(iterator);
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
int processOpReauth(BUFFER_OBJ* bufferObj) {
	LPMESSAGE message = &(bufferObj->sock->mess);
	time_t now = time(0);
	Account* account = NULL;
	Attempt* thisAttempt = NULL;

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
		printf("Cookie not found.\n");
		packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
		return 1;
	}

	// Check if lastActive is more than 1 day ago
	if (now - account->lastActive > TIME_1_DAY) {
		printf("Login session timeout. Deny reauth.\n");
		account->isActive = false;
		packMessage(message, OPS_ERR_NOTLOGGEDIN, 0, 0, 0, "");
		return 1;
	}

	// Check if account is disabled
	if (account->isLocked) {
		printf("Account is locked. Reauth failed.\n");
		account->cookie[0] = 0;
		packMessage(message, OPS_ERR_LOCKED, 0, 0, 0, "");
		return 1;
	}
	// Check if account is logged in on another device
	if (socketAccountMap.find(bufferObj->sock->s) != socketAccountMap.end()) {
		printf("This account is logged in on another socks.\n");
		thisAttempt->account = NULL;
		packMessage(message, OPS_ERR_ANOTHERCLIENT, 0, 0, 0, "");
		return 1;
	}
	// All checks out!
	printf("Session exists. Allow reauth.\n");
	socketAccountMap[bufferObj->sock->s] = account;
	account->lastActive = time(0);
	packMessage(message, OPS_OK, 0, 0, 0, "");
	return 1;
}

/*
Process cookie request and construct response
[IN] sock:		the socket that's sending the request
[OUT] buff:		a char array to store the constructed response message
[OUT] responseLen: a short to store the length of the response payload
*/
int processOpRequestCookie(BUFFER_OBJ* bufferObj) {
	LPMESSAGE message = &(bufferObj->sock->mess);

	auto accountSearch = socketAccountMap.find(bufferObj->sock->s);
	if (accountSearch == socketAccountMap.end()) {
		packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
		return 1;
	}

	Account* account = accountSearch->second;
	char cookie[COOKIE_LEN];
	generateCookies(cookie);

	// Create cookie and add to account
	account->lastActive = time(0);
	strcpy_s(account->cookie, COOKIE_LEN, cookie);

	// Construct response
	packMessage(message, OPS_OK, 0, 0, 0, cookie);
	printf("Generated new Cookie for socket %d: %s.\n", bufferObj->sock->s, cookie);
	return 1;
}

/*
Remove socket information from any Session previously associated with it
[IN] sock:	the socket which has been disconnected
*/
void disconnect(SOCKET sock) {
	socketAccountMap.erase(sock);
}

int processOpGroup(BUFFER_OBJ* bufferObj) {
	/*
	#define OPG_GROUP_USE		200
	#define OPG_GROUP_JOIN		201
	#define OPG_GROUP_LEAVE		202
	#define OPG_GROUP_NEW		210
	*/

	LPMESSAGE message = &(bufferObj->sock->mess);
	Group* group = NULL;
	int ret;

	auto accountSearch = socketAccountMap.find(bufferObj->sock->s);
	if (accountSearch == socketAccountMap.end()) {
		packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
		return 1;
	}
	Account* account = accountSearch->second;
	std::list<Group> tempGroupList;
	auto it = tempGroupList.begin();

	switch (message->opcode) {
	case OPG_GROUP_LIST:

		if (queryGroupForAccount(account, tempGroupList)) {
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}

		char count[10];
		MESSAGE newMessage;
		LPMESSAGE_LIST listPtr;

		// Send group count
		_itoa(tempGroupList.size(), count, 10);
		packMessage(message, OPG_GROUP_COUNT, 0, 0, 0, count);

		// Add directory count to wait queue
		if (!tempGroupList.empty()) {
			packMessage(&newMessage, OPG_GROUP_NAME, strlen(tempGroupList.front().groupName), 0, 0, tempGroupList.front().groupName);
			account->queuedMess = (LPMESSAGE_LIST)malloc(sizeof(MESSAGE_LIST));
			listPtr = account->queuedMess;
			listPtr->mess = newMessage;

			it = tempGroupList.begin();
			it++;
			// Add file name to wait queue
			for ( ; it != tempGroupList.end(); it++) {
				packMessage(&newMessage, OPG_GROUP_NAME, strlen((*it).groupName), 0, 0, (*it).groupName);
				listPtr->next = (LPMESSAGE_LIST)malloc(sizeof(MESSAGE_LIST));
				listPtr = listPtr->next;
				listPtr->mess = newMessage;
			}
		}
		return 1;

	case OPG_GROUP_USE:
		ret = accountHasAccessToGroupDb(account, message->payload);
		if (ret == -1) {
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}
		
		if (ret == 1) {
			for (auto it = groupList.begin(); it != groupList.end(); ++it) {
				if (strcmp(it->groupName, message->payload) == 0) {
					account->workingGroup = &(*it);
					account->workingDir[0] = 0;
					break;
				}
			}
			if (account->workingGroup == NULL) {
				packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
				return 1;
			}
			packMessage(message, OPS_OK, 0, 0, 0, "");
			return 1;
		}

		packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
		return 1;

	case OPG_GROUP_JOIN:
		ret = accountHasAccessToGroupDb(account, message->payload);
		if (ret == -1) {
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}
		
		if (ret == 1) {
			packMessage(message, OPS_ERR_ALREADYINGROUP, 0, 0, 0, "");
			return 1;
		}

		for (auto it = groupList.begin(); it != groupList.end(); ++it) {
			if (strcmp(it->groupName, message->payload) == 0) {
				group = &(*it);
				break;
			}
		}

		if (group == NULL) {
			packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
			return 1;
		}
		// Add to database
		if (addUserToGroupDb(account, group)) {
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}

		packMessage(message, OPS_OK, 0, 0, 0, "");
		return 1;

	case OPG_GROUP_LEAVE:
		ret = accountHasAccessToGroupDb(account, message->payload);
		if (ret == -1) {
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}
		
		if (ret == 0) {
			packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
			return 1;
		}

		for (auto it = groupList.begin(); it != groupList.end(); ++it) {
			if (strcmp(it->groupName, message->payload) == 0) {
				group = &(*it);
				break;
			}
		}

		if (group == NULL) {
			packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
			return 1;
		}
		// Delete from database
		if (deleteUserFromGroupDb(account, group)) {
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}

		packMessage(message, OPS_OK, 0, 0, 0, "");
		return 1;

	case OPG_GROUP_NEW:

		// Verify group name:
		if (message->length == 0) {
			packMessage(message, OPS_ERR_BADREQUEST, 0, 0, 0, "");
			return 1;
		}

		for (auto it = groupList.begin(); it != groupList.end(); ++it) {
			if (strcmp(it->groupName, message->payload) == 0) {
				group = &(*it);
				break;
			}
		}

		if (group != NULL) {
			packMessage(message, OPS_ERR_GROUPEXISTS, 0, 0, 0, "");
			return 1;
		}

		Group newGroup;
		newGroup.ownerId = account->uid;
		strcpy_s(newGroup.groupName, GROUPNAME_SIZE, message->payload);

		char path[MAX_PATH];
		strcpy_s(newGroup.pathName, MAX_PATH, newGroup.groupName);

		while (1) {
			snprintf(path, MAX_PATH, "%s/%s", STORAGE_LOCATION, newGroup.pathName);
			if (CreateDirectoryA(path, NULL) == 0) {
				if (GetLastError() == ERROR_ALREADY_EXISTS) {
					printf("Cannot create directory with path %s as it already exists", path);
					strcat_s(newGroup.pathName, GROUPNAME_SIZE, "_");
					continue;
				}
				else {
					packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
					return 1;
				}
			}
			break;
		}

		// Add group to database
		if (addGroupDb(&newGroup)) {
			if (RemoveDirectoryA(newGroup.pathName) == 0) {
				printf("Cannot remove directory with path %s. Error code %d!",newGroup.pathName, GetLastError());
			}
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}

		// Add user to group
		if (addUserToGroupDb(account, &newGroup)) {
			if (RemoveDirectoryA(newGroup.pathName) == 0) {
				printf("Cannot remove directory with path %s. Error code %d!", newGroup.pathName, GetLastError());
			}
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}

		packMessage(message, OPS_OK, 0, 0, 0, "");
		return 1;
	}
	return 0;
}

int processOpContinue(BUFFER_OBJ* bufferObj) {
	auto accountSearch = socketAccountMap.find(bufferObj->sock->s);
	if (accountSearch == socketAccountMap.end()) {
		packMessage(&(bufferObj->sock->mess), OPS_ERR_FORBIDDEN, 0, 0, 0, "");
		return 1;
	}

	Account* account = accountSearch->second;

	if (account->queuedMess != NULL) {
		bufferObj->sock->mess = account->queuedMess->mess;
		LPMESSAGE_LIST ptr = account->queuedMess->next;
		free(account->queuedMess);
		account->queuedMess = ptr;
		return 1;
	}
	else {
		packMessage(&(bufferObj->sock->mess), OPS_ERR_BADREQUEST, 0, 0, 0, "");
		return 1;
	}
	return 0;
}

int processOpBrowsing(BUFFER_OBJ* bufferObj) {

	LPMESSAGE message = &(bufferObj->sock->mess);

	auto accountSearch = socketAccountMap.find(bufferObj->sock->s);
	if (accountSearch == socketAccountMap.end()) {
		packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
		return 1;
	}

	Account* account = accountSearch->second;

	if (account->workingGroup == NULL) {
		packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
		return 1;
	}

	WIN32_FIND_DATAA FindFileData;
	HANDLE hFind;

	std::list<MESSAGE> fileList;
	std::list<MESSAGE> folderList;

	char path[MAX_PATH];

	switch (message->opcode) {
	case OPB_LIST:
		MESSAGE newMessage;

		snprintf(path, MAX_PATH, "%s/%s", STORAGE_LOCATION, account->workingGroup->pathName);
		if (strlen(account->workingDir) > 0) {
			snprintf(path, MAX_PATH, "%s/%s", path, account->workingDir);
		}
		strcat_s(path, MAX_PATH, "/*");

		hFind = FindFirstFileA(path, &FindFileData);
		if (hFind == INVALID_HANDLE_VALUE) {
			if (GetLastError() != ERROR_NO_MORE_FILES) {
				printf("FindFirstFile failed (%d)\n", GetLastError());
				packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
				return 1;
			}
		}
		else {
			while (1) {
				if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					packMessage(&newMessage, OPB_DIR_NAME, strlen(FindFileData.cFileName), 0, 0, FindFileData.cFileName);
					folderList.push_back(newMessage);
				}
				else {
					packMessage(&newMessage, OPB_FILE_NAME, strlen(FindFileData.cFileName), 0, 0, FindFileData.cFileName);
					fileList.push_back(newMessage);
				}

				if (FindNextFileA(hFind, &FindFileData) == 0) {
					if (GetLastError() == ERROR_NO_MORE_FILES) {
						break;
					}
					else {
						printf("FindNextFile failed (%d)\n", GetLastError());
						packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
						return 1;
					}
				}
			}

			FindClose(hFind);
		}

		char count[10];
		LPMESSAGE_LIST listPtr;

		// Send file count
		_itoa(fileList.size(), count, 10);
		packMessage(message, OPB_FILE_COUNT, 0, 0, 0, count);

		// Add directory count to wait queue
		_itoa(folderList.size(), count, 10);
		packMessage(&newMessage, OPB_DIR_COUNT, 0, 0, 0, count);
		account->queuedMess = (LPMESSAGE_LIST)malloc(sizeof(MESSAGE_LIST));
		listPtr = account->queuedMess;
		listPtr->mess = newMessage;
	
		// Add file name to wait queue
		for (auto it = fileList.begin(); it != fileList.end(); it++) {
			listPtr->next = (LPMESSAGE_LIST)malloc(sizeof(MESSAGE_LIST));
			listPtr = listPtr->next;
			listPtr->mess = *it;
		}
		
		// Add directory name to wait queue
		for (auto it = folderList.begin(); it != folderList.end(); it++) {
			listPtr->next = (LPMESSAGE_LIST)malloc(sizeof(MESSAGE_LIST));
			listPtr = listPtr->next;
			listPtr->mess = *it;
		}

		return 1;

	case OPB_FILE_CD:
		strcpy_s(path, MAX_PATH, account->workingDir);
		strcat_s(path, MAX_PATH, message->payload);

		hFind = FindFirstFileA(path, (LPWIN32_FIND_DATAA)&FindFileData);
		if (hFind == INVALID_HANDLE_VALUE) {
			if (GetLastError() == ERROR_NO_MORE_FILES) {
				packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
				return 1;
			}
			printf("FindFirstFile failed (%d)\n", GetLastError());
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}
		else {
			strcpy(account->workingDir, path);
			packMessage(message, OPS_OK, 0, 0, 0, "");
			FindClose(hFind);
			return 1;
		}

	case OPB_FILE_DEL:
		if (account->uid != account->workingGroup->ownerId) {
			packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
			return 1;
		}

		strcpy_s(path, MAX_PATH, account->workingDir);
		strcat_s(path, MAX_PATH, message->payload);
		if (DeleteFileA(path)) {
			if (GetLastError() == ERROR_FILE_NOT_FOUND) {
				printf("Cannot remove file %s. File not found!", path);
				packMessage(message, OPS_ERR_NOTFOUND, 0, 0, 0, "");
				return 1;
			}
			printf("Cannot remove file %s. Error code %d!", path, GetLastError());
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}
		packMessage(message, OPS_OK, 0, 0, 0, "");
		return 1;

	case OPB_DIR_DEL:
		if (account->uid != account->workingGroup->ownerId) {
			packMessage(message, OPS_ERR_FORBIDDEN, 0, 0, 0, "");
			return 1;
		}

		strcpy_s(path, MAX_PATH, account->workingDir);
		strcat_s(path, MAX_PATH, message->payload);
		if (RemoveDirectoryA(path)) {
			printf("Cannot remove directory with path %s. Error code %d!", path, GetLastError());
			packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
			return 1;
		}
		packMessage(message, OPS_OK, 0, 0, 0, "");
		return 1;

	case OPB_DIR_NEW:
		strcpy_s(path, MAX_PATH, account->workingDir);
		strcat_s(path, MAX_PATH, message->payload);

		if (CreateDirectoryA(path, NULL)) {
			if (GetLastError() == ERROR_ALREADY_EXISTS) {
				printf("Cannot create directory with path %s as it already exists", path);
				packMessage(message, OPS_ERR_ALREADYEXISTS, 0, 0, 0, "");
			}
			else {
				packMessage(message, OPS_ERR_SERVERFAIL, 0, 0, 0, "");
				return 1;
			}
		}

		packMessage(message, OPS_OK, 0, 0, 0, "");
		return 1;
	}
	return 0;
}


/*
Extract information from request and call corresponding process function
Function returns 1 if a response is ready to be sent, else returns 0
[IN] sock:		the socket that's sending the request
[IN/OUT] buff:	a char array which contains the request, and stores response
after the request has been processed
[OUT] len:		the length of the payload of the output
*/
int parseAndProcess(BUFFER_OBJ* bufferObj) {
	LPMESSAGE message = &(bufferObj->sock->mess);

	// Process request
	switch (message->opcode) {
		// Authentication operations
	case OPA_REAUTH:
		return processOpReauth(bufferObj);
	case OPA_REQ_COOKIES:
		return processOpRequestCookie(bufferObj);
	case OPA_LOGIN:
		return processOpLogIn(bufferObj);
	case OPA_LOGOUT:
		return processOpLogOut(bufferObj);

	case OPG_GROUP_LIST:
	case OPG_GROUP_USE:
	case OPG_GROUP_JOIN:
	case OPG_GROUP_LEAVE:
	case OPG_GROUP_NEW:
		return processOpGroup(bufferObj);

	case OPB_LIST:
	case OPB_FILE_CD:
	case OPB_FILE_DEL:
	case OPB_DIR_DEL:
	case OPB_DIR_NEW:
		return processOpBrowsing(bufferObj);

	case OPS_CONTINUE:
		return processOpContinue(bufferObj);

	default:
		printf("bad request at default\n");
		packMessage(&(bufferObj->sock->mess), OPS_ERR_BADREQUEST, 0, 0, 0, "");
		return 1;
	}
}

#endif