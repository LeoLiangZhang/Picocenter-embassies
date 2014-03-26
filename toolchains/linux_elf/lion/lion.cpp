/* Sample UDP server */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>


void print_hosts();
void client();
void server();
void tcp_client();
void tcp_server();

int main(int argc, char**argv)
{
  print_hosts();
  fprintf(stderr, "hello world!\n");
  server();
  // client();
  // tcp_client();
  // tcp_server();
  fprintf(stderr, "Bye\n");
}

void print_hosts()
{
  FILE *fd;
  int count;
  char buf[80];
  fd = fopen("/etc/hosts", "r");
  if(fd == NULL) {
    fprintf(stderr, "Cannot open /etc/hosts \n");
    return;
  }
  fprintf(stderr, "print /etc/hosts\n");
  while((count = fread(buf, 1, sizeof(buf), fd))) {
    fwrite(buf, 1, count, stderr);
  }
  fprintf(stderr, "DONE\n");
}

void client()
{
  char srv_ip[16];
  int sockfd, n, i;
  struct sockaddr_in servaddr;//,cliaddr;
  char sendline[1000];
  char recvline[1000];

  n = 0, recvline[0] = 0;
  strcpy(srv_ip, "10.1.0.250");
  // strcpy(sendline, "Hello world! Count %d\n"); 
  sockfd=socket(AF_INET,SOCK_DGRAM,0);
  if(sockfd < 0){
    fprintf(stderr, "Cannot open socket.\n");
    return;
  }

  bzero(&servaddr,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr=inet_addr(srv_ip);
  servaddr.sin_port=htons(32000);

  for(i = 0; i < 10; i++) {
    sprintf(sendline, "#%d# Hello world!\n", i); 
    fprintf(stderr, "Sending message: %s\n", sendline);
    sendto(sockfd,sendline,strlen(sendline),0,
	   (struct sockaddr *)&servaddr,sizeof(servaddr));

  }

  // fprintf(stderr, "Receiving message\n");
  // n=recvfrom(sockfd,recvline,1000,0,NULL,NULL);
  // recvline[n]=0;
  // fprintf(stderr, "Received the following:\n");
  // fprintf(stderr, "%s", recvline);
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
    fprintf(stderr, "Cannot open socket.\n");
    return;
  }

  bzero(&servaddr,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr=inet_addr(srv_ip);
  servaddr.sin_port=htons(32000);

  if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
    fprintf(stderr, "Cannot connect to server.");
    return;
  }

  sprintf(sendline, "#%d# Hello world!\n", 0); 
  fprintf(stderr, "Sending message: %s\n", sendline);
  n = write(sockfd, sendline, strlen(sendline));
  if (n < 0){
    fprintf(stderr, "Cannot write to the stream.");
    return;
  }

  fprintf(stderr, "Reading message\n");
  n = read(sockfd, recvline, sizeof(recvline));
  if (n < 0) {
    fprintf(stderr, "Cannot read from the stream.");
    return;
  }
  fprintf(stderr, "Received %d bytes: %s\n", n, recvline);
  close(sockfd);
  return;
}

void server()
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
      fprintf(stderr, "-------------------------------------------------------\n");
      mesg[n] = 0;
      fprintf(stderr, "Received the following:\n");
      fprintf(stderr, "%s",mesg);
      fprintf(stderr, "-------------------------------------------------------\n");
    }
}

#define DieWithError(msg) { fprintf(stderr, "%s\n",msg); return; }
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
