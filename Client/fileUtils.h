#pragma once

#ifndef FILEUTILS_H_
#define FILEUTILS_H_

#include "stdio.h"
#include "conio.h"

char* fName;
FILE* f;

/***********************************
/            FILES UTILS           /
***********************************/

int openFile(char* fileName) {
	int retVal = fopen_s(&f, fileName, "r+");
	fName = fileName;
	if (f == NULL) {
		printf("File does not exist. Creating new file.\n");
		retVal = fopen_s(&f, fileName, "w+");
		if (retVal) {
			printf("Cannot create new file.\n");
			return retVal;
		}
	}
	printf("Open file successfully.\n");
	return retVal;
}

int readCookieFromFile(char* cookie, int sidLen) {
	if (f == NULL) {
		return 1;
	}
	fseek(f, 0, SEEK_SET);
	fgets(cookie, sidLen, f);
	return 0;
}

int writeCookietoFile(char* cookie) {
	if (f == NULL) {
		return 1;
	}
	fseek(f, 0, SEEK_SET);
	fputs(cookie, f);
	// Close to save and reopen file
	fclose(f);
	if (openFile(fName))
		return 1;
	return 0;
}

#endif