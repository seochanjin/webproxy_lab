#include "csapp.h"

int main(int argc, char** argv){
	int clientfd;
	char* host;
	char* port;
	char buf[MAXLINE];
	rio_t rio;
	
	// 인자 확인
	if(argc != 3){
		fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
		//프로그램 실행 시 호스트 이름(Host)과 포트 번호(port) 두 개의 인자를 반드시 받아야 한다.
		exit(0);
	}	

	//argv[0]은 실행파일 이름, argv[1]은 host, argv[2]는 port
	host = argv[1];
	port = argv[2];
	
	clientfd = Open_clientfd(host, port);
	//Open_clientfd(host, port)는 서버 소켓에 연결을 맺어주는 함수
	//성공하면 clientfd라는 파일 디스크립터를 둘려준다. 이 fd는 일반 파일처럼 read/write가 가능한 네트워크 소켓

	Rio_readinitb(&rio, clientfd);
	//Rio_readinitb는 RIO(Robust I/O)라는 안전한 입출력 패키지의 버퍼를 초기화한다.
	//rio 구조체는 소켓 clientfd와 연결되어 네트워크 데이터를 버퍼링해서 안정적으로 읽을 수 있게 된다.
	
	while(Fgets(buf, MAXLINE, stdin) != NULL){
		//Fgets -> 표준 입력(stdin)에서 한 줄 읽음(사용자가 키보드에 입력한 텍스트)
		Rio_writen(clientfd, buf, strlen(buf));
		//Rio_writen -> 읽은 텍스트를 서버로 보냄
		Rio_readlineb(&rio, buf, MAXLINE);
		//Rio_readlineb -> 서버가 돌려보낸 응답을 읽어옴
		Fputs(buf, stdout);
		//Fputs -> 응답을 표준 출력(stdout)에 출력(화면에 표시)
	}

	Close(clientfd);
	exit(0);
	//루프가 끝나면(EOF, Ctrl + D) 소켓을 닫고 프로그램 종료
	//소켓을 닫으면 서버는 클라이언트의 EOF를 감지한다.
}