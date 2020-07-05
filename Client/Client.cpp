// TCPClient.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "ui.h"

int main(int argc, char** argv)
{
	//Step 3: Specify server address
	char*  IP;
	unsigned long ulAddr;
	IP = (char *)argv[1];
	ulAddr = inet_addr(IP);

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons((unsigned short)atoi((char *)argv[2]));
	serverAddr.sin_addr.s_addr = ulAddr;
	return 0;


	// Validate parameters
	if (argc != 3) {
		printf("Wrong arguments! Please enter in format: \"%s [ServerIpAddress] [ServerPortNumber]\"", argv[0]);
		return 1;
	}

	char* serverIpAddr = argv[1];
	short serverPortNumbr = atoi(argv[2]);

	// Initialize network
	int iRetVal = initConnection(serverIpAddr, serverPortNumbr);
	if (iRetVal == 1)
		return 1;

	// Open data file
	iRetVal = openFile("accData");
	if (iRetVal == 1)
		return 1;

	// Start program
	startUI();
	ioCleanup();

	return 0;
}