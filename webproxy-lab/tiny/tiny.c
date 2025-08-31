/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

//######################################################################################################################################################
int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 프로그램 실행 시 인자로 포트 번호를 받아야 한다. - 포트 번호 확인
  // 만약 포트 번호가 주어지지 않으면 사용법을 출력하고 종료

  listenfd = Open_listenfd(argv[1]);
  //지정된 포트에서 클라이언트의 연결 요청을 기다리는 소켓(리스닝 소켓)을 만든다 - 서버 소켓 열기

  while (1) // 클라이언트 요청 처리
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    //서버는 계속해서 실행되며 클라이언트 요청이 들어올 때마다 Accept를 호출해 새로운 연결을 받아들인다.
    //connfd는 연결된 소켓(Connected Descriptor)으로 클라이언트와 데이터를 주고받는 데 사용된다.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    //접속한 클라이언트의 호스트 이름과 포트 번호를 알아내서 출력
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    //실제로 클라이언트의 요청을 처리하는 핵심 함수
    //Tiny 서버에서는 주로 HTTP 요청을 읽고 그에 맞는 정적/동적 컨텐츠를 응답하는 일을 한다.
    Close(connfd); // line:netp:tiny:close
    //요청을 처리한 후 연결을 닫는다
    //이렇게 해야 서버는 다음 클라이언트 요청을 새로 받을 수 있다.
  }
}
//######################################################################################################################################################
void doit(int fd){
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  //fd(클라이언트 소켓)를 리오 버퍼와 연결하고 첫 줄(요청 라인)을 읽는다
  //요청 라인은 "GET /path?query HTTP/1.1" 형태
  //method, uri, version에 나눠 담는다
  //디버깅용으로 요청 라인을 출력한다
  if(strcasecmp(method, "GET")){
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  //대소문자 무시 비교로 GET이 아니면 501 Not Implemented 반환 후 종료(HEAD, POST 등은 미구현) - 메소드 검사(GET만 지원)
  read_requesthdrs(&rio);
  //남은 헤더들을 읽어버리지만 별도 해석은 안함 - 요청 헤더 스킵(읽기)

  is_static = parse_uri(uri, filename, cgiargs);
  //URI 해석 -> 정적/동적 분기
  //parse_uri가 uri를 파일 경로(filename)와 CGI 인자(cgiargs)로 분리
  //정적이면 is_static = 1, 동적(CGI)이면 0
    //예) /index.html -> 정적, /cgi-bin/adder?15000&213 -> 동적(+cgiargs = '15000&213')
  if(stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
  //파일 존재/메타데이터 확인
  //파일 시스템에서 filename의 상태를 확인
  //없거나 접근 불가면 404 Not Found

  if(is_static){
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  }
  //정적 컨텐츠
  //일반 파일인지(S_ISREG)와 소유자 읽기 권한(S_IRUSR) 확인
  //조건 불만족 -> 403 Forbidden
  //OK면 serve_static이 헤더(Content-Type, Content-Length)와 파일을 보냄

  else{
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
  //동적 컨텐츠(CGI)
  //일반 파일인지와 실행 권한(S_IXUSR) 확인
  //OK면 serve_dynamic이 fork/exec로 CGI 실행:
    //환경변수(QUERY_STRING 등) 세팅
    //표준출력 <-> 소켓으로 리다이렉션
    //CGI가 직접 Content - type, Content - length 헤더와 본문을 출력
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
void read_requesthdrs(rio_t *rp){

  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  // printf("%s", buf);
  // rio_t* rp는 RIO 버퍼 구조체로 소켓 fd와 연결된 입력 스트림을 가리킨다
  //Rio_readlineb는 소켓으로부터 한 줄을 읽어 buf에 저장한다.(HTTP 요청 헤더는 줄 단위로 전송되므로 적합한 방식)
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    //각 줄을 표준 출력(stdout)에 찍어준다(Tiny 서버에서 디버깅 용도)
    //살제 동작에서는 서버가 헤더를 파싱해서 활용할 수 있지만 이 예제에서는 단순히 출력만 한다.
  }
  //헤더는 반드시 빈 줄(\r\n)로 끝난다는 규칙이 있다
  //strcmp(buf, "\r\n")로 확인하면서 빈 줄이 아닐 때 계속 읽어들이고 출력한다
  //빈 줄을 만나면 루프를 종료 -> 헤더 끝까지 읽은 것이 된다.
  return;
}

//######################################################################################################################################################
int parse_uri(char* uri, char* filename, char* cgiargs){

  char* ptr;
  
  if(!strstr(uri, "cgi-bin")){
  // 정적 콘텐츠 요청
    strcpy(cgiargs, ""); //CGI 인자는 없음
    strcpy(filename, "."); //현재 디렉토리에서 시작
    strcat(filename, uri); // 요청한 URI 붙이기

    if(uri[strlen(uri) - 1] == '/') //URI가 "/"로 끝나면 홈 페이지로 대체
      strcat(filename, "home.html");

    return 1; //정적 콘텐츠임을 알림
  }

  else{
    //동적 콘텐츠 요청 (CGI)
    ptr = index(uri, '?'); //URI 안에 '?' 존재 여부 확인
    if(ptr){
      strcpy(cgiargs, ptr+1); // '?' 뒤에 있는 문자열은 CGI 인자
      *ptr = '\0'; //'?' 위치를 문자열 끝으로 바꿔 URI와 분리
    }
    else
      strcpy(cgiargs, ""); //인자가 없으면 빈 문자열
    strcpy(filename, "."); //현재 디렉토리에서 시작
    strcat(filename, uri); //CGI 프로그램 파일 경로 완성

    return 0; //동적 콘텐츠임을 알림
  }
}

//######################################################################################################################################################
void serve_static(int fd, char* filename, int filesize){

  int srcfd;
  char* srcp, filetype[MAXLINE], buf[MAXLINE];

  get_filetype(filename, filetype);
  // 파일 확장자를 보고 text/html, image/jpeg 같은 Content-Type을 채워 넣는다(MIME 타입 결정)
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 상태줄
  // HTTP/1.0 200 OK: 성공 상태줄
  sprintf(buf, "%sServer: Tiny Web Server \r\n", buf);// 서버 식별(정보성)
  sprintf(buf, "%sConnection: close\r\n", buf); // 요청/응답 이후 연결을 끊겠다는 의미(HTTP/1.0 기본 동작과 일치)
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize); // 바디 길이를 정확히 명시(클라이언트가 언제까지 읽을지 판단)
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); //MIME 타입
  Rio_writen(fd, buf, strlen(buf)); // 응답 헤더 전송
  // 마지막 빈 줄 \r\n로 헤더 종료 -> 바로 본문(파일 내용) 전송
  printf("Response headers:\n");
  printf("%s", buf);

  srcfd = Open(filename, O_RDONLY, 0); //디스크 파일 열기
  // Open으로 읽기 전용 FD 획득
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //파일을 메모리에 매핑
  // Mmap으로 파일을 사용자 공간 메모리에 매핑
  // 이러면 별도 read 루프 없이 버퍼 복사 없이 곧장 Rio_writen으로 보낼 수 있어서 깔끔하고 빠름(큰 파일에서 특히 편함)
  Close(srcfd); // FD는 바로 닫아도 됨(매핑이 유지함)
  // 매핑이 커널이 관리하는 페이지로 이어줌
  Rio_writen(fd, srcp, filesize); //파일 내용(바디) 전송
  // 파일 내용을 소켓으로 전송
  Munmap(srcp, filesize); //매핑 해제
}

//######################################################################################################################################################
void get_filetype(char* filename, char* filetype){

  if(strstr(filename, ".html"))
    strcpy(filetype, "text/html");
    //filetype 배열에 MIME 타입 문자열을 복사
    //나중에 HTTP 응답 헤더에서 Content-tye: ... 부분을 만들 때 사용
  else if(strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if(strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if(strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else 
    strcpy(filetype, "text/plain");
}

//######################################################################################################################################################
void serve_dynamic(int fd, char* filename, char* cgiargs){

  char buf[MAXLINE], *emptylist[] = {NULL};

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  // 클라이언트에 상태줄과 Server 헤더를 먼저 보낸다.
  // 빈 줄(헤더 종료)는 여기서 보내지 않는다. 
  // 왜냐하면 CGI 프로그램이 자체적으로 Content-type, Content-length와 빈 줄을 출력해야 하기 떄문에 

  if(Fork() == 0){ // 자식 프로세스 분기
    setenv("QUERY_STRING", cgiargs, 1);
    // CGI 규약: GET 쿼리 문자열을 QUERY_STRING 환경변수로 전달
    // 예: /cgi-bin/adder?15000&213 -> QUERY_STRING = 15000&213
    Dup2(fd, STDOUT_FILENO);
    //자식의 표준출력(1번 FD)을 클라이언트 소켓 fd로 리다이렉션
    //이후 CGI가 printf/write(1,...)로 쓰는 모든 출력이 직접 클라이언트로 전송된다.
    Execve(filename, emptylist, environ);
    //자식 프로세스를 CGI 실행파일로 교체
    //emptylist는 argv가 비어있는 배열({NULL})이다.
      //많은 프로그램은 argv[0]가 NULL이 아닌 값이라고 가정한다
  }
  Wait(NULL);
  //부모가 자식 종료를 기다려 좀비 프로세스를 방지
  //더 안전하게는 해당 자식 PID로 waitpid(pid, NULL, 0)를 쓰는 걸 권장(다중 자식 상황 대비)
}