#ifndef SOCKETMANAGER_SOCKETMANAGER_H
#define SOCKETMANAGER_SOCKETMANAGER_H

#include "SocketHelperClasses.h"
#include <vector>


class SocketManager {                            // Manage a client connected to an arbitrary number of socket server
    friend class Socket;

private:
    /********************** Static Attributes ************************/

    static const int            FAMILY                          = AF_INET;      // IPv4 address family.
    static const int            THREADS_PER_PROC                = 1;
    static const int            MAX_UNUSED_SOCKET               = 30;
    static const int            DEFAULT_TIME_WAIT_VALUE         = 120000;       // Either 120 or 240sec depending on doc page, but tests confirmed 120 (https://docs.microsoft.com/en-us/biztalk/technical-guides/settings-that-can-be-modified-to-improve-network-performance | https://docs.microsoft.com/en-us/previous-versions/windows/it-pro/windows-2000-server/cc938217(v=technet.10))
    static const int            MIN_TIME_WAIT_VALUE             = 30000;        // Range goes from 30 to 300sec according to microsoft doc
    static const int            MAX_TIME_WAIT_VALUE             = 300000;       // Range goes from 30 to 300sec according to microsoft doc
    static const LONG64         DEFAULT_MAX_PENDING_BYTE_SENT   = 65536;        // 64k, default value only if isb query fail (shouldn't happen)
    static const TCHAR *        TIME_WAIT_REG_KEY;
    static const TCHAR *        TIME_WAIT_REG_VALUE;
    static DWORD                TimeWaitValue;
    static LPFN_CONNECTEX       ConnectEx;
    static LPFN_DISCONNECTEX    DisconnectEx;
    static LPFN_ACCEPTEX        AcceptEx;

    ////////////////////// End Static Attributes /////////////////////

    /************************ Internal def *************************/
public:
    enum Type {
        CLIENT,
        SERVER
    };

private:
    enum State {
        NOT_INITIALIZED,
        WSA_INITIALIZED,
        IOCP_INITIALIZED,
        THREADS_INITIALIZED,
        MSWSOCK_FUNC_INITIALIZED,
        TIME_WAIT_VALUE_SELECTED,
        READY,
        SERVER_LISTENING
    };

    //////////////////////// End Internal def //////////////////////

    /************************ Attributes *************************/

    CriticalRecyclableList<Socket>  inUseSocketList;            // All sockets this instance is currently connected to (linked list are used here because pointers to its elements are used elsewhere and it's the only container to guarantee they will never be moved once allocated, no matter what operation is done on the list)
    CriticalRecyclableList<Buffer>  inUseBufferList;            // All buffers currently used in an overlapped operation
    CriticalQueue<Socket*>          reusableSocketQueue;        // All sockets previously disconnected that can be reused
    Socket*                         currentAcceptSocket;        // Last accept socket if manager is in server mode (because event is raised in the listen socket)
    CriticalMap<UUID, Socket*>      socketAccessMap;            // Only way to access a socket pointer from outside of this class, to prevent invalid memory access

    State                           state;                      // Current state of this instance, used for cleanup and to test readiness
    std::vector<HANDLE>             threadHandles;              // Handles to all threads receiving IOCP events
    HANDLE                          iocpHandle;                 // Handle to IO completion port
    unsigned short                  isbFactor;                  // Factor of isb that sendbuffer can fill before no new send are allowed (0 for no limit)
protected:
    Type                            type;                       // Type of this manager, either client or server
    //////////////////////// End Attributes //////////////////////

    /************************ Methods **************************/
private:
    static DWORD WINAPI IOCPWorkerThread        (LPVOID lpParam);                                       // Per-thread function receiving IOCP events

    void                HandleError             (Socket *sockObj, Buffer *buf, DWORD error);            // Manage one IOCP error
    void                HandleIo                (Socket *sockObj, Buffer *buf, DWORD bytesTransfered);  // Manage one IOCP event, calling all needed functions
    void                HandleRead              (Socket *sockObj, Buffer *buf, DWORD bytesTransfered);
    void                HandleWrite             (Socket *sockObj, Buffer *buf, DWORD bytesTransfered);
    void                HandleConnection        (Socket *sockObj, Buffer *buf);
    void                HandleDisconnect        (Socket *sockObj, Buffer *buf);
    void                UpdateISB               (Socket *sockObj, Buffer *buf);                         // Get ISB value to calculate threshold value for pending send operation
    int                 PostRecv                (Socket *sock, Buffer *recvObj);                        // Post an overlapped recv operation on the socket
    int                 PostSend                (Socket *sock, Buffer *sendObj);                        // Post an overlapped send operation on the socket
    int                 PostISBNotify           (Socket *sock, Buffer *isbObj);                         // Post an overlapped operation on the socket to be notified of ideal send backlog value change
    void                ClearThreads            ();                                                     // Tells all working threads to shut down and free resources
    bool                InitAsyncSocketFuncs    ();                                                     // Initialize function pointer to needed mswsock functions
    bool                InitAsyncSocketFunc     (SOCKET sock, GUID guid, LPVOID func, DWORD size);      // Initialize function pointer to one mswsock function
    void                InitTimeWaitValue       ();                                                     // Initialize TIME_WAIT detected value
    bool                ShouldReuseSocket       ();                                                     // returns a bool indicating if manager is accepting to reuse socket
    Socket*             ReuseSocket             ();                                                     // Try to recycle a disconnected socket, or create a new one
    UUID                ConnectToNewSocket      (const char *address, u_short port, UUID id);           // Connect to and start listening to new read/write event on this socket
    Socket *            GenerateSocket          (bool reuse);                                           // Generate a new socket object, reuse one if possible
    bool                AssociateSocketToIOCP   (Socket *sockObj);                                      // Associate socket to IOCP, delete it if failure
    bool                BindSocket              (Socket *sockObj, SOCKADDR_IN sockAddr);                // Bind socket to given address, delete it if failure
    int                 SetSocketOption         (SOCKET s, int option, const char *optPtr, int optSize);// Set a socket option to a given value and return error status
    inline int          SetSocketOption         (SOCKET s, int option, bool value)                      { return SetSocketOption(s, option, (const char*)&value, sizeof(value)); }
    int                 GetSocketOption         (SOCKET s, int option, char *optPtr, int optSize);      // Get the value of a given socket option and return error status
    void                AddSocketToMap          (Socket *sockObj, UUID id);                             // Give unique id to socket and add it to access map
    bool                AcceptNewSocket         (Socket *listenSockObj);                                // Create a new socket waiting to accept new connection
    void                ChangeSocketState       (Socket *sock, Socket::SocketState state);              // Change the state of a socket (for manual close or failure for example)
protected:
    inline void         CloseSocket             (Socket *sock)                                          { ChangeSocketState(sock, Socket::SocketState::CLOSING); }
    bool                SendData                (const char *data, u_long length, Socket *socket);      // Send a given buffer to the given socket
    virtual int         ReceiveData             (const char* data, u_long length, Socket *socket) = 0;  // Do what needs to be done when receiving content from a socket
public:
    explicit            SocketManager           (Type t, unsigned short factor = 0);
                        ~SocketManager          ();
    UUID                ListenToNewSocket       (u_short port, bool fewCLientsExpected = false);        // Start listening to new connection event on this socket and handle those connection in new sockets
    inline UUID         ConnectToNewSocket      (const char *address, u_short port)                     { return ConnectToNewSocket(address, port, Misc::CreateNilUUID()); }
    inline bool         isReady                 () const                                                { return state == State::READY; };
    inline bool         isSocketInitialising    (UUID socketId)                                         { Socket *sockObj = socketAccessMap.Get(socketId); return sockObj != nullptr && sockObj->state <= Socket::SocketState::RETRY_CONNECTION; };
    inline bool         isClientSocketReady     (UUID socketId)                                         { Socket *sockObj = socketAccessMap.Get(socketId); return sockObj != nullptr && sockObj->state == Socket::SocketState::CONNECTED; };
    inline bool         isServerSocketReady     (UUID socketId)                                         { Socket *sockObj = socketAccessMap.Get(socketId); return sockObj != nullptr && sockObj->state == Socket::SocketState::LISTENING; };
    inline bool         SendData                (const char *data, u_long length, UUID socketId)        { return SendData(data, length, socketAccessMap.Get(socketId)); }
    inline int          SendDataToAll           (const char *data, u_long length)                       { int nbSucc = 0; for (Socket& sock : inUseSocketList.list) nbSucc += SendData(data, length, &sock); return nbSucc; }

    //////////////////////// End Methods ///////////////////////
};

#endif //SOCKETMANAGER_SOCKETMANAGER_H
