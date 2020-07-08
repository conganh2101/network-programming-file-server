
#ifndef UI_H_
#define UI_H_

#include <stdlib.h>
#include "processor.h"

using namespace std;

bool quit = false;
extern bool isLoggedIn;
char username[CRE_MAXLEN];
char password[CRE_MAXLEN];
char* currentGroup;
vector<char*> fileList;

void handleLogIn();
void handleLogOut();
void handleNavigate();
void handleNewFolder();
void handleUpload();
void handleDownload();
void handleVisitGroup();
void handleCreateGroup();
void handleJoinGroup();
void handleLeaveGroup();
void showMenuNotLoggedIn();
void showMenuLoggedIn();
void showGroupMenu();

void getPassword(char *password) {
	char ch = 0;
	int i = 0;

	// Get console mode and disable character echo
	DWORD conMode, dwRead;
	HANDLE handleIn = GetStdHandle(STD_INPUT_HANDLE);

	GetConsoleMode(handleIn, &conMode);
	SetConsoleMode(handleIn, conMode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

	// Read character and echo * to screen
	while (ReadConsoleA(handleIn, &ch, 1, &dwRead, NULL) && ch != '\r') {
		if (ch == '\b') {
			if (i > 0) {
				printf("\b \b");
				i--;
			}
		}
		else if (i < CRE_MAXLEN - 1 && ch != ' ') {
			password[i++] = ch;
			printf("*");
		}
	}
	password[i] = 0;

	// Restore original console mode
	SetConsoleMode(handleIn, conMode);
}

void showStatusMsg(int statusCode, char* opType) {
	switch (statusCode) {
	case OPS_OK:
		printf("%s successful.\n", opType);
		return;
	case OPS_ERR_BADREQUEST:
		printf("Bad request error!\n");
		return;
	case OPS_ERR_WRONGPASS:
		printf("Wrong password. Please try again!\n");
		return;
	case OPS_ERR_NOTLOGGEDIN:
		printf("You did not log in!\n");
		return;
	case OPS_ERR_LOCKED:
		printf("Account locked!\n");
		return;
	case OPS_ERR_ANOTHERCLIENT:
		printf("Account logged in on another client!\n");
		return;
	case OPS_ERR_ALREADYINGROUP:
		printf("Account is already in this group!\n");
		return;
	case OPS_ERR_GROUPEXISTS:
		printf("A group with this name already exists!\n");
		return;
	case OPS_ERR_ALREADYEXISTS:
		printf("Item already exists.\n");
		return;
	case OPS_ERR_SERVERFAIL:
		printf("Internal server error.\n");
		return;
	case OPS_ERR_FORBIDDEN:
		printf("You don't have permission to perform this action.\n");
		return;
	case OPS_ERR_NOTFOUND:
		printf("Item not found.\n");
		return;
	}
}

void showMenuNotLoggedIn() {
	system("cls");
	printf("|================+++============|\n");
	printf("|		  File Transfer			|\n");
	printf("|_____________[Menu]____________|\n");
	printf("|                               |\n");
	printf("|		1. Login				|\n");
	printf("|		2. Exit					|\n");
	printf("|								|\n");
	printf("|===============================|\n");

	char choice = '.';
	do {
		choice = _getch();
		switch (choice) {
		case '1':
			handleLogIn();
			break;
		case '2':
			quit = true;
			return;
		default:
			printf("\nInvalid choice. Please choose again: ");
			choice = '.';
		}
	} while (choice == '.');
}

void showMenuLoggedIn() {
	system("cls");
	printf("|================+++============|\n");
	printf("|		  File Transfer			|\n");
	printf("|_____________[Menu]____________|\n");
	printf("|								|\n");
	printf("|      1. Visit group			|\n");
	printf("|	   2. Create new group		|\n");
	printf("|	   3. Join group			|\n");
	printf("|	   4. Log out				|\n");
	printf("|	   5. Exit					|\n");
	printf("|								|\n");
	printf("|===============================|\n");

	char choice = '.';
	do {
		choice = _getch();
		switch (choice) {
		case '1':
			handleVisitGroup();
			break;
		case '2':
			handleCreateGroup();
		case '3':
			handleJoinGroup();
		case '4':
			handleLogOut();
		case '5':
			quit = true;
			return;
		default:
			printf("\nInvalid choice. Please choose again: ");
			choice = '.';
		}
	} while (choice == '.');
}

void showGroupMenu() {
	system("cls");
	printf("|================+++============|\n");
	printf("|		  File Transfer			|\n");
	printf("|_____________[Menu]____________|\n");
	printf("|								|\n");
	printf("|      1. Navigate				|\n");
	printf("|	   2. Create new folder		|\n");
	printf("|	   3. Upload file			|\n");
	printf("|	   4. Download file			|\n");
	printf("|	   5. Leave group			|\n");
	printf("|	   6. Back					|\n");
	printf("|      7. Exit					|\n");
	printf("|								|\n");
	printf("|===============================|\n");

	char choice = '.';
	do {
		choice = _getch();
		switch (choice) {
		case '1':
			handleNavigate();
			break;
		case '2':
			handleNewFolder();
			break;
		case '3':
			handleUpload();
			break;
		case '4':
			handleDownload();
			break;
		case '5':
			handleLeaveGroup();
		case '6':
			//handleBack();
		case '7':
			quit = true;
			return;
		default:
			printf("\nInvalid choice. Please choose again: ");
			choice = '.';
		}
	} while (choice == '.');
}


/*****************************
Group
*****************************/

void handleVisitGroup() {
	printf("\n");

	int groupCount = 0;
	vector<char*> groupList;

	int ret = processOpGroupList(groupList);
	if (ret == 1) {
		printf("\nError sending to server.\n");
		Sleep(2000);
		return;
	}

	for (char* group : groupList) {
		printf("%d. %s", ++groupCount, group);
	}

	int selection;
	bool valid;
	do {
		valid = true;
		printf("Choose group: ");
		scanf("%d", &selection);
		
		if (selection > groupCount || selection < 0) {
			valid = false;
		}

	} while (!valid);
	printf("\n\n");

	ret = processOpGroup(OPG_GROUP_USE, groupList[selection]);
	if (ret == 1) {
		printf("\nError sending to server.\n");
		for (char* group : groupList) {
			free(group);
		}

		Sleep(2000);
		return;
	}

	currentGroup = groupList[selection];
	for (char* group : groupList) {
		if (group != currentGroup) free(group);
	}

	showStatusMsg(ret, "Enter group");
	Sleep(2000);
}

void handleCreateGroup() {
	char newGroupName[GROUPNAME_SIZE];
	printf("\n");

	bool valid;
	do {
		valid = true;
		printf("Group name should be %d characters or less.\n", GROUPNAME_SIZE);
		printf("Don't input over %d characters, else it will be truncated.\n\n", GROUPNAME_SIZE);
		printf("New group name: ");
		gets_s(newGroupName, GROUPNAME_SIZE);
	} while (!valid);
	printf("\n\n");

	int ret = processOpGroup(OPG_GROUP_NEW, newGroupName);
	if (ret == 1) {
		printf("\nError sending to server.\n");
		Sleep(2000);
		return;
	}
	showStatusMsg(ret, "Create group");
	Sleep(2000);
}

void handleJoinGroup() {
	char groupName[GROUPNAME_SIZE];
	printf("\n");

	bool valid;
	do {
		valid = true;
		printf("Group name should be %d characters or less.\n", GROUPNAME_SIZE);
		printf("Don't input over %d characters, else it will be truncated.\n\n", GROUPNAME_SIZE);
		printf("Enter group name to join: ");
		gets_s(groupName, GROUPNAME_SIZE);
	} while (!valid);
	printf("\n\n");

	int ret = processOpGroup(OPG_GROUP_JOIN, groupName);
	if (ret == 1) {
		printf("\nError sending to server.\n");
		Sleep(2000);
		return;
	}
	showStatusMsg(ret, "Create group");
	
	Sleep(2000);
}

void handleLeaveGroup() {
	char ch;
	int iRetVal;
	printf("\n");
	printf("Are you sure you want to leave this group? (Y/..): ");
	ch = _getch();
	if (ch == 'Y' || ch == 'y') {
		iRetVal = processOpGroup(OPG_GROUP_LEAVE, currentGroup);
		if (iRetVal == 1) {
			printf("\nError connecting to server.\n");
			Sleep(2000);
			return;
		}
		showStatusMsg(iRetVal, "Leave group");
		Sleep(2000);
		return;
	}
	printf("\nLeave group aborted.");
	Sleep(2000);
}


/*****************************
Browsing
*****************************/

void handleNavigate() {
	char path[MAX_PATH];
	printf("\n");

	bool valid;
	do {
		valid = true;
		printf("Path should be %d characters or less.\n", MAX_PATH);
		printf("Don't input over %d characters, else it will be truncated.\n\n", MAX_PATH);
		printf("Where to? ");
		gets_s(path, MAX_PATH);
	} while (!valid);
	printf("\n\n");

	int ret = processOpBrowse(OPB_FILE_CD, path);
	if (ret == 1) {
		printf("\nError sending to server.\n");
		Sleep(2000);
		return;
	}

	showStatusMsg(ret, "Navigating");
	Sleep(2000);
}

void handleNewFolder() {
	char newFolderName[MAX_PATH];
	printf("\n");

	bool valid;
	do {
		valid = true;
		printf("Folder name should be %d characters or less.\n", MAX_PATH);
		printf("Don't input over %d characters, else it will be truncated.\n\n", MAX_PATH);
		printf("New folder name: ");
		gets_s(newFolderName, MAX_PATH);
	} while (!valid);
	printf("\n\n");

	int ret = processOpBrowse(OPB_DIR_NEW, newFolderName);
	if (ret == 1) {
		printf("\nError sending to server.\n");
		Sleep(2000);
		return;
	}

	showStatusMsg(ret, "Create new folder");
	Sleep(2000);
}

void handleUpload() {
	char fileName[MAX_PATH];
	printf("\n");

	bool valid;
	do {
		valid = true;
		printf("File name should be %d characters or less.\n", MAX_PATH);
		printf("Don't input over %d characters, else it will be truncated.\n\n", MAX_PATH);
		printf("Where to? ");
		gets_s(fileName, GROUPNAME_SIZE);
	} while (!valid);
	printf("\n\n");

	uploadFileToServer(fileName);

	Sleep(2000);
}

void handleDownload() {
	char path[MAX_PATH];
	printf("\n");

	bool valid;
	do {
		valid = true;
		printf("Path should be %d characters or less.\n", MAX_PATH);
		printf("Don't input over %d characters, else it will be truncated.\n\n", MAX_PATH);
		printf("Where to? ");
		gets_s(path, GROUPNAME_SIZE);
	} while (!valid);
	printf("\n\n");

	downloadFileFromServer(path);

	Sleep(2000);
}


/*****************************
Authentication
*****************************/

void handleLogOut() {
	char ch;
	int iRetVal;
	printf("\n");
	printf("Are you sure you want to log out? (Y/..): ");
	ch = _getch();
	if (ch == 'Y' || ch == 'y') {
		iRetVal = processOpLogOut();
		if (iRetVal == 1) {
			printf("\nClient will quit.\n");
			quit = true;
			Sleep(2000);
			return;
		}
		showStatusMsg(iRetVal, "Log out");
		Sleep(2000);
		return;
	}
	printf("\nLog out aborted.");
	Sleep(2000);
}

void handleLogIn() {
	printf("Your username and password should be %d characters or less,\n", CRE_MAXLEN);
	printf("and cannot contain spaces.\n");
	printf("Don't input over %d characters, else it will be truncated.\n\n", CRE_MAXLEN);

	bool valid;
	do {
		valid = true;
		printf("Username: ");
		scanf_s("%s", username, (unsigned)_countof(username));

		printf("Password: ");
		getPassword(password);
		if (strlen(username) == 0) {
			printf("\nUsername cannot be empty.\n\n");
			valid = false;
		}
		if (strlen(password) == 0) {
			printf("\nPassword cannot be empty.\n\n");
			valid = false;
		}
	} while (!valid);
	printf("\n\n");

	int ret = processOpLogIn(username, password);
	if (ret == 1) {
		printf("\nClient will quit.\n");
		quit = true;
		Sleep(2000);
		return;
	}
	showStatusMsg(ret, "Log in");
	Sleep(2000);
}


/*****************************
Runner
*****************************/
void runUI() {

	// Try to reauthenticate
	int iRetVal = processOpReauth();
	showStatusMsg(iRetVal, "Reauthenticate");
	if (iRetVal == 1) {
		return;
	}

	// Main menu loop
	while (!quit) {
		if (isLoggedIn)
			showMenuLoggedIn();
		else
			showMenuNotLoggedIn();
	}
}

#endif