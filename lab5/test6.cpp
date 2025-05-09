#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

#include "person.pb.h"

using namespace std;
using namespace mju;

int main() {
  //socket create & set socket
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return 1;
  
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(10001);
  sin.sin_addr.s_addr = inet_addr("127.0.0.1");

  //set Person
  Person *p = new Person;
  p->set_name("MJ Kim");
  p->set_id(12345678);

  Person::PhoneNumber* phone = p->add_phones();
  phone->set_number("010-111-1234");
  phone->set_type(Person::MOBILE);
  
  phone = p->add_phones();
  phone->set_number("02-100-1000");
  phone->set_type(Person::HOME);

  //serialize Person & send
  const string buf = p->SerializeAsString();
  cout << "Length: " << buf.length() << endl;
  cout << buf << endl;
  
  int numBytes = sendto(s, buf.c_str(), buf.length(), 0, (struct sockaddr *) &sin, sizeof(sin));
  cout << "Sent: " << numBytes << endl;

  //recv Person
  char buf2[65536];
  memset(&sin, 0, sizeof(sin));
  socklen_t sin_size = sizeof(sin);
  numBytes = recvfrom(s, buf2, sizeof(buf2), 0, (struct sockaddr *) &sin, &sin_size);
  Person *p2 = new Person;

  //deserialize Person
  p2->ParseFromString(buf2);
  cout << "Name: " << p2->name() << endl;
  cout << "ID: " << p2->id() << endl;
  for (int i=0 ; i < p2->phones_size() ; ++i) {
    cout << "Type: " << p2->phones(i).type() << endl;
    cout << "Phone: " << p2->phones(i).number() << endl;
  }
  cout << "Recevied: " << numBytes << endl;
  cout << "From: " << inet_ntoa(sin.sin_addr) << endl;

  close(s);
  return 0;
}