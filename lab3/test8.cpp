#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#include <iostream>
#include <string>

using namespace std;

int main()
{
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return 1;
  
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(10000 + 221);
  if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
    cerr << strerror(errno) << endl;
    return 0;
  }

  while (true) {
    char buf[65536] = {};
    memset(&sin, 0, sizeof(sin));
    socklen_t sin_size = sizeof(sin);
    int numBytes = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *) &sin, &sin_size);
    cout << "Recevied: " << numBytes << endl;
    cout << "From: " << inet_ntoa(sin.sin_addr) << endl;
    cout << buf << endl;

    string buf2 = buf;
    numBytes = sendto(s, buf2.c_str(), buf2.length(), 0, (struct sockaddr *) &sin, sizeof(sin));
    cout << "Sent: " << numBytes << endl;
  }

  close(s);
  return 0;
}