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



#include "ServerUtils.h"
#include "make_unique.h"


#include "ClientUtils.h"

using azure::storage::storage_exception;
using azure::storage::cloud_table;
using azure::storage::cloud_table_client;
using azure::storage::edm_type;
using azure::storage::entity_property;
using azure::storage::table_entity;
using azure::storage::table_operation;
using azure::storage::table_query;
using azure::storage::table_query_iterator;
using azure::storage::table_request_options;
using azure::storage::table_result;
using azure::storage::table_shared_access_policy;

using std::cin;
using std::cout;
using std::endl;
using std::getline;
using std::make_pair;
using std::pair;
using std::string;
using std::unordered_map;
using std::vector;

using web::http::http_headers;
using web::http::http_request;
using web::http::methods;
using web::http::status_code;
using web::http::status_codes;
using web::http::uri;

using web::json::value;

using web::http::experimental::listener::http_listener;

using prop_vals_t = vector<pair<string,value>>;
using prop_str_vals_t = vector<pair<string,string>>;

constexpr const char* def_url = "http://localhost:34574";
const string auth_url = "http://localhost:34570/";
const string basic_url = "http://localhost:34568/";
const string auth_table_name {"AuthTable"};
//The table storing userID and password data. The table has only one partition,userIDin which
// all entities are placed. The row key is the userID.
const string auth_table_userid_partition {"Userid"};
const string auth_table_password_prop {"Password"}; //password for userid
const string auth_table_partition_prop {"DataPartition"}; //combined with datarow,
// the key for the single entity that this userid can access in DataTable.
const string auth_table_row_prop {"DataRow"};
const string data_table_name {"DataTable"};
//the table whose access is controlled by the authentication server.

const string push_status {"PushStatus"};
const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};

/*
  Cache of opened tables
 */


/*
  Convert properties represented in Azure Storage type
  to prop_str_vals_t type.
 */
// prop_str_vals_t get_string_properties (const table_entity::properties_type& properties) {
//   prop_str_vals_t values {};
//   for (const auto v : properties) {
//     if (v.second.property_type() == edm_type::string) {
//       values.push_back(make_pair(v.first,v.second.string_value()));
//     }
//     else {
//       // Force the value as string in any case
//       values.push_back(make_pair(v.first, v.second.str()));
//     }
//   }
//   return values;
// }

value build_json_object (const vector<pair<string,string>>& properties) {
    value result {value::object ()};
    for (auto& prop : properties) {
      result[prop.first] = value::string(prop.second);
    }
    return result;
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
  Return a token for 24 hours of access to the specified table,
  for the single entity defind by the partition and row.

  permissions: A bitwise OR ('|')  of table_shared_access_poligy::permission
    constants.

    For read-only:
      table_shared_access_policy::permissions::read
    For read and update:
      table_shared_access_policy::permissions::read |
      table_shared_access_policy::permissions::update
 */
// pair<status_code,string> do_get_token (const cloud_table& data_table,
//                    const string& partition,
//                    const string& row,
//                    uint8_t permissions) {
//
//   utility::datetime exptime {utility::datetime::utc_now() + utility::datetime::from_days(1)};
//   try {
//     string limited_access_token {
//       data_table.get_shared_access_signature(table_shared_access_policy {
//                                                exptime,
//                                                permissions},
//                                              string(), // Unnamed policy
//                                              // Start of range (inclusive)
//                                              partition,
//                                              row,
//                                              // End of range (inclusive)
//                                              partition,
//                                              row)
//         // Following token allows read access to entire table
//         //table.get_shared_access_signature(table_shared_access_policy {exptime, permissions})
//       };
//     cout << "Token " << limited_access_token << endl;
//     return make_pair(status_codes::OK, limited_access_token);
//   }
//   catch (const storage_exception& e) {
//     cout << "Azure Table Storage error: " << e.what() << endl;
//     cout << e.result().extended_error().message() << endl;
//     return make_pair(status_codes::InternalError, string{});
//   }
// }

void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
  auto paths = uri::split_path(path);
  //need at least 4 arguments
  if (paths.size() < 4) {
    message.reply(status_codes::BadRequest);
    return;
  }

  if(paths[0] == push_status){
    string user_country {paths[1]};
    string user_name {paths[2]};
    string status {paths[3]};
    string friends_list {""};

    unordered_map<string,string> json_body {get_json_body (message)};
    for(const auto v : json_body){
      friends_list = v.second;
    }


    friends_list_t parsed_friends_list = parse_friends_list(friends_list);
    for(const auto v : parsed_friends_list){//v.first == country v.second == name
      //get old updates
      pair<status_code,value> result { do_request (methods::GET,
                    basic_url + read_entity_admin + "/" + data_table_name + "/" + v.second + "/" + v.first)};
      //get property value of Updates
      string old_updates = get_json_object_prop( result.second, "Updates");
      string new_updates {old_updates + status + "\n"};//Concatenate new updates to old updates
      //build json object to pass to do_request
      value new_updates_object {build_json_object (vector<pair<string,string>> {make_pair("Updates", new_updates)})};
      //do_request to update entity to DataTable
      pair<status_code,value> result2 { do_request (methods::PUT,
                  basic_url + update_entity_admin + "/" + data_table_name + "/" + v.second + "/" + v.first,
                  value::object (vector<pair<string,value>>
                                 {make_pair("Updates", value::string(status))}))};
    }
    message.reply(status_codes::OK);
    return;
  }else{
    message.reply(status_codes::BadRequest);
    return;
  }


}

int main (int argc, char const * argv[]) {
  cout << "PushServer: Parsing connection string" << endl;

  cout << "PushServer: Opening listener" << endl;
  http_listener listener {def_url};
  //listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  //listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop PushServer." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "PushServer closed" << endl;
}
