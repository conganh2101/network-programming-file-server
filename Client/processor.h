#pragma once

#ifndef PROCESSOR_H_
#define PROCESSOR_H_

#include <iostream>
#include <vector>


using namespace std;

#include "defs.h"
#include "network.h"
#include "fileUtils.h"

bool isLoggedIn = false;

extern MESSAGE gSendMessage;
extern MESSAGE gRecvMessage;
// extern MESSAGE message;
char cookie[COOKIE_LEN];

void packMessage(int opcode, int length, int offset, int burst, char* payload) {
	gSendMessage.opcode = opcode;
	gSendMessage.length = length;
	gSendMessage.offset = offset;
	gSendMessage.burst = burst;
	if (payload != NULL) {
		strcpy(gSendMessage.payload, payload);
	}
}

int processOpLogIn(char* username, char* password) {
	// Construct request
	snprintf(gSendMessage.payload, BUFF_SIZE, "%s %s", username, password);
	packMessage(OPA_LOGIN, strlen(gSendMessage.payload), 0, 0, NULL);

	// Send
	handleSent();
	handleRecv();
	Sleep(1000);

	if (gRecvMessage.opcode == OPS_OK) {
		isLoggedIn = true;
	}
	return gRecvMessage.opcode;

}

int processOpLogOut() {
	// Construct request
	packMessage(OPA_LOGOUT, 0, 0, 0, "");

	// Send
	handleSent();
	handleRecv();

	if (gRecvMessage.opcode == OPS_OK) {
		isLoggedIn = false;
	}
	return gRecvMessage.opcode;
}

int processOpReauth() {
	// Read SID from file
	readCookieFromFile(cookie, COOKIE_LEN);
	if (strlen(cookie) != COOKIE_LEN - 1) {
		return -1;
	}
	else {
		// Construct reauth message
		packMessage(OPA_REAUTH, COOKIE_LEN, 0, 0, cookie);
		handleSent();
		handleRecv();


		if (gRecvMessage.opcode == OPS_OK) {
			isLoggedIn = true;
		}

		return gRecvMessage.opcode;
	}
}

int processOpReqCookie() {
	// Construct reauth message
	packMessage(OPA_REQ_COOKIES, 0, 0, 0, "");
	handleSent();
	handleRecv();


	if (gRecvMessage.opcode == OPS_OK) {
		writeCookietoFile(cookie);
	}

	return gRecvMessage.opcode;
}


/*****************************
Group
*****************************/

int processOpGroup(int opCode, char* groupName) {
	// Construct reauth message
	packMessage(opCode, strlen(groupName), 0, 0, groupName);
	handleSent();
	handleRecv();

	return gRecvMessage.opcode;
}

int processOpGroupList(vector<char*> &groupList) {
	// Construct reauth message
	packMessage(OPG_GROUP_LIST, 0, 0, 0, "");
	handleSent();
	handleRecv();

	int groupCount = 0;
	if (gRecvMessage.opcode == OPG_GROUP_COUNT) {
		groupCount = atoi(gRecvMessage.payload);
	}
	else {
		return 1;
	}

	char* groupName;
	
	for (int i = 0; i < groupCount; i++) {
		handleRecv();
		Sleep(5000);
		if (gRecvMessage.opcode == OPG_GROUP_NAME) {
			groupName = (char*)malloc(GROUPNAME_SIZE);
			strcpy_s(groupName, GROUPNAME_SIZE, gRecvMessage.payload);
			groupList.push_back(groupName);
			packMessage(OPS_CONTINUE, 0, 0, 0, "");
			handleSent();
		}
		else {
			return 1;
		}
	}

	return 0;
}


/*****************************
Browsing
*****************************/

int processOpBrowse(int opCode, char* path) {
	// Construct reauth message
	packMessage(opCode, strlen(path), 0, 0, path);
	handleSent();
	handleRecv();

	return gRecvMessage.opcode;
}

#endif