#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <boost/dynamic_bitset.hpp>

#include <boost/bind.hpp>
#include <stdio.h>

using namespace muduo;
using namespace muduo::net;
using namespace boost;

boost::dynamic_bitset<> reps_state (2147483647ul); 
AtomicInt64 req_seq; // current requst packet sequence number
AtomicInt64 res_seq; // current response packet sequence number

int32_t query_cycle = 2; // send a packet every 2 milli second.
int32_t dead_intvl = 3;  // think the packet lost if the response 
int32_t sim_lost = 10;  //simulate the packet lost rate;

const size_t frameLen = 3*sizeof(int64_t);

float cal_lost_rate()
{
   uint64_t top = res_seq.get();
   uint64_t lost = 0;
   uint64_t i =0;
   for (i = top; ((i > 0) && ((top -i) < 10000)) ; i--)
   {
       if(!reps_state[i])
	   lost++; 
   }
   printf("%ld pakcet lost in total %ld \n", lost , top-i);

   float rate = (float)lost /(float)(top-i);

   printf("the lost rate is %f \n ", rate);

   return rate;
}

int createNonblockingUDP()
{
  int sockfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
  if (sockfd < 0)
  {
    LOG_SYSFATAL << "::socket";
  }
  return sockfd;
}


/////////////////////////////// Server ///////////////////////////////

void serverReadCallback(int sockfd, muduo::Timestamp receiveTime)
{
  int64_t message[3];
  struct sockaddr peerAddr;
  bzero(&peerAddr, sizeof peerAddr);
  socklen_t addrLen = sizeof peerAddr;
  ssize_t nr = ::recvfrom(sockfd, message, sizeof message, 0, &peerAddr, &addrLen);

  char addrStr[64];
  sockets::toIpPort(addrStr, sizeof addrStr, &peerAddr);
  LOG_DEBUG << "received " << nr << " bytes from " << addrStr;

  if (nr < 0)
  {
    LOG_SYSERR << "::recvfrom";
  }
  else if (implicit_cast<size_t>(nr) == frameLen)
  {
    if(!(rand()%100 < sim_lost)) // simulate the packet lost
    { 
       message[2] = receiveTime.microSecondsSinceEpoch();
       ssize_t nw = ::sendto(sockfd, message, sizeof message, 0, &peerAddr, addrLen);
       if (nw < 0)
       {
         LOG_SYSERR << "::sendto";
       }
       else if (implicit_cast<size_t>(nw) != frameLen)
       {
         LOG_ERROR << "Expect " << frameLen << " bytes, wrote " << nw << " bytes.";
       }
    }
  }
  else
  {
    LOG_ERROR << "Expect " << frameLen << " bytes, received " << nr << " bytes.";
  }
}

void runServer(uint16_t port)
{
  Socket sock(createNonblockingUDP());
  sock.bindAddress(InetAddress(port));
  EventLoop loop;
  Channel channel(&loop, sock.fd());
  channel.setReadCallback(boost::bind(&serverReadCallback, sock.fd(), _1));
  channel.enableReading();
  loop.loop();
}

/////////////////////////////// Client ///////////////////////////////

void clientReadCallback(int sockfd, muduo::Timestamp receiveTime)
{
  int64_t message[3];
  ssize_t nr = sockets::read(sockfd, message, sizeof message);

  if (nr < 0)
  {
    LOG_SYSERR << "::read";
  }
  else if (implicit_cast<size_t>(nr) == frameLen)
  {
    int64_t seq = message[0];
    printf("get the response seq num is: %ld \n", seq);

    if (seq < res_seq.get()) //out of order packatet
    {
	    // treat it as lost
	 printf(" think the %ld packet is lost\n", seq); 
    }
    else
    {
         reps_state[seq] = 1; //mark it as recieved
    }
    
    res_seq.getAndSet(seq); // update current response index. 
	    
    int64_t send = message[1];
    int64_t their = message[2];
    int64_t back = receiveTime.microSecondsSinceEpoch();
    int64_t mine = (back+send)/2;
    LOG_INFO << "round trip " << back - send
             << " clock error " << their - mine;
  }
  else
  {
    LOG_ERROR << "Expect " << frameLen << " bytes, received " << nr << " bytes.";
  }
}

void sendMyTime(int sockfd)
{
  int64_t message[3] = {0, 0, 0 };
  message[0] = req_seq.addAndGet(1);
  message[1] = Timestamp::now().microSecondsSinceEpoch();
  ssize_t nw = sockets::write(sockfd, message, sizeof message);
  if (nw < 0)
  {
    LOG_SYSERR << "::write";
  }
  else if (implicit_cast<size_t>(nw) != frameLen)
  {
    LOG_ERROR << "Expect " << frameLen << " bytes, wrote " << nw << " bytes.";
  }
}

void runClient(const char* ip, uint16_t port)
{
  Socket sock(createNonblockingUDP());
  InetAddress serverAddr(ip, port);
  int ret = sockets::connect(sock.fd(), serverAddr.getSockAddr());
  if (ret < 0)
  {
    LOG_SYSFATAL << "::connect";
  }
  EventLoop loop;
  Channel channel(&loop, sock.fd());
  channel.setReadCallback(boost::bind(&clientReadCallback, sock.fd(), _1));
  channel.enableReading();
  loop.runEvery(0.2, boost::bind(sendMyTime, sock.fd()));
  loop.runEvery(10, boost::bind(cal_lost_rate));
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

