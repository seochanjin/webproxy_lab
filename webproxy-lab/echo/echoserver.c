#include "csapp.h"

void echo(int connfd);
//연결된 소켓에서 데이터를 읽고 그대로 돌려주는 함수

int main(int argc, char** argv){
	int listenfd; // 서버 소켓(리스닝 소켓) - 클라이언트 요청을 받는 소켓
	int connfd; // 연결된 소켓(커넥션 소켓) - 실제 클라이언트와 통신하는 소켓
	socklen_t clientlen;
	struct sockaddr_storage clientaddr; //접속한 클라이언트의 주소 정보(IP, 포트 등)
	char client_hostname[MAXLINE], client_port[MAXLINE];
	//클라이언트 주소를 문자열 형태로 변환해서 저장할 버퍼
	
	if(argc != 2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}
	// 프로그램 실행 시 반드시 포트 번호를 인자로 받아야 한다.(예: ./echoserver 8080)
	
	listenfd = Open_listenfd(argv[1]);
	//Open_listenfd: 지정된 포트 번호로 리스닝 소켓을 생성하고 클라이언트 연결을 받을 준비
	//내부적으로 socket -> bind -> listen 호출 과정을 포함
	while(1){
		clientlen = sizeof(struct sockaddr_storage);
		connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
		//클라이언트의 연결 요청을 받아들이고 새로운 소켓(connfd)을 리턴
		//이제 connfd를 통해 해당 클라이언트와 데이터 통신이 가능 
		Getnameinfo((SA*) &clientaddr, clientlen, client_hostname, MAXLINE, client_port,MAXLINE, 0);
		//Getnameinfo: 클라이언트의 IP 주소와 포트 번호를 문자열로 변환한다.
		printf("Connected to (%s, %s)\n", client_hostname, client_port);
		//로그를 출력해 누가 접속했는지 확인 가능
		echo(connfd);
		//클라이언트가 보낸 데이터를 읽어서 다시 그대로 돌려줌
		Close(connfd);

	}
	exit(0);
}