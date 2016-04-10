/*
 User Server code for CMPT 276, Spring 2016.
 */

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cpprest/base_uri.h>
#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <pplx/pplxtasks.h>

#include <was/common.h>
#include <was/storage_account.h>
#include <was/table.h>

//#include "config.h"
#include "ServerUtils.h"
#include "ClientUtils.h"
#include "make_unique.h"

#include "azure_keys.h"

using azure::storage::cloud_storage_account;
using azure::storage::storage_credentials;
using azure::storage::storage_exception;
using azure::storage::cloud_table;
using azure::storage::cloud_table_client;
using azure::storage::edm_type;
using azure::storage::entity_property;
using azure::storage::table_entity;
using azure::storage::table_operation;
using azure::storage::table_query;
using azure::storage::table_query_iterator;
using azure::storage::table_result;

using pplx::extensibility::critical_section_t;
using pplx::extensibility::scoped_critical_section_t;

using std::cin;
using std::cout;
using std::endl;
using std::getline;
using std::make_pair;
using std::pair;
using std::string;
using std::unordered_map;
using std::vector;
using std::tuple;

using web::http::http_headers;
using web::http::http_request;
using web::http::methods;
using web::http::status_codes;
using web::http::status_code;
using web::http::uri;

using web::json::value;

using web::http::experimental::listener::http_listener;

using prop_vals_t = vector<pair<string,value>>;

constexpr const char* user_def_url = "http://localhost:34568";

const string auth_def_url = "http://localhost:34570";
const string basic_def_url = "http://localhost:34568";

const string create_table {"CreateTableAdmin"};
const string delete_table {"DeleteTableAdmin"};
const string update_entity {"UpdateEntityAdmin"};
const string delete_entity {"DeleteEntityAdmin"};
const string Add_Property {"AddPropertyAdmin"};
const string Update_Property {"UpdatePropertyAdmin"};
const string read_entity {"ReadEntityAdmin"};
const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string data_table_name {"DataTable"};
const string auth_table_name {"AuthTable"};

const string auth_table_partition {"Userid"}


/////////////////////////////////////////////////////////////
const string add_friend_user{"AddFriend"};
const string un_friend_user{"UnFriend"};

/*
  Convert properties represented in Azure Storage type
  to prop_vals_t type.
 */
prop_vals_t get_properties (const table_entity::properties_type& properties, prop_vals_t values = prop_vals_t {}) {
  for (const auto v : properties) {
    if (v.second.property_type() == edm_type::string) {
      values.push_back(make_pair(v.first, value::string(v.second.string_value())));
    }
    else if (v.second.property_type() == edm_type::datetime) {
      values.push_back(make_pair(v.first, value::string(v.second.str())));
    }
    else if(v.second.property_type() == edm_type::int32) {
      values.push_back(make_pair(v.first, value::number(v.second.int32_value())));
    }
    else if(v.second.property_type() == edm_type::int64) {
      values.push_back(make_pair(v.first, value::number(v.second.int64_value())));
    }
    else if(v.second.property_type() == edm_type::double_floating_point) {
      values.push_back(make_pair(v.first, value::number(v.second.double_value())));
    }
    else if(v.second.property_type() == edm_type::boolean) {
      values.push_back(make_pair(v.first, value::boolean(v.second.boolean_value())));
    }
    else {
      values.push_back(make_pair(v.first, value::string(v.second.str())));
    }
  }
  return values;
}

/*
  Given an HTTP message with a JSON body, return the JSON
  body as an unordered map of strings to strings.

  Note that all types of JSON values are returned as strings.
  Use C++ conversion utilities to convert to numbers or dates
  as necessary.
 */
unordered_map<string,string> get_json_body(http_request message) {
  unordered_map<string,string> results {};
  const http_headers& headers {message.headers()};
  auto content_type (headers.find("Content-Type"));
  if (content_type == headers.end() ||
      content_type->second != "application/json")
    return results;

  value json{};
  message.extract_json(true)
    .then([&json](value v) -> bool
	  {
            json = v;
	    return true;
	  })
    .wait();

  if (json.is_object()) {
    for (const auto& v : json.as_object()) {
      if (v.second.is_string()) {
	results[v.first] = v.second.as_string();
      }
      else {
	results[v.first] = v.second.serialize();
      }
    }
  }
  return results;
}

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least an operation and user id
  if (paths.size() < 2) {
    message.reply(status_codes::BadRequest);
    return;
  }

  unordered_map<string,string> json_body {get_json_body (message)};

  // Store userid parameter
  string userid_name {paths[1]};
  // Look up AuthTable
  cloud_table table {table_cache.lookup_table(auth_table_name)};

  string dataPartition;
  string dataRow;
  string passwordFromBody;
  vector<string> passwordInTable;
  table_query query {};
  table_query_iterator end;
  table_query_iterator it = table.execute_query(query);

  unordered_map<string,tuple> usersSignedIn;

  // Store password from given JSON body
  for( auto v = json_body.begin(); v != json_body.end(); ++v ) {
    // If JSON body has property "Password" then store the password
    if (v->first == auth_table_password_prop) {
      // Adds password to vector
      passwordInTable.push_back(v->second);
    }
  }

  // Loop through properties to store user's Partition and Row for looking up
  // in DataTable
  while( it != end ){
    const table_entity::properties_type& propertyPWD {it->properties()};
    for( auto v = propertyPWD.begin(); v != propertyPWD.end(); ++v ) {

      if (v->first == auth_table_password_prop ) {
        passwordFromBody = v->second.str();
      }

      if ( v->first == auth_table_partition_prop ) {
        dataPartition = v->second.str();
      }
      if ( v->first == auth_table_row_prop ){
        dataRow = v->second.str();
      }
    }
  }

  // Send a GetReadToken request to AuthServer
  pair<status_code,value> updateToken {
             do_request (methods::GET,
          		    auth_def_url
          		  + auth_table_name + "/"
          		  + auth_table_partition + "/"
          		  + userid_name)
              };

  if ( status_codes::NotFound == updateToken.first) {
    message.reply(status_codes::NotFound);
  }

  // Send a ReadEntityAuth request to BasicServer
  pair<status_code,value> user_in_data_table {
             do_request (methods::GET,
          		    basic_def_url
          		  + data_table_name + "/"
                + updateToken + "/"
          		  + dataPartition + "/"
          		  + dataRow)
              };
  if ( status_codes::NotFound == user_in_data_table.first) {
    message.reply(status_codes::NotFound);
  }

  // Check to see if user has signed in previously
  auto isUserSignedIn {usersSignedIn.find( userid_name )};

  // If not signed in, add to usersSignedIn
  if ( isUserSignedIn == usersSignedIn.end() ) {
    usersSignedIn.insert( {userid_name,
                            make_tuple( updateToken, dataPartition, dataRow )} );
  }
  // If already signed in but password in JSON body is incorrect
  else {
    if ( passwordFromBody != passwordInTable[0] ) {
      message.reply(status_codes::NotFound);
    }
  }
}

//************* Handle_Put Function here

void handle_put(http_request message){
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
  auto paths = uri::split_path(path);

//*********ADDFRIEND OPERATION**********
  if(paths[0] == add_friend_user){
    if(paths.size() != 4){ //Operation, Userid, friend country, and full friend name
      message.reply(status_codes::BadRequest);
      return;
    }
    string user_id {paths[1]};
    string friend_country{paths[2]};
    string friend_full_name{paths[3]};

    //check if user is signed in
    auto check_signed = usersSignedIn.find(paths[1]);
    if(check_signed = usersSignedIn.end){
      //not signed-in
      message.reply(status_codes::Forbidden);
      return;
    }

    else{ //user is signed-in
      string friend_token = {get<0>(usersSignedIn[user_id])};
      string friend_partition = {get<1>(usersSignedIn[user_id])};
      string friend_row = {get<2>(usersSignedIn[user_id])};

      pair<status_code,value> result {
        do_request(methods::GET, basic_def_url + read_entity_auth+"/"+
          data_table_name+"/"+friend_token+"/"+friend_partition+"/"+friend_row)
      };

      string friend_list = get_json_object_prop(result.second, "Friends");

      //getting a vector of country, name pairs
      friends_list_t friends_list_val = parse_friends_list(friend_list);

      bool checker = false;

      for(int i = 0; i < friends_list_val.size(); ++i){
        if(friends_list_val[i].first == friend_country && friends_list_val[i].second == friend_full_name){
          cout<<"Already friend" << endl;
          checker = true;
        }
      }
      if(checker == true){
        //already friends
        //return OK anyways
        message.reply(status_codes::OK);
        return;
      }

      //At this point, new friend is not already in the friends list
      friends_list_val.push_back(make_pair(friend_country,friend_full_name));

      //converting new friends list to string to replace the old friends list value
      string friend_list_new = friends_list_to_string(friends_list_val);

      value friend_json_object {build_json_value (vector<pair<string,string>>{make_pair("Friends",friend_list_new)})};

      pair<status_code,value> result_a{
        do_request(methods::PUT, basic_def_url + update_entity_auth +"/"+
          data_table_name + "/" + friend_token + "/" + friend_partition + "/"+ friend_row,friend_json_object)
      };

      //Successfully added as friend
      message.reply(status_codes::OK);
      return;
    }
  } // END OF ADDFRIEND

//**************UNFRIEND OPERATION******************
  if(paths[0] == un_friend_user){

    if(paths.size() != 4){
      message.reply(status_codes::BadRequest);
      return;
    }

    string unfriend_userid {paths[1]};
    string unfriend_country{paths[2]};
    string unfriend_full_name{paths[3]};

    auto check_signed = usersSignedIn.find(paths[1]);
    if(check_signed == usersSignedIn.end()){
      //User is not signed in
      message.reply(status_codes::Forbidden);
    }
    else{//user signed-in

      string unfriend_token = {get<0>(usersSignedIn[user_id])};
      string unfriend_partition = {get<1>(usersSignedIn[user_id])};
      string unfriend_row = {get<2>(usersSignedIn[user_id])};

      //Checking if the friend exist
      pair<status_code,value> check{
        do_request(methods::GET, basic_def_url + read_entity_auth +"/"+
        data_table_name+"/"+unfriend_country+"/"+unfriend_full_name)
      };

      if(check.first == status_codes::NotFound){
        message.reply(status_codes::NotFound);
        return;
      }

      pair<status_code,value> result {
        do_request(methods::GET, basic_def_url + read_entity_auth+"/"+
          data_table_name+"/"+unfriend_token+"/"+unfriend_partition+"/"+unfriend_row)
      };

      string friend_list = get_json_object_prop(result.second, "Friends");

      friends_list_t friends_list_val = parse_friends_list(friend_list);

      bool checker = false;

      for(int i = 0; i < friends_list_val.size(); ++i){
        if(friends_list_val[i].first == unfriend_country && friends_list_val[i].second == unfriend_full_name){
          //friend found
          friend_list_val.erase(friends_list_val.begin()+i);
          checker = true;
        }
      }
      if(checker == false){
        //friend doesnt exist
        cout << "Friend Does Not Exist" << endl;
        //return OK anyways
        message.reply(status_codes::OK);
        return;
      }
      else{
        ///////*************** NOT SURE ABOUT THIS PART YET****************
        string friend_list_new = friends_list_to_string(friends_list_val);
        value friend_json_object {build_json_value (vector<pair<string,string>>{make_pair("Friends",friend_list_new)})};
        pair<status_code,value> result_a{
          do_request(methods::PUT, basic_def_url + update_entity_auth +"/"+
            data_table_name + "/" + unfriend_token + "/" + unfriend_partition + "/"+ unfriend_row,friend_json_object)
        };
        //successfully un-friended
        message.reply(status_codes::OK);
        return;
      }
    }
  } // END OF UNFRIEND
}

/*
  Main server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.

  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {

  http_listener listener {user_def_url};
  listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  listener.support(methods::PUT, &handle_put);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop server." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "Closed" << endl;
}
