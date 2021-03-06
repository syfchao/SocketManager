#include "SocketManager.h"

class SocketManagerImplExample : public SocketManager {
public:
    explicit SocketManagerImplExample(Type t, unsigned short factor = 0) : SocketManager(t, factor) {}
private:
    int ReceiveData(const char *data, u_long length, Socket *socket) final {
        LOG("receive bytes : %.*s\n", length, data);
        if(length == 5 && strncmp(data, "ping\n", 5) == 0)
            SendData("pong\n", 5, socket);
        else if(length == 5 && strncmp(data, "quit\n", 5) == 0) {
            CloseSocket(socket);
        }
        return 1;
    }
};


static constexpr char   address[]               = "127.0.0.1";
static const u_short    port                    = 55555;


int pingpongStressTest(){
    static const int N = 10;
    static const int N2 = 5; //0 for infinite test

    RPC_STATUS                  status;
    SocketManagerImplExample    serverManager(SocketManager::Type::SERVER);
    SocketManagerImplExample    clientManager(SocketManager::Type::CLIENT);
    UUID                        serverSocketId, socketId[N];

    if(serverManager.isReady()) {
        serverSocketId = serverManager.ListenToNewSocket(port);
        if (UuidIsNil(&serverSocketId, &status))
            return 1;
    } else
        return 1;
    if(clientManager.isReady()) {
        for(int i = 0 ; i < N ; i++) {
            socketId[i] = clientManager.ConnectToNewSocket(address, port);
            if (UuidIsNil(socketId+i, &status)) {
                return 1;
            }
        }
    } else
        return 1;

    for (;;) {
        if (!serverManager.isServerSocketReady(serverSocketId)) {
            if (serverManager.isSocketInitialising(serverSocketId))
                Sleep(100);
            else
                return 1;
        } else {
            break;
        }
    }

    for (int i = 0 ; N2 == 0 || i < N2 ; i++) {
        int n = serverManager.SendDataToAll("ping\n", 5);
        LOG("Sent ping data to %d sockets\n", n);
    }
    return 0;
}

int idleExample(int argc){
    RPC_STATUS                  status;
//    char                        data[65536];
    bool                        isServer = argc > 1;
    SocketManagerImplExample    manager(isServer ? SocketManager::Type::SERVER : SocketManager::Type::CLIENT);
    UUID                        socketId;

    if(manager.isReady()) {
        if (isServer) {
            socketId = manager.ListenToNewSocket(port);
            if (!UuidIsNil(&socketId, &status)) {
                for (;;) {
                    if (!manager.isServerSocketReady(socketId)) {
                        if (manager.isSocketInitialising(socketId))
                            Sleep(100);
                        else
                            socketId = manager.ListenToNewSocket(port);
                    }
                }
            }
        } else {
            socketId = manager.ConnectToNewSocket(address, port);
            if (!UuidIsNil(&socketId, &status)) {
                for (;;) {
                    if (!manager.isClientSocketReady(socketId)) {
                        if (manager.isSocketInitialising(socketId))
                            Sleep(100);
                        else
                            socketId = manager.ConnectToNewSocket(address, port);
                    }
                    /*
                    for (int i = 1; i < 65536; i = i == 65000 ? 65536 : i + 1000) {
                        manager.SendData(data, i, socketId);
                    }
                    */
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[]){
    return pingpongStressTest();
//    return idleExample(argc);
}

