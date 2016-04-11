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
using std::get;
using std::make_tuple;

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
const string push_def_url = "http://localhost:34574";

const string create_table {"CreateTableAdmin"};
const string delete_table {"DeleteTableAdmin"};
const string update_entity {"UpdateEntityAdmin"};
const string delete_entity {"DeleteEntityAdmin"};
const string Add_Property {"AddPropertyAdmin"};
const string Update_Property {"UpdatePropertyAdmin"};
const string read_entity {"ReadEntityAdmin"};
const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string get_read_token_op {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};
const string get_update_data_op {"GetUpdateData"};

const string sign_off {"SignOff"};
const string sign_on {"SignOn"};
const string read_friend_list {"ReadFriendList"};
const string update_status {"UpdateStatus"};
const string push_status {"PushStatus"};
const string add_friend_user{"AddFriend"};
const string un_friend_user{"UnFriend"};

const string data_table_name {"DataTable"};
const string auth_table_name {"AuthTable"};

const string data_partition_prop {"DataPartition"};
const string data_row_prop {"DataRow"};
const string password_prop {"Password"};
const string token_prop {"token"};

const string auth_table_partition {"Userid"};

// Unordered map of users currently signed in
unordered_map<string,tuple<string,string,string>> usersSignedIn;

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

  // Store userid parameter
  string userid_name {paths[1]};

  // To store password from request
  vector<string> passwordInRequest;

  // Flag for user in usersSignedIn
  bool userFound {false};

  // Look for user in usersSignedIn
  for ( auto it = usersSignedIn.begin(); it != usersSignedIn.end(); ++it ) {
    if ( it->first == userid_name ) {
      userFound = true;
    }
  }

  if ( paths[0] == sign_on ) {

    unordered_map<string,string> json_body {get_json_body (message)};

    // Store password from given JSON body
    for( auto v = json_body.begin(); v != json_body.end(); ++v ) {
      // If JSON body has property "Password" then store the password
      if (v->first == password_prop) {
        // Adds password to vector
        passwordInRequest.push_back(v->second);
      }
    }

    pair<string,string> passwordPairToSend {
      make_pair( password_prop, passwordInRequest[0] ) };

    value passwordObjectToSend { build_json_value( passwordPairToSend ) };

    // If user is not signed in, then attempt to sign them on
    if ( !userFound ) {

      // Send a GetUpdateData request to AuthServer
      pair<status_code,value> updateData {
                 do_request (methods::GET,
              		    auth_def_url + "/"
                    + get_update_data_op + "/"
              		  + auth_table_name + "/"
              		  + auth_table_partition + "/"
              		  + userid_name,
                      passwordObjectToSend)
                  };

      if ( status_codes::NotFound == updateData.first) {
        message.reply(status_codes::NotFound);
      }

      unordered_map<string,string> updateDataJSONBody {
        unpack_json_object( updateData.second )
      };

      // Iterators that point to their respective data
      // in updateDataTokenJSONBody
      unordered_map<string,string>::const_iterator dataToken {
        updateDataJSONBody.find(token_prop) };
      unordered_map<string,string>::const_iterator dataPartition {
        updateDataJSONBody.find(data_partition_prop) };
      unordered_map<string,string>::const_iterator dataRow {
        updateDataJSONBody.find(data_row_prop) };

      // Send a ReadEntityAuth request to BasicServer
      pair<status_code,value> user_in_data_table {
                 do_request (methods::GET,
              		    basic_def_url + "/"
                    + read_entity_auth + "/"
              		  + data_table_name + "/"
                    + dataToken->second + "/"
              		  + dataPartition->second + "/"
              		  + dataRow->second)
                  };

      if ( status_codes::NotFound == user_in_data_table.first) {
        message.reply(status_codes::NotFound);
      }

      // Once token has been created and user is confirmed to be in data table,
      // add user to usersSignedIn
      usersSignedIn.insert(
        {userid_name, make_tuple( dataToken->second,
                          dataPartition->second, dataRow->second )} );

    }

    // User is already signed in
    else {

      // Check if given password matches the one in auth table
      // Don't have to check for status code b/c user has to be in auth table
      // if they are already signed in
      pair<status_code,value> passwordCheck {
                 do_request (methods::GET,
              		    auth_def_url + "/"
                    + read_entity + "/"
              		  + auth_table_name + "/"
              		  + auth_table_partition + "/"
              		  + userid_name)
                  };
      unordered_map<string,string> passwordCheckBody {
        unpack_json_object( passwordCheck.second )
      };

      unordered_map<string,string>::const_iterator passwordInAuthTable {
        passwordCheckBody.find(password_prop) };

      if ( passwordInRequest[0] == passwordInAuthTable->second ) {
        message.reply(status_codes::OK);
      }
      else {
        message.reply(status_codes::NotFound);
      }
    }
  }

  else if ( paths[0] == sign_off ) {

    // Check if user is signed in
    auto isUserSignedIn {usersSignedIn.find( userid_name )};

    if( !userFound ) {
      message.reply(status_codes::NotFound);
    }

    auto isUserRemoved {usersSignedIn.erase( userid_name )};

    if( isUserRemoved.size() == 1 ) {
      message.reply(status_codes::OK);
    }
  }

}

/*
  Top-level routine for processing all HTTP GET requests.
 */
void handle_get(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** GET " << path << endl;
  auto paths = uri::split_path(path);

  if (paths.size() < 2){
    message.reply(status_codes::BadRequest);
    return;
  }

  string user_id {paths[1]};

  if(paths[0] == read_friend_list){
    auto isUserSignedIn = usersSignedIn.find(user_id);

    if (isUserSignedIn == usersSignedIn.end()){
      message.reply(status_codes::Forbidden);
      return;
    }
    //if user signed in, get friend list
    else{
      string dataToken = get<0>(usersSignedIn[user_id]);
      string dataPartition = get<1>(usersSignedIn[user_id]);
      string dataRow = get<2>(usersSignedIn[user_id]);

      pair<status_code,value> result {
        do_request(methods::GET, basic_def_url + read_entity_auth + "/" +
        data_table_name + "/" + dataToken + "/" + dataPartition + "/" + dataRow)
      };

      string friends_list = get_json_object_prop(result.second, "Friends");
      value json_friends {build_json_value (vector<pair<string,string>> {make_pair("Friends", friends_list)})};
      message.reply(status_codes::OK, json_friends);
      return;
    }
  }
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
  auto paths = uri::split_path(path);

  if (paths.size() < 3){
    message.reply(status_codes::BadRequest);
    return;
  }

  string user_id {paths[1]};
  string status {paths[2]};

  if(paths[0] == update_status){
    auto isUserSignedIn = usersSignedIn.find(user_id);

    if (isUserSignedIn == usersSignedIn.end()){
      message.reply(status_codes::Forbidden);
      return;
    }
    // If user signed in, update status
    else{
      string dataToken = get<0>(usersSignedIn[user_id]);
      string dataPartition = get<1>(usersSignedIn[user_id]);
      string dataRow = get<2>(usersSignedIn[user_id]);

      pair<status_code,value> result {
        do_request(methods::GET, basic_def_url + read_entity_auth + "/" +
        data_table_name + "/" + dataToken + "/" + dataPartition + "/" + dataRow)
      };

      value json_status {build_json_value (vector<pair<string,string>> {make_pair("Status", status)})};
      pair<status_code,value> result2 {
        do_request(methods::PUT, basic_def_url + update_entity_auth + "/" +
        data_table_name + "/" + dataToken + "/" + dataPartition + "/" + dataRow, json_status)
      };

      string friends_list = get_json_object_prop(result.second, "Friends");
      value json_friends {build_json_value (vector<pair<string,string>> {make_pair("Friends", friends_list)})};
      pair<status_code,value> result3 {
        do_request(methods::POST, push_def_url + push_status + "/" +
        dataPartition + "/" + dataRow + "/" + status, json_friends)
      };
      message.reply(status_codes::OK);
      return;
    }
  }
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
    auto check_signed = usersSignedIn.find( user_id );
    if( check_signed == usersSignedIn.end() ){
      //not signed-in
      message.reply(status_codes::Forbidden);
      return;
    }

    else{ //user is signed-in
      string friend_token = get<0>(usersSignedIn[user_id]);
      string friend_partition = get<1>(usersSignedIn[user_id]);
      string friend_row = get<2>(usersSignedIn[user_id]);

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
          friends_list_val.erase(friends_list_val.begin()+i);
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
