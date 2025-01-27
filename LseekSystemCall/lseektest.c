#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define BUFMAX 512

int main(int argc, char **argv) {
  int fd, offset; // fd, offset 선언
  char buf[BUFMAX];
  
  if(argc < 4) { // 입력 방식이 잘못됐을 경우 에러처리
    printf(2, "usage : lseek_test <filename> <offset> <string> \n");
    exit();
  }

  fd = open(argv[1], O_RDWR);
  if(fd < 0) { // 파일이 열리지 않을 경우 에러처리
    printf(2, "open error for %s\n", argv[1]);
    exit();
  }

  if(lseek(fd, 0, SEEK_SET) < 0) { // lseek() 함수 에러 처리
    printf(2, "lseek error\n");
    exit();
  }

  int len;
  printf(1, "Before : ");
  while((len = read(fd, buf, sizeof(buf)-1)) > 0) { // 한 줄씩 읽으면서 파일 내용 출력
    buf[len] = '\0';
    printf(1, "%s\n", buf);
  }


  offset = atoi(argv[2]); // offset 값을 정수로 변환

  if(lseek(fd, offset, SEEK_SET) < 0) { // lseek 함수를 이용하여 offset 이동 및 에러처리
    printf(2, "lseek error\n");
    return -1;
  }
  
  if(write(fd, argv[3], strlen(argv[3])) != strlen(argv[3])) { // 입력받은 내용을 해당 오프셋에 write 및 에러처리
    printf(2, "write error\n");
    return -1;
  }

  lseek(fd, 0, SEEK_SET); // lseek 을 통해 오프셋 0으로 설정

  printf(1, "After : ");

  int length;
  while((length = read(fd, buf, sizeof(buf)-1)) > 0) { // 한 줄 씩 읽으며 내용 출력
    buf[length] = '\0';
    printf(1, "%s\n", buf);
  }
 
  printf(1, "\n"); 

  exit();
}
