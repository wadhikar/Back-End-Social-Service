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

const string get_read_token_op {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};
const string get_update_data_op {"GetUpdateData"};

const string sign_off {"SignOff"};
const string sign_on {"SignOn"};

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
