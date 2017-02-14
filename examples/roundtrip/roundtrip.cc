#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;

const size_t frameLen = 2*sizeof(int64_t);

void serverConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_TRACE << conn->name() << " " << conn->peerAddress().toIpPort() << " -> "
        << conn->localAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");
  if (conn->connected())
  {
    conn->setTcpNoDelay(true);
  }
  else
  {
  }
}

void serverMessageCallback(const TcpConnectionPtr& conn,
                           Buffer* buffer,
                           muduo::Timestamp receiveTime)
{
  int64_t message[2];
  while (buffer->readableBytes() >= frameLen)
  {
    memcpy(message, buffer->peek(), frameLen);
    buffer->retrieve(frameLen);
    message[1] = receiveTime.microSecondsSinceEpoch();
    conn->send(message, sizeof message);
  }
}

void runServer(uint16_t port)
{
  EventLoop loop;
  TcpServer server(&loop, InetAddress(port), "ClockServer");
  server.setConnectionCallback(serverConnectionCallback);
  server.setMessageCallback(serverMessageCallback);
  server.start();
  loop.loop();
}

std::map<string, TcpConnectionPtr> connections;
//TcpConnectionPtr clientConnection;

void clientConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
        << conn->peerAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");
  if (conn->connected())
  {
    //clientConnection = conn;
    //TcpConnectionPtr tmp = conn;
    string tname = conn->name();
    TcpConnectionPtr tptr =  conn;
    connections.insert(std::pair<string, TcpConnectionPtr>(conn->name(),conn));
    conn->setTcpNoDelay(true);
    //printf("connection %s is connected \n ", conn->name();
  }
  else
  {
    //clientConnection.reset();
   std::map<string,TcpConnectionPtr>::iterator it;
    it = connections.find(conn->name());
    if (it != connections.end())
    {  
	it->second.reset();
	connections.erase(it);
    }
  }
}

void clientMessageCallback(const TcpConnectionPtr& conn,
                           Buffer* buffer,
                           muduo::Timestamp receiveTime)
{
  int64_t message[2];
  while (buffer->readableBytes() >= frameLen)
  {
    memcpy(message, buffer->peek(), frameLen);
    buffer->retrieve(frameLen);
    int64_t send = message[0];
    int64_t their = message[1];
    int64_t back = receiveTime.microSecondsSinceEpoch();
    int64_t mine = (back+send)/2;
    
    LOG_INFO << "round trip " << back - send
             << " clock error " << their - mine
	     << conn->name();
  }
}

void sendMyTime()
{
  // show content:
  for (std::map<string,TcpConnectionPtr>::iterator it = connections.begin(); it!=connections.end(); ++it)
  {
    if(it->second)
    {
       int64_t message[2] = { 0, 0 };
       message[0] = Timestamp::now().microSecondsSinceEpoch();
       it->second->send(message, sizeof message);
    }  
  
  }

 /* if (clientC nnection)
  {
    int64_t message[2] = { 0, 0 };
    message[0] = Timestamp::now().microSecondsSinceEpoch();
    clientConnection->send(message, sizeof message);
  }
  */
}

void runClient(const char* ip, uint16_t port)
{
  EventLoop loop;
  TcpClient client(&loop, InetAddress(ip, port), "ClockClient");
  client.enableRetry();
  client.setConnectionCallback(clientConnectionCallback);
  client.setMessageCallback(clientMessageCallback);
  client.connect();
  
  TcpClient client1(&loop, InetAddress(ip, port), "ClockClient1");
  client1.enableRetry();
  client1.setConnectionCallback(clientConnectionCallback);
  client1.setMessageCallback(clientMessageCallback);
  client1.connect();

  loop.runEvery(0.2, sendMyTime);
  loop.loop();


}

int main(int argc, char* argv[])
{
  if (argc > 2)
  {
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    if (strcmp(argv[1], "-s") == 0)
    {
      runServer(port);
    }
    else
    {
      runClient(argv[1], port);
    }
  }
  else
  {
    printf("Usage:\n%s -s port\n%s ip port\n", argv[0], argv[0]);
  }
}

