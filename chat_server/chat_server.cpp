/**
 * @file chat_server.cpp
 * @brief 채팅 서버의 클라이언트 및 방 관리와 메시지 처리 기능을 구현하는 클래스들
 */

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include "message.pb.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <string>
#include <map>
#include <unordered_map>
#include <queue>
#include <vector>
#include <set>
#include <functional>
#include <atomic>
#include <stdexcept>

#include </home/students/2024-2/u60182195/git/mju_backend_60182195/chat_server/nlohmann/json.hpp>

using namespace std;
using namespace mju;
using json = nlohmann::json;
using Index = int;

static const uint16_t PORT = 10221; ///< 서버 포트 번호
string format = "json"; ///< 기본 메시지 포맷

// 프로그램 종료를 위한 atomic flag
atomic<bool> quit(false);

// 클라이언트 메시지를 처리하기 위한 큐
queue<int> task_queue;
mutex queue_mutex;
condition_variable task_cv;

mutex room_mutex; // 방의 원자성을 위한 뮤텍스


/**
 * @brief 메시지에 "type" 필드가 없을 때 발생하는 예외 클래스
 */
class NoTypeFieldInMessage : public runtime_error {
  public:
    NoTypeFieldInMessage() : runtime_error("Message is missing a 'type' field") {}
};

/**
 * @brief 메시지 타입이 잘못된 경우 예외를 던지는 클래스
 */
class UnknownTypeInMessage : public runtime_error {
  public:
    UnknownTypeInMessage(const string &type)
      :runtime_error("Unknown message type: " + type) {}
};


/**
 * @brief 클라이언트 정보를 저장하는 클래스
 */
class Client {
  private:
    int client_fd; ///< 클라이언트의 소켓 파일 디스크립터
    string client_name; ///< 클라이언트 이름
    int entered_room_id; ///< 클라이언트가 속한 방 ID

    int current_message_len; ///< 현재 처리 중인 메시지의 길이.
    string socket_buffer; ///< 소켓에서 수신한 데이터를 저장하는 버퍼.
    string current_protobuf_type; ///< 현재 처리 중인 Protobuf 메시지의 타입.

    bool is_waiting; ///< 클라이언트가 task queue에서 대기 중인지 여부

  public:
    /**
     * @brief 기본 생성자
     */
    Client() {}

    /**
     * @brief 클라이언트 정보를 초기화하는 생성자
     *  
     * @param client_fd 클라이언트 소켓 파일 디스크립터
     * @param client_name 클라이언트의 (ip, port) 로 이루어진 클라이언트 이름
     */
    Client(int client_fd, const string &client_name) 
    : client_fd(client_fd), entered_room_id(0) ,client_name(client_name), is_waiting(false), current_message_len(0) {}

    

    //setter
    void set_client_name(string name) {client_name = name;}
    void set_entered_room_id(int room_id) {entered_room_id = room_id;}
    void set_current_message_len(int current_message_len) {this->current_message_len = current_message_len;}
    void append_socket_buffer(char message[], int size) {this->socket_buffer.append(message, size);}
    void erase_socket_buffer(int start, int end) {this->socket_buffer.erase(start, end);}
    void set_current_protobuf_type(string current_protobuf_type) {this->current_protobuf_type = current_protobuf_type;}
    void set_is_waiting(bool is_waiting) {this->is_waiting = is_waiting;}

    //getter
    const int &get_client_fd() {return client_fd;}
    const string &get_client_name() {return client_name;}
    const int &get_entered_room_id() {return entered_room_id;}
    const int &get_current_message_len() {return current_message_len;}
    const string &get_socket_buffer() {return socket_buffer;}
    string substr_socket_buffer(int start, int end) {return socket_buffer.substr(start, end);}
    const string &get_current_protobuf_type() {return current_protobuf_type;}
    const bool &get_is_waiting() {return is_waiting;}
};

/**
 * @brief 채팅방 정보를 저장하는 클래스
 */
class Room {
  private:
    using MemberMap = map<Index, Client*>;

    int room_id;
    string title;
    MemberMap members; ///< 방에 속한 클라이언트들

  public:
    static int next_room_id; ///< 다음에 만들어질 방 ID
    /**
     * @brief 기본 생성자
     */
    Room() {}

    /**
     * @brief 방 정보를 초기화하는 생성자
     * 
     * @param title 방 제목
     */
    Room(string title) : title(title) {
      room_id = next_room_id;
      next_room_id++;
      cout << "방[" << room_id << "] 생성. 방제 " << title << endl;
    }

    /**
     * @brief 클라이언트를 방에 추가하는 함수
     * 
     * @param sock 클라이언트 소켓
     * @param client 추가할 클라이언트 객체
     * @param room_id 방 ID
     */
    void join_client(int sock, Client *Client, int room_id) {
      {
        unique_lock<mutex> lock(room_mutex);
        members.insert({sock, Client});
        members[sock]->set_entered_room_id(room_id);
      }
    }

    /**
     * @brief 클라이언트를 방에서 제거하는 함수
     * 
     * @param sock 클라이언트 소켓
     */
    void leave_client(int sock) {
      {
        unique_lock<mutex> lock(room_mutex);
        members[sock]->set_entered_room_id(0);
        members.erase(sock);
      }
    }

    //getter
    json get_room() {
      json room {
        {"roomId", room_id},
        {"title", title},
        {"members", json::array()},
      };

      for (auto it = members.begin() ; it != members.end() ; ++it) {
        auto &member = it->second;
        room["members"].push_back(member->get_client_name());
      }
      
      return room;
    }
    const int &get_room_id() {return room_id;}
    const string &get_title() {return title;}
    const MemberMap &get_members() {return members;}
};
int Room::next_room_id = 1;

using RoomMap = map<Index, Room>;
using ClientMap = unordered_map<Index, Client>;

/**
 * @brief MessageHandlers 클래스는 다양한 유형의 메시지 처리를 담당.
 * 
 * @tparam Format JSON 또는 Protobuf 등 메시지 포맷을 나타내는 타입.
 */
template <typename Format>
class MessageHandlers {
  private:
    using MessageHandler = function<void(int, Format)>;
    using HandlerMap = unordered_map<string, MessageHandler>;
    using MessageList = vector<Format>;

    HandlerMap handlers;
    ClientMap *client_sockets;
    RoomMap *rooms; 


    /**
     * @brief 메시지 핸들러 맵을 초기화.
     * 각 메시지 타입에 맞는 핸들러 함수를 설정.
     */
    void init_message_handlers() {
      handlers["CSName"] = [this](int sock, Format argv) {on_cs_name(sock, argv);};
      handlers["CSRooms"] = [this](int sock, Format argv) {on_cs_rooms(sock, argv);};
      handlers["CSCreateRoom"] = [this](int sock, Format argv) {on_cs_create_room(sock, argv);};
      handlers["CSJoinRoom"] = [this](int sock, Format argv) {on_cs_join_room(sock, argv);};
      handlers["CSLeaveRoom"] = [this](int sock, Format argv) {on_cs_leave_room(sock, argv);};
      handlers["CSChat"] = [this](int sock, Format argv) {on_cs_chat(sock, argv);};
      handlers["CSShutdown"] = [this](int sock, Format argv) {on_cs_shutdown(sock, argv);};

      handlers[to_string(Type_MessageType_CS_NAME)] = [this](int sock, Format argv) {on_cs_name(sock, argv);};
      handlers[to_string(Type_MessageType_CS_ROOMS)] = [this](int sock, Format argv) {on_cs_rooms(sock, argv);};
      handlers[to_string(Type_MessageType_CS_CREATE_ROOM)] = [this](int sock, Format argv) {on_cs_create_room(sock, argv);};
      handlers[to_string(Type_MessageType_CS_JOIN_ROOM)] = [this](int sock, Format argv) {on_cs_join_room(sock, argv);};
      handlers[to_string(Type_MessageType_CS_LEAVE_ROOM)] = [this](int sock, Format argv) {on_cs_leave_room(sock, argv);};
      handlers[to_string(Type_MessageType_CS_CHAT)] = [this](int sock, Format argv) {on_cs_chat(sock, argv);};
      handlers[to_string(Type_MessageType_CS_SHUTDOWN)] = [this](int sock, Format argv) {on_cs_shutdown(sock, argv);};
    }
    /**
     * @brief 클라이언트의 이름을 설정하는 메시지를 처리.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param argv 메시지 데이터
     */
    void on_cs_name(int sock, Format argv) {
      MessageList messages; ///< 보낼 메시지 리스트
      auto &client_socket = (*client_sockets)[sock];

      if constexpr (is_same<Format, json>::value) {
        //json 메시지 처리
        json message = {
          {"type", "SCSystemMessage"},
          {"text", client_socket.get_client_name() +  " 의 이름이 " + argv["name"].dump() + " 으로 변경되었습니다"},
        };

        client_socket.set_client_name(argv["name"]);
        
        messages.push_back(message);

      } else {
        //protobuf 메시지 처리
        Type *message_type = new Type;
        SCSystemMessage *message_sys = new SCSystemMessage;
        CSName *cs_name = new CSName;
        cs_name->ParseFromString(argv);

        //메시지 세팅
        message_type->set_type(Type_MessageType_SC_SYSTEM_MESSAGE);
        messages.push_back(message_type->SerializeAsString());

        message_sys->set_text(client_socket.get_client_name() +  " 의 이름이 " + cs_name->name() + " 으로 변경되었습니다");
        messages.push_back(message_sys->SerializeAsString());

        //이름 세팅
        client_socket.set_client_name(cs_name->name());

        delete(message_type);
        delete(message_sys);
        delete(cs_name);
      }

      send_messages_to_client(sock, messages);
      //방에 있을 시 브로드캐스트
      if (client_socket.get_entered_room_id() != 0) {  
        broadcast(sock, messages);
      }

      return;
    }

    /**
     * @brief 방 목록 요청 메시지를 처리.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param argv 메시지 데이터
     */
    void on_cs_rooms(int sock, Format argv) {
      MessageList messages; ///< 보낼 메시지 리스트

      if constexpr (is_same<Format, json>::value) {
        //json 메시지 처리
        if (!(*rooms).empty()) {
          json message {
            {"type", "SCRoomsResult"},
            {"rooms", json::array()},
          };

          //방 목록 추가
          for (auto it = (*rooms).begin() ; it != (*rooms).end() ; ++it) {
            auto &room = it->second;
            message["rooms"].push_back(room.get_room());
          }

          messages.push_back(message);
        } else {
          json message = {
            {"type", "SCSystemMessage"},
            {"text", "개설된 방이 없습니다."},
          };
          messages.push_back(message);
        }

      } else {
        //protobuf 메시지 처리
        Type *message_type = new Type;
        
        if (!(*rooms).empty()) {
          SCRoomsResult *message_room_result = new SCRoomsResult;
          SCRoomsResult::RoomInfo *room_info;

          message_type->set_type(Type_MessageType_SC_ROOMS_RESULT);
          messages.push_back(message_type->SerializeAsString());

          //방 목록 추가
          for (auto it = (*rooms).begin() ; it != (*rooms).end() ; ++it) {
            auto &room = it->second;

            room_info = message_room_result->add_rooms();
            room_info->set_roomid(room.get_room_id());
            room_info->set_title(room.get_title());

            auto members = room.get_members();
            for (auto i = members.begin() ; i != members.end() ; ++i) {
              auto &member = i->second;
              room_info->add_members(member->get_client_name());
            }
          }
          messages.push_back(message_room_result->SerializeAsString());

          delete(message_room_result);
        } else {
          SCSystemMessage *message_sys = new SCSystemMessage;

          message_type->set_type(Type_MessageType_SC_SYSTEM_MESSAGE);
          messages.push_back(message_type->SerializeAsString());

          message_sys->set_text("개설된 방이 없습니다.");
          messages.push_back(message_sys->SerializeAsString());

          delete(message_sys);
        }

        delete(message_type);
      }

      send_messages_to_client(sock, messages);

      return;
    }

    /**
     * @brief 방 생성 메시지를 처리.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param argv 메시지 데이터
     */
    void on_cs_create_room(int sock, Format argv) {
      MessageList messages; ///< 보낼 메시지 리스트
      int client_room_id = (*client_sockets)[sock].get_entered_room_id(); // 클라이언트가 현재 들어가 있는 방 ID, 없다면 0

      if constexpr (is_same<Format, json>::value) {
        //json 메시지 처리
        if (client_room_id != 0) {
          json message = {
            {"type", "SCSystemMessage"},
            {"text", "대화 방에 있을 때는 방을 개설 할 수 없습니다."},
          };
          messages.push_back(message);
        } else {
          int room_id = Room::next_room_id;
          
          //방 생성
          Room room(argv["title"]);
          {
            unique_lock<mutex> lock(room_mutex);
            (*rooms)[room_id] = room;
          }
          (*rooms)[room_id].join_client(sock, &(*client_sockets)[sock], room_id);

          json message = {
            {"type", "SCSystemMessage"},
            {"text", "방제[" + (*rooms)[room_id].get_title() + "] 방에 입장했습니다."},
          };
          messages.push_back(message);
        }

      } else {
        //protobuf 메시지 처리
        Type *message_type = new Type;
        SCSystemMessage *message_sys = new SCSystemMessage;

        message_type->set_type(Type_MessageType_SC_SYSTEM_MESSAGE);
        messages.push_back(message_type->SerializeAsString());

        if (client_room_id != 0) {
          message_sys->set_text("대화 방에 있을 때는 방을 개설 할 수 없습니다.");
          messages.push_back(message_sys->SerializeAsString());
        } else {
          int room_id = Room::next_room_id;
          CSCreateRoom *cs_create_room = new CSCreateRoom;
          cs_create_room->ParseFromString(argv);

          //방 생성
          Room room(cs_create_room->title());
          {
            unique_lock<mutex> lock(room_mutex);
            (*rooms)[room_id] = room;
          }
          (*rooms)[room_id].join_client(sock, &(*client_sockets)[sock], room_id);
          
          message_sys->set_text("방제[" + (*rooms)[room_id].get_title() + "] 방에 입장했습니다.");
          messages.push_back(message_sys->SerializeAsString());

          delete(cs_create_room);
        }

        delete(message_type);
        delete(message_sys);
      }

      send_messages_to_client(sock, messages);

      return;
    }

    /**
     * @brief 방 입장 메시지를 처리.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param argv 메시지 데이터
     */
    void on_cs_join_room(int sock, Format argv) {
      MessageList messages; ///< 보낼 메시지 리스트
      int client_room_id = (*client_sockets)[sock].get_entered_room_id(); // 클라이언트가 현재 들어가 있는 방 ID, 없다면 0

      if constexpr (is_same<Format, json>::value) {
        //json 메시지 처리
        if (client_room_id != 0) {
          json message = {
            {"type", "SCSystemMessage"},
            {"text", "대화 방에 있을 때는 다른 방에 들어갈 수 없습니다."},
          };
          messages.push_back(message);
        } else if ((*rooms).find(argv["roomId"]) == (*rooms).end()){
          json message = {
            {"type", "SCSystemMessage"},
            {"text", "대화방이 존재하지 않습니다."},
          };
          messages.push_back(message);
        } else {
          auto &room = (*rooms)[argv["roomId"]];
          auto &client_socket = (*client_sockets)[sock];

          room.join_client(sock, &client_socket, argv["roomId"]); // 방 입장

          json message = {
            {"type", "SCSystemMessage"},
            {"text", "[" + client_socket.get_client_name() + "] 님이 입장했습니다."},
          };
          messages.push_back(message);
          
          broadcast(sock, messages);
          messages.pop_back();

          message = {
            {"type", "SCSystemMessage"},
            {"text", "방제[" + room.get_title() + "] 방에 입장했습니다."},
          };
          messages.push_back(message);
        }

      } else {
        //protobuf 메시지 처리
        Type *message_type = new Type;
        SCSystemMessage *message_sys = new SCSystemMessage;
        CSJoinRoom *cs_join_room = new CSJoinRoom;
        cs_join_room->ParseFromString(argv);

        message_type->set_type(Type_MessageType_SC_SYSTEM_MESSAGE);
        messages.push_back(message_type->SerializeAsString());

        if (client_room_id != 0) {
          message_sys->set_text("대화 방에 있을 때는 다른 방에 들어갈 수 없습니다.");
          messages.push_back(message_sys->SerializeAsString());

        } else if ((*rooms).find(cs_join_room->roomid()) == (*rooms).end()){
          message_sys->set_text("대화방이 존재하지 않습니다.");
          messages.push_back(message_sys->SerializeAsString());

        } else {      
          auto &room = (*rooms)[cs_join_room->roomid()];
          auto &client_socket = (*client_sockets)[sock];

          room.join_client(sock, &client_socket, cs_join_room->roomid()); // 방 입장
          
          message_sys->set_text("[" + client_socket.get_client_name() + "] 님이 입장했습니다.");
          messages.push_back(message_sys->SerializeAsString());

          broadcast(sock, messages);
          messages.pop_back();

          message_sys->set_text("방제[" + room.get_title() + "] 방에 입장했습니다.");
          messages.push_back(message_sys->SerializeAsString());
        }

        delete(message_type);
        delete(message_sys);
        delete(cs_join_room);
      }

      send_messages_to_client(sock, messages);

      return;
    }

    /**
     * @brief 방 퇴장 메시지를 처리.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param argv 메시지 데이터
     */
    void on_cs_leave_room(int sock, Format argv) {
      MessageList messages; ///< 보낼 메시지 리스트
      int client_room_id = (*client_sockets)[sock].get_entered_room_id(); // 클라이언트가 현재 들어가 있는 방 ID, 없다면 0

      if constexpr (is_same<Format, json>::value) {
        //json 메시지 처리
        if (client_room_id == 0) {
          json message = {
            {"type", "SCSystemMessage"},
            {"text", "현재 대화방에 들어가 있지 않습니다."},
          };
          messages.push_back(message);
        } else {
          auto &client_socket = (*client_sockets)[sock];
          auto &room = (*rooms)[client_room_id];

          json message = {
            {"type", "SCSystemMessage"},
            {"text", "[" + client_socket.get_client_name() + "] 님이 퇴장했습니다."}
          };
          messages.push_back(message);
          broadcast(sock, messages);
          messages.pop_back();

          //방 퇴장, 퇴장 후 방에 멤버가 아무도 없다면 방폭
          room.leave_client(sock);
          if (room.get_members().size() == 0) {
            cout << "방[" << client_room_id << "] 명시적 /leave로 인해 삭제"<< endl;
            {
              unique_lock<mutex> lock(room_mutex);
              (*rooms).erase(client_room_id);
            }
          }

          message = {
            {"type", "SCSystemMessage"},
            {"text", "방제[" + room.get_title() + "] 대화 방에서 퇴장했습니다."},
          };
          messages.push_back(message);
        }
      } else {
        //protobuf 메시지 처리
        Type *message_type = new Type;
        SCSystemMessage *message_sys = new SCSystemMessage;

        message_type->set_type(Type_MessageType_SC_SYSTEM_MESSAGE);
        messages.push_back(message_type->SerializeAsString());

        if (client_room_id == 0) {
          message_sys->set_text("현재 대화방에 들어가 있지 않습니다.");
          messages.push_back(message_sys->SerializeAsString());

        } else {
          auto &client_socket = (*client_sockets)[sock];
          auto &room = (*rooms)[client_room_id];

          message_sys->set_text("[" + client_socket.get_client_name() + "] 님이 퇴장했습니다.");
          messages.push_back(message_sys->SerializeAsString());

          broadcast(sock, messages);
          messages.pop_back();
          
          //방 퇴장, 퇴장 후 방에 멤버가 아무도 없다면 방폭
          room.leave_client(sock);
          if (room.get_members().size() == 0) {
            cout << "방[" << client_room_id << "] 명시적 /leave로 인해 삭제"<< endl;
            {
              unique_lock<mutex> lock(room_mutex);
              (*rooms).erase(client_room_id);
            }
          }

          message_sys->set_text("방제[" + room.get_title() + "] 대화 방에서 퇴장했습니다.");
          messages.push_back(message_sys->SerializeAsString());
        }

        delete(message_type);
        delete(message_sys);
      }

      send_messages_to_client(sock, messages);

      return;
    }

    /**
     * @brief 채팅 메시지를 처리.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param argv 메시지 데이터
     */
    void on_cs_chat(int sock, Format argv) {
      MessageList messages; ///< 보낼 메시지 리스트
      int client_room_id = (*client_sockets)[sock].get_entered_room_id(); // 클라이언트가 현재 들어가 있는 방 ID, 없다면 0

      if constexpr (is_same<Format, json>::value) {
        //json 메시지 처리
        if (client_room_id == 0) {
          json message = {
            {"type", "SCSystemMessage"},
            {"text", "현재 대화방에 들어가 있지 않습니다."},
          };
          messages.push_back(message);
        } else {
          json message = {
            {"type", "SCChat"},
            {"member", (*client_sockets)[sock].get_client_name()},
            {"text", argv["text"]},
          };
          messages.push_back(message);
        }

      } else {
        //protobuf 메시지 처리
        Type *message_type = new Type;
        
        if (client_room_id == 0) {
          SCSystemMessage *message_sys = new SCSystemMessage;

          message_type->set_type(Type_MessageType_SC_SYSTEM_MESSAGE);
          messages.push_back(message_type->SerializeAsString());

          message_sys->set_text("현재 대화방에 들어가 있지 않습니다.");
          messages.push_back(message_sys->SerializeAsString());

          delete(message_sys);
        } else {
          SCChat *sc_chat = new SCChat;
          CSChat *cs_chat = new CSChat;
          cs_chat->ParseFromString(argv);

          message_type->set_type(Type_MessageType_SC_CHAT);
          messages.push_back(message_type->SerializeAsString());

          sc_chat->set_member((*client_sockets)[sock].get_client_name());
          sc_chat->set_text(cs_chat->text());
          messages.push_back(sc_chat->SerializeAsString());

          delete(sc_chat);
          delete(cs_chat);
        }

        delete(message_type);
      }

      send_messages_to_client(sock, messages);
      if (client_room_id != 0) {
        broadcast(sock, messages);
      }

      return;
    }

    /**
     * @brief 서버 종료 메시지를 처리.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param argv 메시지 데이터
     */
    void on_cs_shutdown(int sock, Format message) {
      quit.store("true");
      cout << "shutdown" << endl;

      return;
    }

    /**
     * @brief 특정 클라이언트에게 메시지 리스트를 전송.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param messages 전송할 메시지 리스트
     */
    void send_messages_to_client(int sock, MessageList messages){
      for (auto message = messages.begin(); message != messages.end(); ++message) {
        string serialized;
        if constexpr (is_same<Format, json>::value) {
          serialized = (*message).dump(); //json
        } else {
          serialized = (*message); //protobuf
        }
        
        // 어디까지 읽어야 되는지 message boundary 추가
        uint16_t to_send = htons(serialized.length());
        char to_send_big_endian[2];
        memcpy(to_send_big_endian, &to_send, 2);
        serialized = string(to_send_big_endian, 2) + serialized;

        int offset = 0;
        while (offset < serialized.length()) {
          int num_sent = send(sock, serialized.data() + offset, serialized.length() - offset, 0);
          if (num_sent <= 0) {
            cerr << "send() failed: " << strerror(errno) << ", clientSock: " << sock << endl;
            return;
          } 
          // cout << "Sent: " << num_sent << " bytes, clientSock: " << sock << endl;
          offset += num_sent;
        }
      }

      return;
    }

    /**
     * @brief 송신 클라이언트의 방에 있는 모든 클라이언트에게 메시지를 브로드캐스트.
     * 
     * @param sock 송신 클라이언트 소켓 번호
     * @param messages 브로드캐스트할 메시지 리스트
     */
    void broadcast(int sock, MessageList messages) {
      int client_room_id = (*client_sockets)[sock].get_entered_room_id();

      if (client_room_id == 0) {
        cout << "broadcast failed" << endl;
        return;

      } else {
        auto members = (*rooms)[client_room_id].get_members();
        {
          //브로드 캐스트 중에 방 멤버가 바뀌거나 방이 사라지지 않도록 mutex로 보호
          unique_lock<mutex> lock(room_mutex);
          for (auto it = members.begin() ; it != members.end() ; ++it) {
            auto &member = it->second;
            if (member->get_client_fd() == sock) continue;
            send_messages_to_client(member->get_client_fd() , messages);
          }
        }
      }

      return;
    }


  public:
    /**
     * @brief 생성자, 클라이언트와 방을 초기화하고 메시지 핸들러를 설정.
     * 
     * @param client_sockets 클라이언트 소켓 관리 포인터
     * @param rooms 채팅 방 관리 포인터
     */
    MessageHandlers(ClientMap *client_sockets, RoomMap *rooms) 
      : client_sockets(client_sockets), rooms(rooms) {
      init_message_handlers();
    }

    /**
     * @brief 메시지를 처리하고 해당하는 핸들러를 실행.
     * 
     * @param sock 클라이언트 소켓 번호
     * @param type 메시지 타입
     * @param argv 메시지 데이터
     */
    void handle_message(int sock, string type, Format argv) {
      if (handlers.find(type) != handlers.end()) {
        handlers[type](sock, argv);
      } else {
        throw UnknownTypeInMessage(type);
      }
    }
};

/**
 * @class ChatServer
 * @brief 채팅 서버 기능을 처리하는 클래스.
 * 
 * 서버 소켓 생성, 클라이언트 연결 관리 및 JSON 또는 Protobuf 메시지를 처리하는 기능을 제공.
 * 여러 개의 워커 스레드를 사용하여 클라이언트의 메시지를 병렬로 처리.
 */
class ChatServer {
  private:
    int server_socket; ///< 서버 소켓 파일 디스크립터.
    ClientMap client_sockets; ///< 연결된 클라이언트 소켓을 저장하는 맵.
    RoomMap rooms; ///< 방 정보를 저장하는 맵.
    set<int> will_close_client; ///< 닫을 소켓들.
    MessageHandlers<json> json_message_handlers; ///< JSON 메시지 핸들러.
    MessageHandlers<string> protobuf_message_handlers; ///< Protobuf 메시지 핸들러.
    vector<thread> worker_threads; ///< 클라이언트를 병렬로 처리할 워커 스레드들.


    /**
     * @brief 서버 소켓을 초기화하여 클라이언트의 연결을 대기.
     * 
     * @param port 서버 소켓이 바인딩될 포트 번호.
     */
    void init_server_socket(int port) {
      server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (server_socket < 0) {
        cerr << "socket() failed: " << strerror(errno) << endl;
        exit(1);
      }

      int on = 1;
      if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        cerr << "setsockopt() failed: " << strerror(errno) << endl;
        exit(1);
      }

      struct sockaddr_in sin;
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = INADDR_ANY;
      sin.sin_port = htons(port);
      if (bind(server_socket, (struct sockaddr *) &sin, sizeof(sin))  < 0) {
        cerr << "bind() failed: " << strerror(errno) << endl;
        exit(1);
      }

      if (listen(server_socket, 10) < 0) {
        cerr << "listen() failed: " << strerror(errno) << endl;
        exit(1);
      }
    }

    /**
     * @brief 클라이언트의 메세지를 처리할 워커 스레드를 초기화.
     * 
     * @param num_worker 생성할 워커 스레드의 수.
     */
    void init_worker_threads(int num_worker) {
      for (int i = 0; i < num_worker; ++i) {
        worker_threads.emplace_back([this, i]() {
          cout << "thread " << i << " started" << endl;
          while (quit.load() == false) {
            int sock;
            {
              unique_lock<mutex> lock(queue_mutex);
              while (task_queue.empty() && quit.load() == false) {
                task_cv.wait(lock);
              }

              if (quit.load() == true) {
                continue;
              }

              sock = task_queue.front();
              task_queue.pop();
              // cout << "Consumed: " << sock << endl;
            }
            process_socket(sock);
            client_sockets[sock].set_is_waiting(false);
          }
          cout << "thread " << i << " finished" << endl;
        });
      }
    }

    /**
     * @brief 새로운 클라이언트 연결을 받아들여 클라이언트 소켓 맵에 추가.
     * 
     * 새로운 클라이언트 연결을 수락하고, 해당 연결 정보를 클라이언트 맵에 저장.
     */
    void make_new_connection() {
      struct sockaddr_in sin;
      memset(&sin, 0, sizeof(sin));
      socklen_t sin_len = sizeof(sin);
      int sock = accept(server_socket, (struct sockaddr *) &sin, &sin_len);
      if (sock < 0) {
        cerr << "accept() failed: " << strerror(errno) << endl;
      } else {
        memset(&sin, 0, sizeof(sin));
        sin_len = sizeof(sin);
        if (getpeername(sock, (struct sockaddr *) &sin, &sin_len) == 0) {
          Client client_info(sock, "(" + to_string(*inet_ntoa(sin.sin_addr)) + ", " + to_string(ntohs(sin.sin_port)) + ")");
          client_sockets[sock] = client_info;
          cout << "new connection succes, [" << client_sockets[sock].get_client_name() << "]" << endl;
        } else {
          cerr << "getpeername() failed: " << strerror(errno) << endl;
          close(sock);
        }
      }
    }

    /**
     * @brief 클라이언트 소켓에서 받은 JSON 또는 Protobuf 형식 데이터를 처리.
     * 
     * @param sock 클라이언트 소켓.
     * 
     * 주어진 소켓에서 데이터를 읽고, 메시지가 완성되었는지 확인한 후,
     * 그 형식에 맞게(JSON 또는 Protobuf) 메시지를 처리.
     */
    void process_socket(int sock) {
      char received_buffer[65535];
      int num_recv = recv(sock, received_buffer, sizeof(received_buffer), 0);
      if (num_recv == 0) {
        will_close_client.insert(sock);
        return;
      } else if (num_recv < 0) {
        cerr << "recv() failed: " << strerror(errno) << endl;
        will_close_client.insert(sock);
        return;
      } else {
        // cout << "Received: " << num_recv << "bytes, clientSock " << sock << endl;
      }
      
      auto &client_socket = client_sockets[sock];
      auto &socket_buf = client_socket.get_socket_buffer();
      
      client_socket.append_socket_buffer(received_buffer, num_recv);

      while (true) {
        if (client_socket.get_current_message_len() == 0) {
          if (socket_buf.length() < 2) {
            return;
          }

          uint16_t current_message_len_big_endian;
          memcpy(&current_message_len_big_endian, socket_buf.data(), 2);
          client_socket.set_current_message_len(ntohs(current_message_len_big_endian));

          client_socket.erase_socket_buffer(0, 2);
        }

        if (socket_buf.length() < client_socket.get_current_message_len()) {
          return;
        }

        string serialized = client_socket.substr_socket_buffer(0, client_socket.get_current_message_len());
        client_socket.erase_socket_buffer(0, client_socket.get_current_message_len());
        client_socket.set_current_message_len(0);

        try {
          if (format == "json") {
          
            json msg = json::parse(serialized);
            // cout << "받은 JSON serialized: " << msg.dump(2) << endl;
            if (!msg.contains("type")) {
              throw NoTypeFieldInMessage();
            }
            json_message_handlers.handle_message(sock, msg["type"], msg);
          
          } else {
            if (client_socket.get_current_protobuf_type().empty()) {
              Type *msg = new Type;
              msg->ParseFromString(serialized);
              client_socket.set_current_protobuf_type(to_string(msg->type()));
              // cout << "받은 protobuf type: " << client_socket.get_current_protobuf_type() << endl;

              delete(msg);
            } else {
              protobuf_message_handlers.handle_message(sock, client_socket.get_current_protobuf_type(), serialized);
              client_socket.set_current_protobuf_type("");
            }
          } 
        } catch (const json::parse_error &e) {
          will_close_client.insert(sock);
          cerr << "Error: " << e.what() << endl;
        } catch (const NoTypeFieldInMessage &e){
          will_close_client.insert(sock);
          cerr << "Error: " << e.what() << endl;
        } catch (const UnknownTypeInMessage &e){
          will_close_client.insert(sock);
          cerr << "Error: " << e.what() << endl;
        } catch (const exception& e) {
          will_close_client.insert(sock);
          cerr << "Error: " << e.what() << endl;
        }
      }
    }

  public:
      /**
     * @brief ChatServer 생성자.
     * 
     * @param port 서버 소켓이 바인딩될 포트 번호.
     * @param num_worker 메시지를 처리할 워커 스레드의 수.
     */
    ChatServer(int port, int num_worker) 
      : json_message_handlers(&client_sockets, &rooms), protobuf_message_handlers(&client_sockets, &rooms) {
      init_server_socket(port);
      init_worker_threads(num_worker);
    }

    /**
     * @brief ChatServer 소멸자.
     * 
     * 모든 스레드를 종료하고, 클라이언트 소켓과 서버 소켓을 안전하게 닫는다.
     */
     ~ChatServer() {
      quit.store(true);
      task_cv.notify_all();

      for (auto& thread : worker_threads) {
        if (thread.joinable()) {
          thread.join();
        }        
      }
      
      for (auto it = client_sockets.begin() ; it != client_sockets.end() ; ++it) {
        auto &client_socket = it->second;
        close(client_socket.get_client_fd());
      }

      client_sockets.clear();
      rooms.clear();

      close(server_socket);
    }

    /**
     * @brief 서버의 메인 이벤트 루프를 실행.
     * 
     * `select`를 사용하여 클라이언트 소켓과 서버 소켓에서 데이터가 들어오는지 감지하고,
     * 새로운 연결이나 데이터를 처리.
     */
    void run() {
      while (quit.load() == false) {
        fd_set rset;
        FD_ZERO(&rset);

        FD_SET(server_socket, &rset);
        int max_fd = server_socket;
        
        for (auto it = client_sockets.begin() ; it != client_sockets.end() ; ++it) {
          int sock = it->first;
          FD_SET(sock, &rset);
          if (sock > max_fd) {
            max_fd = sock;
          }
        }

        struct timeval tv = {0, 1000};
        int num_ready = select(max_fd + 1, &rset, NULL, NULL, &tv);
        if (num_ready < 0) {
          cerr << "select() failed: " << strerror(errno) << endl;
          continue;
        }

        if (FD_ISSET(server_socket, &rset)) {
          make_new_connection();
        }

        for (auto it = client_sockets.begin() ; it != client_sockets.end() ; ++it) {
          int sock = it->first;
          if (FD_ISSET(sock, &rset)) {
            if (!client_sockets[sock].get_is_waiting()) {
              {
                unique_lock<mutex> lock(queue_mutex);

                task_queue.push(sock);
                client_sockets[sock].set_is_waiting(true);
                task_cv.notify_one();
                // cout << "Produced: " << sock << endl;
              }            
            }
          }
        }

        //닫을 소켓 정리
        for (int sock: will_close_client) {
          cout << "closed: " << sock << endl;
          close(sock);

          int entered_room_id = client_sockets[sock].get_entered_room_id();
          if (entered_room_id != 0) {
            rooms[entered_room_id].leave_client(sock);
            if (rooms[entered_room_id].get_members().size() == 0) {
              {
                unique_lock<mutex> lock(room_mutex);
                cout << "방[" << entered_room_id << "] 클라이언트 연결 종료로 인해 삭제"<< endl;
                rooms.erase(entered_room_id);
              }
            }
          }

          client_sockets.erase(sock);
        }
        will_close_client.clear();
      }
    }
};

/**
 * @brief 프로그램의 진입점.
 * 
 * @param argc 명령줄 인수의 개수.
 * @param argv 명령줄 인수.
 * @return 0 성공, 1 실패.
 */
int main(int argc, char* argv[]) {
  int num_worker = 2;
  
  try {
    for (int i = 1; i < argc; ++i) {
      string arg = argv[i];
      if (arg.rfind("--help", 0) == 0) { // "--help"으로 시작하는지 확인
        cout << endl
             << "       USAGE: chat_server.cpp [flags]" << endl
             << "flags:" << endl
             << endl
             << "chat_server.cpp:" <<endl
             << "  --formet: <json|protobuf>: 메시지 포멧" << endl
             << "    (default: 'json')" << endl
             << "  --workers: 작업 쓰레드 숫자" << endl
             << "    (default: '2')" << endl
             << "    (an integer)" << endl;
        return 0;
      } else if (arg.rfind("--format=", 0) == 0) { // "--format="으로 시작하는지 확인
        format = arg.substr(9);

        if (format != "json" && format != "protobuf") { 
          throw invalid_argument(format);
        }
      } else if (arg.rfind("--workers=", 0) == 0) { // "--worker="으로 시작하는지 확인    
        num_worker = stoi(arg.substr(10));
      } else {
        throw invalid_argument(format);
      }
    }

  } catch (const invalid_argument &e) {
    cerr << "Error: " << e.what() << endl;
    return 1;
  }

  ChatServer server(PORT, num_worker);
  server.run();

  return 0;
}