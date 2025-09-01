#include <stdio.h>
#include "csapp.h"
#include <stdbool.h>
#include <string.h>
#include <strings.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_HEADERS 100

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static void handle_client(int fd);
static void read_request_headers(rio_t* rio, char header[][MAXLINE], int* num_headers);
void clienterror(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg);
static int parse_uri(const char* uri, char* host, char* port, char* path);
static int forward_request_to_origin(
  int clientfd,
  const char* host, const char* port, const char* path,
  char header[][MAXLINE], int num_headers);

//######################################################################################################################################################
int main(int argc, char** argv){
  
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if(argc != 2){
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  while(1){
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
    handle_client(connfd);
    Close(connfd);
  }
}

//######################################################################################################################################################
static void handle_client(int fd){

  int num_headers = 0;
  char method[MAXLINE], buf[MAXLINE], uri[MAXLINE], version[MAXLINE], header[MAX_HEADERS][MAXLINE];
  char host[MAXLINE], port[16], path[MAXLINE];
  rio_t rio;
  // rio 초기화

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  //요청 라인 읽기: METHOD URI VERSION

  //GET만 허용(아니면 간단한 에러 응답 후 리턴)
  if(strcasecmp(method, "GET")){
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  //요청 헤더를 빈 줄(CRLF, \r\n)까지 한 줄씩 읽어 벡터/배열에 보관
  read_request_headers(&rio, header, &num_headers);

  //이후 단계(서법 연결, 요청 재작성, 포워딩, 응답릴레이)에 넘길 재료 준비
  
  int host_idx = -1;
  for(int i = 0; i < num_headers; i++){
    if(!strncasecmp(header[i], "Host:", 5)) {host_idx = i; break;}
  }

  if(uri[0] == '/') {
    if(host_idx < 0) {clienterror(fd, uri, "400", "Bad Request", "Host header missing"); return;}
    char hostline[MAXLINE];
    strncpy(hostline, header[host_idx] + 5, sizeof(hostline) - 1);
    hostline[sizeof(hostline) - 1] = '\0';

    char* h = hostline;
    while(*h == ' ' || *h == '\t') h++;

    char* colon = strchr(h, ':');
    if(colon) {*colon = '\0'; strncpy(host, h, MAXLINE-1); host[MAXLINE-1] = '\0'; strncpy(port, colon+1, 15); port[15] = '\0';}
    else{strncpy(host, h, MAXLINE-1); host[MAXLINE - 1] = '\0'; strcpy(port, "80");}

    strncpy(path, uri, MAXLINE - 1);
    path[MAXLINE - 1] = '\0';
  }
  else{
    if(parse_uri(uri, host, port, path) < 0){
      clienterror(fd, uri, "400", "Bad Request", "Proxy couldn't parse URI");
      return;
    }
  }



  if(forward_request_to_origin(fd, host, port, path, header, num_headers) < 0){
    clienterror(fd, host, "502", "Bad Gateway", "Proxy failed to connect to origin");
    return;
  }
  
}

//######################################################################################################################################################
static void read_request_headers(rio_t* rio, char header[][MAXLINE], int* num_headers){
  //rio_readlineb로 한 줄씩 읽고, \r\n 만나면 종료
  //각 줄 끝엔 CRLF가 포함될 수 있으니 필요하면 정리(개행 제거)
  char buf[MAXLINE];

  while(Rio_readlineb(rio, buf, MAXLINE) > 0){
    //rio 버퍼에서 한 줄을 읽어 buf에 저장
    if(!strcmp(buf, "\r\n")) break;
    if(*num_headers < MAX_HEADERS){
      strncpy(header[*num_headers], buf, MAXLINE - 1); 
      //현재 읽은 한 줄(buf)을 header[num_headers]에 복사
      header[*num_headers][MAXLINE - 1] = '\0';
      (*num_headers)++;
      printf("%s", buf); //디버깅을 위해 읽은 헤더줄을 출력
    }
  }
  return;
  
}

//######################################################################################################################################################
void clienterror(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg){
  /*
  * fd: 클라이언트와 연결된 소켓 디스크립터
  * cause: 에러 원인(예: 파일 이름)
  * errnum: HTTP 상태 코드 문자열 (예: "404")
  * shortmsg: 상태 메시지 요약 (예: "Not Found")
  * longmsg: 상테 메시지 (예: "Tiny couldn't find this file")
  * 이 함수는 fd로 연결된 클라이언틍게 에러 응답을 만들어 보낸다.
  */
  
  char buf[MAXLINE], body[MAXLINE];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  //에러 응답 본문 생성
  //body라는 문자열 버퍼에 간단한 HTML 페이지를 만든다
  //페이지 배경을 흰색으로 지정하고, 에러 코드와 메시지, 에러 원인(cause), 그리고 서버 서명을 출력한다

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  //첫 줄(Response line): "HTTP/1.0 404 Not Found\r\n"
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  //헤더 1: 콘텐츠 타입을 HTML로 지정
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  //헤더 2: 본문 길이를 지정(Content-length)
  //마지막 \r\n으로 헤더 종료
  Rio_writen(fd, body, strlen(body));
  //본문(에러 페이지) 전송
  //앞에서 만든 HTML 문자열 body를 클라이언트에 전송한다
}

//######################################################################################################################################################
static int parse_uri(const char* uri, char* host, char* port, char* path){
  //원 서버와 통신하기 위한 주소 정보 추출
  const char *p = uri;
  const char* host_begin, *host_end, *port_begin, *path_begin;

  const char* prefix = "http://";
  size_t plen = strlen(prefix);
  if(strncasecmp(p, prefix, plen) == 0){
    p += plen;
  }

  host_begin = p;

  while(*p && *p != ':' && *p != '/')
    p++;
  
  host_end = p;

  if(host_end == host_begin) return -1;

  size_t hlen = host_end - host_begin;
  strncpy(host, host_begin, hlen);
  host[hlen] = '\0';

  if(*p == ':'){
    p++;
    port_begin = p;
    while(*p && *p != '/')
      p++;
    if(p == port_begin) return -1;
    size_t tlen = p - port_begin;
    strncpy(port, port_begin, tlen);
    port[tlen] = '\0';
  }
  else{
    strcpy(port, "80");
  }

  if(*p == '/'){
    path_begin = p;
    strncpy(path, path_begin, MAXLINE - 1);
    path[MAXLINE - 1] = '\0';
  }
  else{
    strcpy(path, "/");
  }
  return 0;
}

//######################################################################################################################################################
static int forward_request_to_origin(
  int clientfd,
  const char* host, const char* port, const char* path,
  char header[][MAXLINE], int num_headers)
  {
    int serverfd = Open_clientfd(host, port);
    if(serverfd < 0) return -1;

    rio_t s_rio;
    Rio_readinitb(&s_rio, serverfd);

    char out[MAXBUF];
    int n = snprintf(out, sizeof(out), "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, out, n);

    bool have_host = false;
    for(int i = 0; i < num_headers; i++){
      if(!strncasecmp(header[i], "Host:", 5)){
        have_host = true;
        break;
      }
    }

    if(!have_host){
      if(*port && strcmp(port, "80")){
        n = snprintf(out, sizeof(out), "Host: %s:%s\r\n", host, port);
      }
      else{
        n = snprintf(out, sizeof(out), "Host: %s\r\n", host);
      }
      Rio_writen(serverfd, out, n);
    }

    n = snprintf(out, sizeof(out), "%s", user_agent_hdr);
    Rio_writen(serverfd, out, n);
    n = snprintf(out, sizeof(out), "Connection: close\r\n");
    Rio_writen(serverfd, out, n);
    n = snprintf(out, sizeof(out), "Proxy-Connection: close\r\n");
    Rio_writen(serverfd, out, n);

    for(int i = 0; i < num_headers; i++){
      if (!strncasecmp(header[i], "Connection:", 11)) continue;
      if (!strncasecmp(header[i], "Proxy-Connection:", 17)) continue;
      if (!strncasecmp(header[i], "Keep-Alive:", 11)) continue;
      if (!strncasecmp(header[i], "Transfer-Encoding:", 18)) continue;
      if (!strncasecmp(header[i], "TE:", 3)) continue;
      if (!strncasecmp(header[i], "Trailer:", 8)) continue;
      if (!strncasecmp(header[i], "Upgrade:", 8)) continue;
      if (!strncasecmp(header[i], "User-Agent:", 11)) continue;
      Rio_writen(serverfd, header[i], strlen(header[i]));
    }
    Rio_writen(serverfd, "\r\n", 2);

    char buf[MAXBUF];
    ssize_t m;
    while((m = Rio_readnb(&s_rio, buf, sizeof(buf))) > 0)
      Rio_writen(clientfd, buf, m);
    
    Close(serverfd);
    return 0;
  }

//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################
