#pragma once

#ifndef _DBUTILS_H
#define _DBUTILS_H

#include "dataStructures.h"
#include "sqlite3.h"

#define DB_NAME		"data.db"

int accountHasAccessToGroupDb(Account* account, char* groupName);
int addUserToGroupDb(Account* account, Group* group);
int deleteUserFromGroupDb(Account* account, Group* group);
int addGroupDb(Group* group);

// Add part where owner cannot leave the group

// Add part where user wants to go back to the folder whose subdirectory is the group directory.

sqlite3 *db;

/*
Return 0 if success
*/
int openDb() {
	char *err_msg = 0;
	int ret = sqlite3_open(DB_NAME, &db);
	if (ret) {
		fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 1;
	}
	return 0;
}


int readAccountDb(std::list<Account>& accList) {
	Account acc;
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return 1;
	}

	char *sql = "SELECT UID, USERNAME, PASSWORD, LOCKED FROM ACCOUNT;";
	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}

	while (sqlite3_step(res) == SQLITE_ROW) {
		acc.uid = sqlite3_column_int(res, 0);
		strcpy_s(acc.username, CRE_MAXLEN, (const char *) sqlite3_column_text(res, 1));
		strcpy_s(acc.password, CRE_MAXLEN, (const char *) sqlite3_column_text(res, 2));
		acc.isLocked = (sqlite3_column_int(res, 3) == 1);

		accList.push_back(acc);
	}

	sqlite3_finalize(res);
	printf("Account data loaded.\n");
	return 0;
}

int readGroupDb(std::list<Group>& groupList) {
	Group group;
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return 1;
	}

	char *sql = "SELECT GID, GROUPNAME, PATHNAME, OWNERID FROM [GROUP];";
	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}

	while (sqlite3_step(res) == SQLITE_ROW) {
		group.gid = sqlite3_column_int(res, 0);
		strcpy_s(group.groupName, CRE_MAXLEN, (const char *)sqlite3_column_text(res, 1));
		group.ownerId = sqlite3_column_int(res, 2);
		strcpy_s(group.pathName, MAX_PATH, (const char *)sqlite3_column_text(res, 3));

		groupList.push_back(group);
	}

	sqlite3_finalize(res);
	printf("Group data loaded.\n");
	return 0;
}

int accountHasAccessToGroupDb(Account* account, char* groupName) {
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return -1;
	}

	char *sql = "SELECT gm.GID, gm.UID FROM GROUPMEMBER gm"
		"JOIN [GROUP] g ON g.GID = gm.GID "
		"WHERE gm.UID = ? AND g.GROUPNAME = ?;";

	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}
	else {
		sqlite3_bind_int(res, 1, account->uid);
		sqlite3_bind_text(res, 2, groupName, -1, NULL);
	}

	ret = (sqlite3_step(res) == SQLITE_ROW);

	sqlite3_finalize(res);
	return ret;
}

int queryGroupForAccount(Account* account, std::list<Group> &groupList) {
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return -1;
	}

	char *sql = "SELECT g.GID, g.GROUPNAME FROM GROUPMEMBER gm"
		"JOIN [GROUP] g ON g.GID = gm.GID "
		"WHERE gm.UID = ?;";

	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}
	else {
		sqlite3_bind_int(res, 1, account->uid);
	}

	Group group;
	while (sqlite3_step(res) == SQLITE_ROW) {
		group.gid = sqlite3_column_int(res, 0);
		strcpy_s(group.groupName, GROUPNAME_SIZE, (const char *)sqlite3_column_text(res, 1));

		groupList.push_back(group);
	}

	sqlite3_finalize(res);
	return ret;
}

int addUserToGroupDb(Account* account, Group* group) {
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return -1;
	}

	char *sql = "INSERT INTO GROUPMEMBER(GID, UID) VALUES (?, ?);";
	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}
	else {
		sqlite3_bind_int(res, 1, group->gid);
		sqlite3_bind_int(res, 2, account->uid);
	}

	ret = (sqlite3_step(res) == SQLITE_DONE);

	sqlite3_finalize(res);
	return ret;
}

int deleteUserFromGroupDb(Account* account, Group* group) {
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return -1;
	}

	char *sql = "DELETE FROM GROUPMEMBER WHERE uid = ? AND gid = ?;";
	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}
	else {
		sqlite3_bind_int(res, 1, account->uid);
		sqlite3_bind_int(res, 2, group->gid);
	}

	ret = (sqlite3_step(res) == SQLITE_DONE);

	sqlite3_finalize(res);
	return ret;
}

int addGroupDb(Group* group) {
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return -1;
	}

	char *sql = "INSERT INTO [GROUP](GROUPNAME, PATHNAME, OWNERID) VALUES (?, ?, ?);";
	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}
	else {
		sqlite3_bind_text(res, 1, group->groupName, -1, NULL);
		sqlite3_bind_text(res, 2, group->pathName, -1, NULL);
		sqlite3_bind_int(res, 3, group->ownerId);
	}

	ret = (sqlite3_step(res) == SQLITE_DONE);

	sqlite3_finalize(res);
	group->gid = (int) sqlite3_last_insert_rowid(db);

	return ret;
}

int lockAccountDb(Account* account) {
	sqlite3_stmt *res;
	int ret;

	if (db == NULL) {
		fprintf(stderr, "Databased not opened!\n");
		return -1;
	}

	char *sql = "UPDATE ACCOUNT SET LOCKED = 1 WHERE UID=?;";
	ret = sqlite3_prepare_v2(db, sql, -1, &res, 0);
	if (ret != SQLITE_OK) {
		printf("Failed to execute statement: %s\n", sqlite3_errmsg(db));
	}
	else {
		sqlite3_bind_int(res, 1, account->uid);
	}

	ret = (sqlite3_step(res) == SQLITE_DONE);

	sqlite3_finalize(res);

	return ret;
}

void closeDb() {
	sqlite3_close(db);
}

#endif