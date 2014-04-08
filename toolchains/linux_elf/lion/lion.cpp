/* Sample UDP server */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include <sys/time.h>


void print_hosts();
void udp_client();
void udp_server();
void tcp_client();
void tcp_server();

// #include "linpack.c"		
#include "whetstone.c"
// #include "stream.c"

#define _printf(...) fprintf(stderr, __VA_ARGS__);

static struct tms my_tms;
// static struct timeval tv;
typedef long long my_time_t;

void print_timeofday()
{
  gettimeofday(&tv, NULL);
  _printf("sec %d, ms %d\n", (int)tv.tv_sec, (int)tv.tv_usec);
}

// static my_time_t _get_microsec()
// {
//   my_time_t t = 0;
//   gettimeofday(&tv, NULL);
//   t = tv.tv_sec;
//   t = t * 1000000;
//   t += tv.tv_usec;
//   return t;
// }

// This funciton is used to test checkpointing
void timer()
{
  int seconds = 0;
  while(1){
    _printf("Uptime in seconds: %d \n", seconds);
    sleep(1);
    seconds++;
  }
}

int main(int argc, char**argv)
{
  
  clock_t c1, c2; double ts; time_t t0;
  my_time_t t1, t2;
  c1 = times(&my_tms);
  t1 = get_microsec();

  print_hosts();
  t0 = time(NULL);
  print_timeofday();
  _printf("hello world!\n");

  timer();
  // udp_server();
  // udp_client();
  // tcp_client();
  // tcp_server();
  // linpack_main();
  // stream_main();
  // whetstone_main();

  _printf("Bye\n");
  _printf("time: %ld s\n", time(NULL)-t0);
  c2 = times(&my_tms);
  t2 = get_microsec();
  ts = (((double)(c2 - c1)) / (double)CLOCKS_PER_SEC ); // in second
  _printf("Total time is: %d clock, %f s, %f ms\n", (int)(c2-c1), ts, ts*1000.0);
  _printf("Total time in microsec: %lld\n", t2-t1);
  print_timeofday();
  exit(0);
  return 0;
}

void test_clock()
{

}

void print_hosts()
{
  FILE *fd;
  int count;
  char buf[80];
  fd = fopen("/etc/hosts", "r");
  if(fd == NULL) {
    _printf("Cannot open /etc/hosts \n");
    return;
  }
  _printf("print /etc/hosts\n");
  while((count = fread(buf, 1, sizeof(buf), fd))) {
    fwrite(buf, 1, count, stderr);
  }
  _printf("DONE\n");
}

void udp_client()
{
  char srv_ip[16];
  int sockfd, n, i;
  struct sockaddr_in servaddr;//,cliaddr;
  char sendline[1000];
  char recvline[1000];
  clock_t c1, c2; double ts; my_time_t mt1, mt2;
  time_t t1, t2;
  int count = 1000;

  n = 0, recvline[0] = 0;
  strcpy(srv_ip, "10.1.0.1");
  // strcpy(sendline, "Hello world! Count %d\n"); 
  sockfd=socket(AF_INET,SOCK_DGRAM,0);
  if(sockfd < 0){
    _printf("Cannot open socket.\n");
    return;
  }

  bzero(&servaddr,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr=inet_addr(srv_ip);
  servaddr.sin_port=htons(32000);

  c1 = clock();
  t1 = time(0);
  mt1 = get_microsec();
  for(i = 0; i < count; i++) {
    sprintf(sendline, "#%d# Hello world!\n", i); 
    // disable printf while benchmarking
    // _printf("Sending message: %s\n", sendline);
    sendto(sockfd,sendline,strlen(sendline),0,
	   (struct sockaddr *)&servaddr,sizeof(servaddr));

  }
  c2 = clock();
  t2 = time(0);
  mt2 = get_microsec();
  ts = (((double)(c2 - c1)) / (double)CLOCKS_PER_SEC ); // in second
  _printf("Send %d packets in %f s, or %f ms\n", count, ts, ts*1000.0);
  _printf("Send %d packets in %d s\n", count, (int)(t2-t1));
  _printf("Send %d packets in %lld microsec, or %f microsec/packet \n", count, (mt2-mt1), (double)(mt2-mt1)/(double)count );


  // _printf("Receiving message\n");
  // n=recvfrom(sockfd,recvline,1000,0,NULL,NULL);
  // recvline[n]=0;
  // _printf("Received the following:\n");
  // _printf("%s", recvline);
}

void tcp_client()
{
  char srv_ip[16];
  int sockfd, n;
  struct sockaddr_in servaddr;//,cliaddr;
  char sendline[1000];
  char recvline[1000];

  n = 0, recvline[0] = 0;
  strcpy(srv_ip, "10.1.0.1");
  // strcpy(sendline, "Hello world! Count %d\n"); 
  sockfd=socket(AF_INET,SOCK_STREAM,0);
  if(sockfd < 0){
    _printf("Cannot open socket.\n");
    return;
  }

  bzero(&servaddr,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr=inet_addr(srv_ip);
  servaddr.sin_port=htons(32000);

  if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
    _printf("Cannot connect to server.");
    return;
  }

  sprintf(sendline, "#%d# Hello world!\n", 0); 
  _printf("Sending message: %s\n", sendline);
  n = write(sockfd, sendline, strlen(sendline));
  if (n < 0){
    _printf("Cannot write to the stream.");
    return;
  }

  _printf("Reading message\n");
  n = read(sockfd, recvline, sizeof(recvline));
  if (n < 0) {
    _printf("Cannot read from the stream.");
    return;
  }
  _printf("Received %d bytes: %s\n", n, recvline);
  close(sockfd);
  return;
}

void udp_server()
{
  int sockfd,n;
  struct sockaddr_in servaddr,cliaddr;
  socklen_t len;
  char mesg[1000];

  sockfd=socket(AF_INET,SOCK_DGRAM,0);

  bzero(&servaddr,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
  servaddr.sin_port=htons(32000);
  bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));

  for (;;)
    {
      len = sizeof(cliaddr);
      n = recvfrom(sockfd,mesg,1000,0,(struct sockaddr *)&cliaddr,&len);
      sendto(sockfd,mesg,n,0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
      _printf("-------------------------------------------------------\n");
      mesg[n] = 0;
      _printf("Received the following:\n");
      _printf("%s",mesg);
      _printf("-------------------------------------------------------\n");
    }
}

#define DieWithError(msg) { _printf("%s\n",msg); return; }
#define RCVBUFSIZE 32   /* Size of receive buffer */

void HandleTCPClient(int clntSocket)
{
  char echoBuffer[RCVBUFSIZE];        /* Buffer for echo string */
  int recvMsgSize;                    /* Size of received message */

  /* Receive message from client */
  if ((recvMsgSize = recv(clntSocket, echoBuffer, RCVBUFSIZE, 0)) < 0)
    DieWithError("recv() failed");

  /* Send received string and receive again until end of transmission */
  while (recvMsgSize > 0)      /* zero indicates end of transmission */
    {
      /* Echo message back to client */
      if (send(clntSocket, echoBuffer, recvMsgSize, 0) != recvMsgSize)
	DieWithError("send() failed");

      /* See if there is more data to receive */
      if ((recvMsgSize = recv(clntSocket, echoBuffer, RCVBUFSIZE, 0)) < 0)
	DieWithError("recv() failed");
    }

  close(clntSocket);    /* Close client socket */
}

void tcp_server()
{
  int servSock;                    /* Socket descriptor for server */
  int clntSock;                    /* Socket descriptor for client */
  struct sockaddr_in echoServAddr; /* Local address */
  struct sockaddr_in echoClntAddr; /* Client address */
  unsigned short echoServPort;     /* Server port */
  unsigned int clntLen;            /* Length of client address data structure */
  int numClients;                  /* Number of clients the server will accept */
  int i;
  

  echoServPort = 32000;
  numClients = 1;

  if ((servSock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    DieWithError("socket() failed");

  /* Construct local address structure */
  memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
  echoServAddr.sin_family = AF_INET;                /* Internet address family */
  echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
  echoServAddr.sin_port = htons(echoServPort);      /* Local port */

  /* Bind to the local address */
  if (bind(servSock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
    DieWithError("bind() failed");

  /* 
   * listen: make this socket ready to accept connection requests 
   */
  if (listen(servSock, 1) < 0) /* allow 5 requests to queue up */ 
    DieWithError("ERROR on listen");

  for (i = 0; i < numClients; i++) /* Run forever */
  {
    /* Set the size of the in-out parameter */
    clntLen = sizeof(echoClntAddr);


    /* Wait for a client to connect */
    if ((clntSock = accept(servSock, (struct sockaddr *) &echoClntAddr, &clntLen)) < 0)
      DieWithError("accept() failed");

    /* clntSock is connected to a client! */

    printf("Handling client %s\n", inet_ntoa(echoClntAddr.sin_addr));

    HandleTCPClient(clntSock);
  }

}
