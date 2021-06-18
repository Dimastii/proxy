#include <winsock.h>

#include <stdlib.h>
#include <iostream>

using namespace std;


//// Прототипы ////////////////////////////////////////////////////////

extern int DoWinsock(const char* pcHost, int nPort);

//// main //////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    // Адрес и порт прокси сервера.
    const char* pcHost = "127.0.0.1";
    int nPort = 8000;

    // Start Winsock.
    WSAData wsaData;

	int nCode;
    if ((nCode = WSAStartup(MAKEWORD(1, 1), &wsaData)) != 0) {
		cerr << "WSAStartup() returned error code " << nCode << "." <<
				endl;
        return 255;
    }
   

    // Вызов главного модуля.
    int retval = DoWinsock(pcHost, nPort);

    // Закрываем Winsock.
    WSACleanup();
    return retval;
}

