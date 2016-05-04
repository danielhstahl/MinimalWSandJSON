#define _USE_MATH_DEFINES
#define _WEBSOCKETPP_CPP11_THREAD_
#define _WEBSOCKETPP_CPP11_CHRONO_
#define _WEBSOCKETPP_CPP11_TYPE_TRAITS_
#define ASIO_STANDALONE
#define ASIO_HAS_STD_ARRAY
#define ASIO_HAS_STD_ATOMIC
#define ASIO_HAS_CSTDINT
#define ASIO_HAS_STD_ADDRESSOF
#define ASIO_HAS_STD_SHARED_PTR
#define ASIO_HAS_STD_TYPE_TRAITS

#include <asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/config/core.hpp>
#include <websocketpp/server.hpp>
#include <unordered_map>
#include "schema.h" //rapidjson
#include "document.h" //rapidjson
#include "writer.h" //rapidjson
#include "reader.h" //rapidjson
#include "stringbuffer.h" //rapidjson
#include "error/error.h" // rapidjson::ParseResult
#include "error/en.h" // rapidjson::ParseResult
#include <iostream>
#include <thread>

struct Parser{
private:
    rapidjson::Document schemaJson;
    
    bool hasJson=false;
    bool hasSchema=false;
public:
    bool noError=false;
    rapidjson::Document json;
    template<typename fnc, typename msg>
    void parse(msg& json_, const fnc& onError){
        if(json.Parse(json_.c_str()).HasParseError()){
            onError("{\"Error\":\"String is not JSON\"}");
        }
        else{
            hasJson=true;
            validateJson(onError);
        }
    }
    template<typename fnc, typename msg>
    void parseSchema(msg& schema_, const fnc& onError){
        if(schemaJson.Parse(schema_.c_str()).HasParseError()){
            onError("{\"Error\":\"String is not JSON\"}");
        }
        else{
            hasSchema=true;
        }
    }
    template<typename fnc>
    void validateJson(const fnc& onError){
        if(hasSchema){
            rapidjson::SchemaDocument schema(schemaJson);
            rapidjson::SchemaValidator validator(schema);
            if (!json.Accept(validator)) {
                std::stringstream ss;
                ss<<"{\"Error\":\""<<validator.GetInvalidSchemaKeyword()<<"\"}";
                onError(ss.str());
                ss.clear();//though should go out of scope anyway
            }
            else{
                noError=true;
            }
        }
    }    
};
typedef websocketpp::server<websocketpp::config::asio> server;
using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;


struct Task{
    template<typename connection, typename parsedValue, typename callback>
    static void run(connection& thisConnection, parsedValue& json, callback& call){
        call("{\"msg\":\"You are connection and you are good!\"}");        
    }
};

template<typename parser, typename longRunningTask> //longRunningTask is a class with a function "run" which accepts the type from the "holdConnections" map, reference to a rapidjson::Document, and a function which sends data back to client
class WS{
private:
    server m_server;
    std::map<connection_hdl, int, std::owner_less<connection_hdl>> holdConnections;
    parser* textParser;
    std::vector<std::thread*> holdThreads;
    std::string on_open_message;
public:
	WS(){
		m_server.init_asio();
		m_server.set_open_handler(bind(&WS<parser, longRunningTask>::on_open,this,::_1));
		m_server.set_close_handler(bind(&WS<parser, longRunningTask>::on_close,this,::_1));
		m_server.set_message_handler(bind(&WS<parser, longRunningTask>::on_message,this,::_1,::_2));
    }
    void on_open(connection_hdl hdl) {
        holdConnections[hdl]=0;//just an example.  Creates a new item for every connection.  Gets deleted on close
        m_server.send(hdl,on_open_message, websocketpp::frame::opcode::text);
	}
	void on_close(connection_hdl hdl) {
        holdConnections.erase(hdl); //remove connection from map
	}
	void on_message(connection_hdl hdl, server::message_ptr msg) {//make sure to start a new thread for long running processes!
        auto send_message=[&](const std::string& message){
            m_server.send(hdl,message, websocketpp::frame::opcode::text);
        };
        textParser->parse(msg->get_payload(), send_message);
        std::cout<<msg->get_payload()<<std::endl;
        if(textParser->noError){
            std::thread* myThread=new std::thread([&](){return longRunningTask::run(holdConnections[hdl], textParser->json, send_message);});
            holdThreads.push_back(myThread);
        }
        
    }
	void run(uint16_t port, parser* parseFunction,  std::string& on_open_message_) {//parseFunction must be a class that has a function "parse" which takes a string and a function to run when an error occurs
        textParser=parseFunction;
        on_open_message=on_open_message_;
		m_server.listen(port);
		m_server.start_accept();
		m_server.run();
	}
    ~WS(){
        for(auto& i : holdThreads){ //delete old threads
            i->join();
            delete i;
        }
        holdThreads.clear();
    }
};
int main(){
    std::string schemaJson="{\"$schema\":\"http://json-schema.org/draft-04/schema#\",\"id\":\"http://jsonschema.net\",\"type\":\"object\",\"properties\":{\"address\":{\"id\":\"http://jsonschema.net/address\",\"type\":\"object\",\"properties\":{\"streetAddress\":{\"id\":\"http://jsonschema.net/address/streetAddress\",\"type\":\"string\"},\"city\":{\"id\":\"http://jsonschema.net/address/city\",\"type\":\"string\"}, \"dateArrived\":{\"id\":\"http://jsonschema.net/address/dateArrived\",\"type\":\"string\", \"format\":\"date\"}},\"required\":[\"streetAddress\",\"city\"]},\"phoneNumber\":{\"id\":\"http://jsonschema.net/phoneNumber\",\"type\":\"array\",\"items\":{\"id\":\"http://jsonschema.net/phoneNumber/0\",\"type\":\"object\",\"properties\":{\"location\":{\"id\":\"http://jsonschema.net/phoneNumber/0/location\",\"type\":\"string\"},\"code\":{\"id\":\"http://jsonschema.net/phoneNumber/0/code\",\"type\":\"integer\"}}}}},\"required\":[\"address\",\"phoneNumber\"]}";
    //see http://jsonschema.net/ for automatic schema generator
    //example JSON that satisifies this schema: "{  \"address\": { \"streetAddress\": \"21 2nd Street\", \"city\": \"New York\" },\"phoneNumber\": [   {  \"location\": \"home\", \"code\": 44  }]}";
    WS<Parser, Task> server;
    Parser jsonParser;
    jsonParser.parseSchema(schemaJson, [&](const std::string& error){std::cout<<error<<std::endl;});
    server.run(9000, &jsonParser, schemaJson);//give port to the program
}