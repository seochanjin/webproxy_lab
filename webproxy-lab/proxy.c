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
/*
* 명령행에서 포트를 받고 그 포트로 리스닝 소켓을 연다.
* 클라이언트 연결을 accept해서 한 연결씩 handle client로 처리한다.
* 처리가 끝난 소켓을 닫는다.
*/
int main(int argc, char** argv){
  
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if(argc != 2){
    //포트가 없으면 즉시 종료
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  //Open_listenfd는 socket -> bind -> listen까지 해결해주는 헬퍼(에러 처리 포함)
  //여기서 만들어진 소켓은 수동 대기(listening) 상태

  while(1){
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
    //Accept로 새 TCP 연결을 수락하여 연결 전용 소켓(connfd)를 획득
    handle_client(connfd);
    //전용 소켓을 handle_client에 넘겨 요청 파싱/원서버로 포워딩/응답 릴레이를 수행
    Close(connfd);
    //끝나면 반드시 Close(connfd)로 정리(리소스 누수 방지)
    //리스닝 소켓(listenfd)은 루프 동안 계속 열려있음
  }
}

//######################################################################################################################################################
/*
* method, uri, version: 요청라인의 3요소 저장용
* header[MAX_HEADERS][MAXLINE]: 클라이언트가 보낸 요청 헤더들을 한 줄씩 보관
* host, port, path: 원서버(오리진)에 접속할 때 필요할 주소 3종
*/
static void handle_client(int fd){

  int num_headers = 0;
  char method[MAXLINE], buf[MAXLINE], uri[MAXLINE], version[MAXLINE], header[MAX_HEADERS][MAXLINE];
  char host[MAXLINE], port[16], path[MAXLINE];
  rio_t rio;
  // rio 초기화

  Rio_readinitb(&rio, fd);
  //Rio_readinitb(&rio, fd): fd(클라이언트 소켓)를 RIO 버퍼와 연결해서 줄 단위 읽기 준비

  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  /*
  * 예: GET http://example.com/index.html HTTP/1.1
  * 여기서 method/uri/version을 분리해 저장
  * 디버깅용으로 첫 줄 출력함
  */

  //GET만 허용(아니면 간단한 에러 응답 후 리턴)
  if(strcasecmp(method, "GET")){
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  // 전부 똑같으면 0을 출력하기 때문에 이렇게 진행

  //요청 헤더를 빈 줄(CRLF, \r\n)까지 한 줄씩 읽어 벡터/배열에 보관
  read_request_headers(&rio, header, &num_headers);
  // 빈 줄(\r\n) 나올 때까지 한 줄씩 읽어 header[i]에 저장
  // 나중에 원서버로 재전달(필요한 것만) 하기 위해 쌓아둔다.


  // 목적지(host/port)와 경로 결정
  // 프록시로 오는 요청 URI는 두 형태가 올 수 있다.
    //absolute-form: http://host:port/path(브라우저가 프록시로 말할 때 자주 사용)
    //origin-form: /path(curl이나 일부 상황에서 사용) -> 이떄는 반드시 Host: 헤더로 호스트를 알아내야 한다.
  // 헤더에서 Host: 가 있는지 찾기
  int host_idx = -1;
  for(int i = 0; i < num_headers; i++){
    if(!strncasecmp(header[i], "Host:", 5)) {host_idx = i; break;}
    //strncasecmp는 대소문자 구분없이 header[i] 앞의 5글자와 "Host"를 비교하며 같을 경우 0을 반환.
    // 클라이언트가 프록시에 보낼 때 요청라인이 /path 형태(origin-form)이면 원 서버 호스트는 반드시 Host: 헤더에서 얻어야 한다.(HTTP/1.1 규칙)
    // 전체 헤더 배열에서 Host: 라인을 찾아 위치(host_idx)를 기록한다.
  }
  

  if(uri[0] == '/') {
    if(host_idx < 0) {clienterror(fd, uri, "400", "Bad Request", "Host header missing"); return;}
    char hostline[MAXLINE];
    strncpy(hostline, header[host_idx] + 5, sizeof(hostline) - 1);
    hostline[sizeof(hostline) - 1] = '\0';
    // header[host_idx] + 5는 "Host:" 딱 5글자를 건너뛴 다음을 가리킴(보통 공백이 따라옴)

    char* h = hostline;
    while(*h == ' ' || *h == '\t') h++;
    // 공백 제거
    // 이제 h는 "example.com" 또는 "example.com:8080"의 시작을 가리킴

    char* colon = strchr(h, ':');
    if(colon){
      *colon = '\0'; // host 문자열을 여기서 끊는다.
      strncpy(host, h, MAXLINE-1); //host = "example.com"
      host[MAXLINE-1] = '\0'; 
      strncpy(port, colon+1, 15); // port = "8080"
      port[15] = '\0';
    }
    else{
      strncpy(host, h, MAXLINE-1); // host = "example.com"
      host[MAXLINE - 1] = '\0'; 
      strcpy(port, "80"); // 포트가 없으면 기본 80
    }

    strncpy(path, uri, MAXLINE - 1);
    path[MAXLINE - 1] = '\0';
  }
  // uri가 /... 로 시작하면 origin-form이다. 예: GET /index.html HTTP/1.1
  // 이 경우 호스트 정보가 요청라인에 없으므로 반드시 Host:헤더가 있어야 하고 없으면 400으로 거절
  // path는 그냥 uri를 그대로 사용

  else{
    if(parse_uri(uri, host, port, path) < 0){
      clienterror(fd, uri, "400", "Bad Request", "Proxy couldn't parse URI");
      return;
    }
  }
  // uri가 http://example.com:8080/index.html 같은 absolute-form이면
  // parse_uri가 알아서 host="example.com", port = "8080"(없으면 "80"), path = "/index.html"로 분해
  // 여기서는 Host: 헤더가 없더라도 요청라인에 이미 호스트가 있으므로 포워딩에 필요한 정보가 채워짐
  // (그래도 나중에 원 서버로 보낼 때는 Host: 헤더를 넣어줘야 하니까, 후단에서 have_host 검사하고 추가함)

  // 원 서버로 요청 포워딩 + 응답 릴레이
  if(forward_request_to_origin(fd, host, port, path, header, num_headers) < 0){
    clienterror(fd, host, "502", "Bad Gateway", "Proxy failed to connect to origin");
    return;
  }
  // 여기서 실제로 원 서버에 TCP 연결 -> HTTP 요청 재작성/전송 -> 응답 받아서 클라이언트로 그대로 흘려보내기를 수행
  // 내부에서:
    // 요청라인을 GET <path> HTTP/1.0\r\n으로 표준화(원 서버와는 짧은 연결로 간단하게)
    // 필수/권장 헤더 채움: Host, 고정된 User-Agent, Connection: close, Proxy-Connection: close
    // 클라이언트가 보낸 헤더 중 hop-by-hop 헤더(Connection 류, TE, Upgrade 등)는 제거하고 나머지는 전달
    // 헤더 끝 \r\n 추가 후 바디(있다면) 처리
    // 원 서버 응답을 읽어 클라이언트 소켓으로 그대로 write
    // 응답은 읽어서 그대로 클라이언트 fd로 릴레이
  
}

//######################################################################################################################################################
static void read_request_headers(rio_t* rio, char header[][MAXLINE], int* num_headers){
  // rio: RIO 구조체 포인터, 클라이언트 소켓과 연결된 버퍼.
  // header: 문자열 배열. 요청 헤더를 한 줄 씩 저장할 공간
  // num_headers: 지금까지 읽은 헤더 줄 수를 기록하는 변수(포인터)
  // rio_readlineb로 한 줄씩 읽고, \r\n 만나면 종료
  // 각 줄 끝엔 CRLF가 포함될 수 있으니 필요하면 정리(개행 제거)
  char buf[MAXLINE];

  while(Rio_readlineb(rio, buf, MAXLINE) > 0){
    // Rio_readlineb: 소켓 스트림에서 줄 단위(최대 MAXLINE까지) 읽기.
    // > 0: 한 줄 이상 읽어왔으면 계속 반복
    // 0 이면 EOF, 즉 클라이언트가 연결을 끊었음
    if(!strcmp(buf, "\r\n")) break;
    // HTTP 헤더는 빈 줄(\r\n)이 나오면 끝난다
    // 루프 종료
    if(*num_headers < MAX_HEADERS){
      // *num_headers < MAX_HEADERS: 배열 크기 한도를 넘지 않도록 확인
      strncpy(header[*num_headers], buf, MAXLINE - 1); 
      // 현재 읽은 한 줄(buf)을 header[num_headers]에 복사
      // MAXLINE - 1 까지만 복사해서 버퍼 오버플로우 방지
      header[*num_headers][MAXLINE - 1] = '\0';
      // 마지막에 \0을 강제로 넣어 문자열 종료 보장
      (*num_headers)++;
      // 헤더 줄 하나를 저장했으니 num_headers++
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

  // "http://" 접두사 제거
  const char* prefix = "http://";
  size_t plen = strlen(prefix);
  if(strncasecmp(p, prefix, plen) == 0){
    p += plen;
  }
  // URI가 "http://"로 시작하면 그 부분을 건너뛴다.
  // "http://example.com:8080/index.html" -> p는 "examle.com:8080/index.html"을 가리킨다.

  host_begin = p;

  while(*p && *p != ':' && *p != '/')
    p++;
  
  host_end = p;
  // p가 ':'(포트 시작) 또는 '/' (path 시작)를 만날 때까지 이동
  // host_gegin = "example.com", host_end = ':'

  if(host_end == host_begin) return -1;

  size_t hlen = host_end - host_begin;
  strncpy(host, host_begin, hlen);
  host[hlen] = '\0';
  //host = "example.com" 저장

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
  // : 있으면 그 뒤 숫자를 포트 번호로 복사 (port = "8080")
  // 없으면 기본 포트 "80"을 넣음

  if(*p == '/'){
    path_begin = p;
    strncpy(path, path_begin, MAXLINE - 1);
    path[MAXLINE - 1] = '\0';
  }
  else{
    strcpy(path, "/");
  }
  // / 있으면 거기서부터 끝까지 저장 (path = "/index.html")
  // / 없으면 기본값 "/"

  return 0;
}

//######################################################################################################################################################
static int forward_request_to_origin(
  int clientfd,
  const char* host, const char* port, const char* path,
  char header[][MAXLINE], int num_headers)
  {
    // 원서버에 TCP 연결
    int serverfd = Open_clientfd(host, port);
    if(serverfd < 0) return -1;
    rio_t s_rio;
    Rio_readinitb(&s_rio, serverfd);
    // host: port로 outbound 소켓을 열어 원서버에 접속
    // 이 소켓을 RIO 버퍼에 묶어서 이후 응답을 편하게 읽도록 준비

    // 원서버로 보낼 요청라인 작성
    char out[MAXBUF];
    int n = snprintf(out, sizeof(out), "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, out, n);
    // 프록시는 항상 원서버로 HTTP/1.0을 사용해 단순화(keep-alive, chunked 등 회피)
    // GET <path> HTTP/1.0\r\n처럼 절대 URI가 아닌 경로(path)로 보낸다.(프록시가 이미 Host로 목적지 알려줄 것)

    // 필수/표준화 헤더 구성
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
    // host 헤더 보장
    // HTTP/1.1 클라이언트가 보냈다면 보통 Host가 있음
    // 만약 없으면(HTTP/1.0 클라일 수도) 프록시가 필수 Host 헤더를 추가해 원서버가 가상호스트를 식별하도록 함


    n = snprintf(out, sizeof(out), "%s", user_agent_hdr);
    Rio_writen(serverfd, out, n);
    n = snprintf(out, sizeof(out), "Connection: close\r\n");
    Rio_writen(serverfd, out, n);
    n = snprintf(out, sizeof(out), "Proxy-Connection: close\r\n");
    Rio_writen(serverfd, out, n);
    // 표준화 강제 헤더 3종
    // User-Agent를 과제에서 주어진 표준 문자열로 강제
    // Connection: close, Proxy-Connection: close로 양쪽 모두 연결을 요청-응답 후 끊도록 만들어
      // 응답 경계 판단을 간단히(EOF가 응답 끝) 하고
      // 동시연결 누수를 막는다.

    // 나머지 헤더 전달("hop-by-hop" 및 중복/문제 헤더 제거)
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
    // hop-by-hop 헤더(Connection, Proxy-Connection, Keep-Alive, TE, Trailer, Upgrade)는 프록시 구간을 넘기면 안 됨 -> 드롭
    // Transfer-Encoding도 드롭(HTTP/1.0로 단순화하려고 chunked 등을 피함)
    // User-Agent는 이미 위에서 우리가 보낸 값이 있으니 중복 방지로 드롭
    // 나머지는 그대로 원서버로 전달
    // 나머지 \r\n은 헤더 종료 빈 줄

    char buf[MAXBUF];
    ssize_t m;
    while((m = Rio_readnb(&s_rio, buf, sizeof(buf))) > 0)
      Rio_writen(clientfd, buf, m);
    // 원서버 응답을 클라이언트로 그대로 중계
    // 원서버 응답(헤더+바디)을 그대로 스트리밍 복사
    // 위에서 Connection: close를 강제했기 때문에 원서버가 응답을 보내고 연결을 닫으면(EOF) 루프가 종료
    // -> 응답 끝을 쉽게 인지한다.
    
    Close(serverfd);
    return 0;
    // 원서버 소켓 닫고 종료(클라이언트 소켓은 바깥 handle_client에서 닫음)
  }

//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################


//######################################################################################################################################################
