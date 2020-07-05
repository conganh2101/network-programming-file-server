#pragma once

#ifndef PROCESSOR_H_
#define PROCESSOR_H_

#include "defs.h"
#include "network.h"
#include "fileUtils.h"

bool isLoggedIn = false;
char buff[BUFF_SIZE];
char cookie[COOKIE_LEN];


void packMessage(LPMESSAGE message, int opcode, int length, int offset, int burst, char* payload) {
	message->opcode = opcode;
	message->length = 0;
	message->offset = 0;
	message->burst = 0;
	strcpy(message->payload, payload);
}

/*
Parse response in buff. Extract sid if
status code is RES_OK. Return status code
*/

int reqLogIn(char* username, char* password) {
	MESSAGE message;
	// Construct request
	packMessage(&message, OPA_USERNAME, strlen(username), 0, 0, username);

	// Send
	if (sendReq(buff, BUFF_SIZE) == 1) {
		return 1;
	}

	// Parse response
	int iRetVal = parseResponse();
	if (iRetVal == RES_OK) {
		isLoggedIn = true;
	}
	return iRetVal;
}

int reqLogOut() {
	// Encapsulate request
	snprintf(buff, BUFF_SIZE, "LOGOUT %s", sid);
	if (sendReq(buff, BUFF_LEN) == 1) {
		return 1;
	}
	// Parse response
	int iRetVal = parseResponse();
	if (iRetVal == RES_OK) {
		isLoggedIn = false;
	}
	return iRetVal;
}

int reqReauth() {
	// Read SID from file
	readCookieFromFile(sid, SID_LEN);
	if (strlen(sid) != SID_LEN - 1) {
		return -1;
	}
	else {
		// Encapsulate reauth message
		snprintf(buff, BUFF_SIZE, "REAUTH %s", sid);
		if (sendReq(buff, BUFF_LEN) == 1) {
			return 1;
		}

		// Parse response
		int iRetVal = parseResponse();
		switch (iRetVal) {
		case RES_OK:
			isLoggedIn = true;
			break;
		case RES_RA_FOUND_NOTLI:
			isLoggedIn = false;
			break;
		default:
			isLoggedIn = false;
			sid[0] = 0;		  // Clear saved sid if
			writeCookietoFile(sid); // sid not found on server
		}
		return iRetVal;
	}
}

int reqSid() {
	// Encapsulate request
	snprintf(buff, BUFF_LEN, "REQUESTSID");
	if (sendReq(buff, BUFF_LEN) == 1) {
		return 1;
	}
	// Parse response in buff
	int iRetVal = parseResponse();
	if (iRetVal == RES_OK) {
		writeCookietoFile(sid);
	}
	return (iRetVal != RES_OK);
}

#endif