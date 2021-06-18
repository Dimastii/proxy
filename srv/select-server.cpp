#include "ws-util.h"

#include <winsock.h>

#include <iostream>
#include <fstream>
#include <vector>
#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable: 4996)
using namespace std;


////////////////////////////////////////////////////////////////////////
// Константы 

const int kBufferSize = 8192;
		

////////////////////////////////////////////////////////////////////////
// Глобалы и типы
enum State
{
	READ_FROM_CLIENT,
	READ_FROM_DB,
	SEND_TO_CLIENT,
	SEND_TO_DB,
};

struct Connection {
	SOCKET sd;
	SOCKET sockDb;
	char acBuffer[kBufferSize];
	int nCharsInBuffer;
	State state;
	int nBytesRead;
	bool (*fcnPtr)(Connection& conn);
	ofstream logFd;
	Connection(SOCKET sd_, SOCKET sockDb_) : sd(sd_), sockDb(sockDb_), nCharsInBuffer(0), state(READ_FROM_CLIENT){ }
};
typedef vector<Connection> ConnectionList;

ConnectionList gConnections;


////////////////////////////////////////////////////////////////////////
// Прототипы

SOCKET SetUpListener(const char* pcAddress, int nPort);
void AcceptConnections(SOCKET ListeningSocket);


//// DoWinsock /////////////////////////////////////////////////////////
// Функция главного модуля. Просто тоговим просушиваемый сокет и вызываем обработчик событий.

int DoWinsock(const char* pcAddress, int nPort)
{
	cout << "Establishing the listener..." << endl;
	SOCKET ListeningSocket = SetUpListener(pcAddress, htons(nPort));
	if (ListeningSocket == INVALID_SOCKET) {
		cout << endl << WSAGetLastErrorMessage("establish listener") << 
				endl;
		return 3;
	}
	cout << "Waiting for connections..." << flush;
	while (1) {
		AcceptConnections(ListeningSocket);
		cout << "Acceptor restarting..." << endl;
	}

#if defined(_MSC_VER)
	return 0;   // warning eater
#endif
}


//// SetUpListener /////////////////////////////////////////////////////
// Устанавливает прослушиватель для данного интерфейса и порта, возвращая прослушивающий сокет в случае успеха;
//в противном случае возвращает INVALID_SOCKET.

SOCKET SetUpListener(const char* pcAddress, int nPort)
{
	u_long nInterfaceAddr = inet_addr(pcAddress);
	if (nInterfaceAddr != INADDR_NONE) {
		SOCKET sd = socket(AF_INET, SOCK_STREAM, 0);
		if (sd != INVALID_SOCKET) {
			sockaddr_in sinInterface;
			sinInterface.sin_family = AF_INET;
			sinInterface.sin_addr.s_addr = nInterfaceAddr;
			sinInterface.sin_port = nPort;
			if (bind(sd, (sockaddr*)&sinInterface, 
					sizeof(sockaddr_in)) != SOCKET_ERROR) {
				listen(sd, SOMAXCONN);
				return sd;
			}
			else {
				cerr << WSAGetLastErrorMessage("bind() failed") <<
						endl;
			}
		}
	}

	return INVALID_SOCKET;
}


//// ReadData //////////////////////////////////////////////////////////
// Блок чтения данных от клиента из базы данных.

bool ReadDataFromDb(Connection& conn)
{
	conn.nBytesRead = recv(conn.sockDb, conn.acBuffer,
		kBufferSize, 0);
	if (conn.nBytesRead == 0) {
		cout << "Socket(DB) " << conn.sd <<
			" was closed by the client. Shutting down." << endl;
		return false;
	}
	(conn).state = SEND_TO_CLIENT;
	return true;
}
// Функция проверки полученой строки на содержание в ней запроса.
bool Checker(string data)
{
	string::iterator it = data.begin();
	while (it != data.end() - 1) {
		if (*it > 'A' && *it < 'Z')
			return true;
		++it;
	}
	return false;
}
// Функция логгирования запросов в файл
// Новый запрос будет появляться в новой строке,
// но записываться в том виде, в котором был послан.
void Logging(string data)
{
	ofstream logFd("cppstudio.txt", ios_base::app);
	if (!logFd.is_open())
	{
		cerr << "Open file error" << endl;
	}
	if (Checker(data))
	{
		string::iterator it = data.begin();
		while (it != data.end() - 1) {
			if (*it == ';')
				*it = '\n';
			++it;
		}
		logFd <<  data << endl;
	}
	logFd.close();
}

bool ReadDataFromClient(Connection& conn) 
{
	string data;
	conn.nBytesRead = recv(conn.sd, conn.acBuffer,
		kBufferSize, 0);
	if (conn.nBytesRead == 0) {
		cout << "Socket " << conn.sd << 
				" was closed by the client. Shutting down." << endl;
		return false;
	}
	data = conn.acBuffer + 5;
	if (data.size() != 0)
		Logging(data);
	(conn).state = SEND_TO_DB;

	return true;
}


//// WriteData /////////////////////////////////////////////////////////
// Блок отправки данных клиенту и базе данных.
bool WriteDataToDb(Connection& conn)
{
	int nBytes = send(conn.sockDb, conn.acBuffer, conn.nBytesRead, 0);
	if (nBytes == SOCKET_ERROR) {
		// Something bad happened on the socket.  Deal with it.
		int err;
		int errlen = sizeof(err);
		getsockopt(conn.sd, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
		return (err == WSAEWOULDBLOCK);
	}
	(conn).state = READ_FROM_DB;

	return true;
}


bool WriteDataToClient(Connection& conn) 
{
	int nBytes = send(conn.sd, conn.acBuffer, conn.nBytesRead, 0);
	if (nBytes == SOCKET_ERROR) {
		int err;
		int errlen = sizeof(err);
		getsockopt(conn.sd, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);
		return (err == WSAEWOULDBLOCK);
	}
	(conn).state = READ_FROM_CLIENT;
	
	return true;
}

//// SetupFDSets ///////////////////////////////////////////////////////
// Функция обновляет наборы отслеживаемых сокетов.

void SetupFDSets(fd_set& ReadFDs, fd_set& WriteFDs,
	fd_set& ExceptFDs, SOCKET ListeningSocket = INVALID_SOCKET)
{
	FD_ZERO(&ReadFDs);
	FD_ZERO(&WriteFDs);
	FD_ZERO(&ExceptFDs);


	if (ListeningSocket != INVALID_SOCKET) {
		FD_SET(ListeningSocket, &ReadFDs);
		FD_SET(ListeningSocket, &ExceptFDs);
	}
	ConnectionList::iterator it = gConnections.begin();
	while (it != gConnections.end()) {
		FD_SET(it->sd, &ReadFDs);
		switch ((*it).state)
		{
		case READ_FROM_CLIENT:
		{
			FD_SET(it->sd, &ReadFDs);
			(*it).fcnPtr = ReadDataFromClient;
			break;
		}
		case READ_FROM_DB:
		{
			FD_SET(it->sockDb, &ReadFDs);
			(*it).fcnPtr = ReadDataFromDb;
			break;
		}
		case SEND_TO_CLIENT:
		{
			FD_SET(it->sd, &WriteFDs);
			(*it).fcnPtr = WriteDataToClient;
			break;
		}
		case SEND_TO_DB:
		{
			FD_SET(it->sockDb, &WriteFDs);
			(*it).fcnPtr = WriteDataToDb;
			break;
		}
		default:
			break;
		}
		FD_SET(it->sd, &ExceptFDs);
		++it;
	}
}
//// AcceptConnections /////////////////////////////////////////////////
// Функция отлавливает события на сокетах.
void AcceptConnections(SOCKET ListeningSocket)
{
	sockaddr_in sinRemote;
	int nAddrSize = sizeof(sinRemote);

	while (1) {
		fd_set ReadFDs, WriteFDs, ExceptFDs;
		SetupFDSets(ReadFDs, WriteFDs, ExceptFDs, ListeningSocket);

		if (select(0, &ReadFDs, &WriteFDs, &ExceptFDs, 0) > 0) {
			//// select поймал событие на сокете.
			// Если это прослушиваемый сокет то...
			if (FD_ISSET(ListeningSocket, &ReadFDs)) {
				// Принимаем соединение
				SOCKET sd = accept(ListeningSocket, 
						(sockaddr*)&sinRemote, &nAddrSize);
				if (sd != INVALID_SOCKET) {
					cout << "Accepted connection from " <<
							inet_ntoa(sinRemote.sin_addr) << ":" <<
							ntohs(sinRemote.sin_port) << 
							", socket " << sd << "." << endl;
					SOCKADDR_IN addr;
					int sizeofaddr = sizeof(addr);
					addr.sin_addr.s_addr = inet_addr("127.0.0.1");
					addr.sin_port = htons(5432);
					addr.sin_family = AF_INET;

					SOCKET sockDb = socket(AF_INET, SOCK_STREAM, NULL);
					if (connect(sockDb, (SOCKADDR*)&addr, sizeof(addr)) != 0) {
						cerr << "Error: failed connect to server.\n";
					}

					gConnections.push_back(Connection(sd, sockDb));
					u_long nNoBlock = 1;
					ioctlsocket(sd, FIONBIO, &nNoBlock);
				}
				else {
					cerr << WSAGetLastErrorMessage("accept() failed") << 
							endl;
					return;
				}
			}
			else if (FD_ISSET(ListeningSocket, &ExceptFDs)) {
				int err;
				int errlen = sizeof(err);
				getsockopt(ListeningSocket, SOL_SOCKET, SO_ERROR,
						(char*)&err, &errlen);
				cerr << WSAGetLastErrorMessage(
						"Exception on listening socket: ", err) << endl;
				return;
			}

			// ...или это один из клиентских сокетов?
			ConnectionList::iterator it = gConnections.begin();
			while (it != gConnections.end()) {
				bool bOK = true;
				const char* pcErrorType = 0;

				// Проверяю клиентский сокет на событие 

				if (FD_ISSET(it->sd, &ExceptFDs)) {
					bOK = false;
					pcErrorType = "General socket error";
					FD_CLR(it->sd, &ExceptFDs);
				}
				else {
					if(FD_ISSET(it->sd, &ReadFDs) ||
						FD_ISSET(it->sd, &WriteFDs) ||
						FD_ISSET(it->sockDb, &ReadFDs) ||
						FD_ISSET(it->sockDb, &WriteFDs))
					{
						bOK = it->fcnPtr(*it); // Вызываю функцию, которая соответствует текущему состоянию соединения
					}
				}

				if (!bOK) {
					//Что-то случилось с сокетом, или клиент закрыл свою половину соединения.
					//Выключем соединение и удалите его из списка.
					int err;
					int errlen = sizeof(err);
					getsockopt(it->sd, SOL_SOCKET, SO_ERROR,
							(char*)&err, &errlen);
					if (err != NO_ERROR) {
						cerr << WSAGetLastErrorMessage(pcErrorType,
								err) << endl;
					}
					ShutdownConnection(it->sd);
					gConnections.erase(it);
					it = gConnections.begin();
				}
				else {
					// Перейти к следующему подключению
					++it;
				}
			}
		}
		else {
			cerr << WSAGetLastErrorMessage("select() failed") << endl;
			return;
		}
	}
}

