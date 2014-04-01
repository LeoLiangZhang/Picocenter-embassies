/* Sample UDP server */

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

void print_hosts();
void client();
void server();
void tcp_client();

int lion_main(int argc, char**argv)
{
  print_hosts();
  fprintf(stderr, "hello world!\n");
  // server();
  // client();
  tcp_client();
  fprintf(stderr, "Bye\n");
  return 0;
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
  strcpy(srv_ip, "10.1.0.1");
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
