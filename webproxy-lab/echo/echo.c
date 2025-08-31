#include "csapp.h"

void echo(int connfd){
	// connfd는 accept() 호출로 만들어진 연결된 소켓 디스크립터
	// 이 소켓을 통해 서버와 특정 클라이언트가 데이터를 주고 받는다
	
	size_t n;
	char buf[MAXLINE];
	//buf: 클라이언트가 보낸 데이터를 임시로 저장할 버퍼
	rio_t rio;
	//rio: Robust I/O 패키지의 구조체
	//내부적으로 소켓(connfd)과 연결된 버퍼 정보를 저장
	
	Rio_readinitb(&rio, connfd);
	// rio 구조체를 connfd와 연결해서 버퍼드 I/O 준비를 한다
	// 이후 Rio_readlineb 같은 함수가 connfd에서 데이터를 읽을 때 효율적으로 동작할 수 있다(커널 호출을 최소화 -> 성능 향상)
	while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
		//Rio_readlineb:
			//클라이언트가 보낸 텍스트 라인을 읽어 buf에 저장
			//최대 MAXLINE -1 바이트까지 읽음
			//n은 읽어들인 바이트 수
			//n == 0이면 EOF -> 반복문 종료
		printf("server received %d bytes\n", (int)n);
			//서버 측 콘솔에 몇 바이트 받았다를 출력(디버깅 용)
		Rio_writen(connfd, buf, n);
			//읽은 데이터를 그대로 클라이언트에게 다시 전송(에코 동작)
	}
}