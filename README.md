Reference from where I learned to use IOCP with sockets: https://www.codeproject.com/Articles/10330/A-simple-IOCP-Server-Client-Class
no direct way to access socket object because sockets can be closed anytime. Instead, pass by functions of the manager that's going to fetch socket from a map UUID->Socket* each time any operation on the socket must be done