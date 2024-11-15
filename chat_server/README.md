# 1. chat_server 사용법

## 기본 설정
chat_server.cpp은 C++로 구현되어 있습니다.
기본적으로 worker 스레드는 2개이며 메시지 포멧은 json입니다. 이 부분은 실행 인자를 통해 바꿀 수 있습니다.
port 번호는 10221로 고정되어 있습니다.

## 실행 인자

chat_server.cpp 는 다음 실행 인자들을 사용할 수 있습니다.

* `--help` : 사용 가능한 실행 인자 목록과 간단한 설명을 출력합니다.
* `--format` : `--format=json` 이나 `--format=protobuf` 처럼 쓸 수 있습니다. 클라이언트-서버 간 메시지의 포맷을 지정합니다. 기본 값은 json으로 지정되어 있습니다.
* `--workers`: 메시지 처리 스레드의 수를 지정합니다. 기본 값은 2로 지정되어 있습니다.

## 실행 예시

### JSON 을 이용해서 메시지를 주고 받을 예정일 경우

```
$ g++ -o chat_server chat_server.cpp message.pb.cc -lprotobuf
$ ./chat_server
```

### Protobuf 를 이용해서 메시지를 주고 받을 예정이고, 메시지 처리 스레드의 수를 4로 지정할 경우

```
$ g++ -o chat_server chat_server.cpp message.pb.cc -lprotobuf
$ ./chat_server --format=protobuf --workers=4
```