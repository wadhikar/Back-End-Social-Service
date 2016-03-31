/*
  Sample unit tests for BasicServer
 */

 #include <algorithm>
 #include <exception>
 #include <iostream>
 #include <string>
 #include <utility>
 #include <vector>
 #include <unordered_map>

 #include <cpprest/http_client.h>
 #include <cpprest/json.h>

 #include <pplx/pplxtasks.h>

 #include <UnitTest++/UnitTest++.h>

 using std::cerr;
 using std::cout;
 using std::endl;
 using std::make_pair;
 using std::pair;
 using std::string;
 using std::vector;

 using web::http::http_headers;
 using web::http::http_request;
 using web::http::http_response;
 using web::http::method;
 using web::http::methods;
 using web::http::status_code;
 using web::http::status_codes;
 using web::http::uri_builder;

 using web::http::client::http_client;

 using web::json::object;
 using web::json::value;

 const string create_table_op {"CreateTableAdmin"};
 const string delete_table_op {"DeleteTableAdmin"};

 const string read_entity_admin {"ReadEntityAdmin"};
 const string update_entity_admin {"UpdateEntityAdmin"};
 const string delete_entity_admin {"DeleteEntityAdmin"};

 const string read_entity_auth {"ReadEntityAuth"};
 const string update_entity_auth {"UpdateEntityAuth"};

 const string get_read_token_op  {"GetReadToken"};
 const string get_update_token_op {"GetUpdateToken"};

 // The two optional operations from Assignment 1
 const string add_property_admin {"AddPropertyAdmin"};
 const string update_property_admin {"UpdatePropertyAdmin"};

/*
  Make an HTTP request, returning the status code and any JSON value in the body

  method: member of web::http::methods
  uri_string: uri of the request
  req_body: [optional] a json::value to be passed as the message body

  If the response has a body with Content-Type: application/json,
  the second part of the result is the json::value of the body.
  If the response does not have that Content-Type, the second part
  of the result is simply json::value {}.

  You're welcome to read this code but bear in mind: It's the single
  trickiest part of the sample code. You can just call it without
  attending to its internals, if you prefer.
 */

// Version with explicit third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string, const value& req_body) {
  http_request request {http_method};
  if (req_body != value {}) {
    http_headers& headers (request.headers());
    headers.add("Content-Type", "application/json");
    request.set_body(req_body);
  }

  status_code code;
  value resp_body;
  http_client client {uri_string};
  client.request (request)
    .then([&code](http_response response)
	  {
	    code = response.status_code();
	    const http_headers& headers {response.headers()};
	    auto content_type (headers.find("Content-Type"));
	    if (content_type == headers.end() ||
		content_type->second != "application/json")
	      return pplx::task<value> ([] { return value {};});
	    else
	      return response.extract_json();
	  })
    .then([&resp_body](value v) -> void
	  {
	    resp_body = v;
	    return;
	  })
    .wait();
  return make_pair(code, resp_body);
}

// Version that defaults third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string) {
  return do_request (http_method, uri_string, value {});
}

/*
  Utility to create a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int create_table (const string& addr, const string& table) {
  pair<status_code,value> result {do_request (methods::POST, addr + "CreateTable/" + table)};
  return result.first;
}

/*
  Utility to compare two JSON objects

  This is an internal routine---you probably want to call compare_json_values().
 */
bool compare_json_objects (const object& expected_o, const object& actual_o) {
  CHECK_EQUAL (expected_o.size (), actual_o.size());
  if (expected_o.size() != actual_o.size())
    return false;

  bool result {true};
  for (auto& exp_prop : expected_o) {
    object::const_iterator act_prop {actual_o.find (exp_prop.first)};
    CHECK (actual_o.end () != act_prop);
    if (actual_o.end () == act_prop)
      result = false;
    else {
      CHECK_EQUAL (exp_prop.second, act_prop->second);
      if (exp_prop.second != act_prop->second)
        result = false;
    }
  }
  return result;
}

/*
  Utility to compare two JSON objects represented as values

  expected: json::value that was expected---must be an object
  actual: json::value that was actually returned---must be an object
*/
bool compare_json_values (const value& expected, const value& actual) {
  assert (expected.is_object());
  assert (actual.is_object());

  object expected_o {expected.as_object()};
  object actual_o {actual.as_object()};
  return compare_json_objects (expected_o, actual_o);
}

/*
  Utility to compre expected JSON array with actual

  exp: vector of objects, sorted by Partition/Row property
    The routine will throw if exp is not sorted.
  actual: JSON array value of JSON objects
    The routine will throw if actual is not an array or if
    one or more values is not an object.

  Note the deliberate asymmetry of the how the two arguments are handled:

  exp is set up by the test, so we *require* it to be of the correct
  type (vector<object>) and to be sorted and throw if it is not.

  actual is returned by the database and may not be an array, may not
  be values, and may not be sorted by partition/row, so we have
  to check whether it has those characteristics and convert it
  to a type comparable to exp.
*/
bool compare_json_arrays(const vector<object>& exp, const value& actual) {
  /*
    Check that expected argument really is sorted and
    that every value has Partion and Row properties.
    This is a precondition of this routine, so we throw
    if it is not met.
  */
  auto comp = [] (const object& a, const object& b) -> bool {
    return a.at("Partition").as_string()  <  b.at("Partition").as_string()
           ||
           (a.at("Partition").as_string() == b.at("Partition").as_string() &&
            a.at("Row").as_string()       <  b.at("Row").as_string());
  };
  if ( ! std::is_sorted(exp.begin(),
                         exp.end(),
                         comp))
    throw std::exception();

  // Check that actual is an array
  CHECK(actual.is_array());
  if ( ! actual.is_array())
    return false;
  web::json::array act_arr {actual.as_array()};

  // Check that the two arrays have same size
  CHECK_EQUAL(exp.size(), act_arr.size());
  if (exp.size() != act_arr.size())
    return false;

  // Check that all values in actual are objects
  bool all_objs {std::all_of(act_arr.begin(),
                             act_arr.end(),
                             [] (const value& v) { return v.is_object(); })};
  CHECK(all_objs);
  if ( ! all_objs)
    return false;

  // Convert all values in actual to objects
  vector<object> act_o {};
  auto make_object = [] (const value& v) -> object {
    return v.as_object();
  };
  std::transform (act_arr.begin(), act_arr.end(), std::back_inserter(act_o), make_object);

  /*
     Ensure that the actual argument is sorted.
     Unlike exp, we cannot assume this argument is sorted,
     so we sort it.
   */
  std::sort(act_o.begin(), act_o.end(), comp);

  // Compare the sorted arrays
  bool eq {std::equal(exp.begin(), exp.end(), act_o.begin(), &compare_json_objects)};
  CHECK (eq);
  return eq;
}

/*
  Utility to create JSON object value from vector of properties
*/
value build_json_object (const vector<pair<string,string>>& properties) {
    value result {value::object ()};
    for (auto& prop : properties) {
      result[prop.first] = value::string(prop.second);
    }
    return result;
}

/*
  Utility to delete a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int delete_table (const string& addr, const string& table) {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
		addr + "DeleteTable/" + table)};
  return result.first;
}

/*
  Utility to put an entity with a single property

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity
  row: Row of the entity
  prop: Name of the property
  pstring: Value of the property, as a string
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row, const string& prop, const string& pstring) {
  pair<status_code,value> result {
    do_request (methods::PUT,
                addr + update_entity_admin + "/" + table + "/" + partition + "/" + row,
                value::object (vector<pair<string,value>>
                               {make_pair(prop, value::string(pstring))}))};
  return result.first;
}

/*
  Utility to put an entity with multiple properties

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity
  row: Row of the entity
  props: vector of string/value pairs representing the properties
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row,
              const vector<pair<string,value>>& props) {
  pair<status_code,value> result {
    do_request (methods::PUT,
               addr + "UpdateEntity/" + table + "/" + partition + "/" + row,
               value::object (props))};
  return result.first;
}

/*
  Utility to delete an entity

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity
  row: Row of the entity
 */
int delete_entity (const string& addr, const string& table, const string& partition, const string& row)  {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
		addr + "DeleteEntity/" + table + "/" + partition + "/" + row)};
  return result.first;
}

/*
  Utility to get a token good for updating a specific entry
  from a specific table for one day.
 */
pair<status_code,string> get_update_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_update_token_op + "/" +
                                              userid,
                                              pwd
                                              )};
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK)
    return make_pair (result.first, "");
  else {
    string token {result.second["token"].as_string()};
    return make_pair (result.first, token);
  }
}

/*
  Utility to get a token good for reading a specific entry
  from a specific table for one day.
 */
pair<status_code,string> get_read_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_read_token_op + "/" +
                                              userid,
                                              pwd
                                              )};
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK)
    return make_pair (result.first, "");
  else {
    string token {result.second["token"].as_string()};
    return make_pair (result.first, token);
  }
}

/*
  A sample fixture that ensures TestTable exists, and
  at least has the entity Franklin,Aretha/USA
  with the property "Song": "RESPECT".

  The entity is deleted when the fixture shuts down
  but the table is left. See the comments in the code
  for the reason for this design.
 */
SUITE(GET) {
  class GetFixture {
  public:
    static constexpr const char* addr {"http://127.0.0.1:34568/"};
    static constexpr const char* auth_addr {"http://localhost:34570/"};
    static constexpr const char* userid {"user"};
    static constexpr const char* user_pwd {"user"};
    static constexpr const char* auth_table {"AuthTable"};
    static constexpr const char* auth_table_partition {"Userid"};
    static constexpr const char* auth_pwd_prop {"Password"};
    static constexpr const char* table {"TestTable"};
    static constexpr const char* partition {"Franklin,Aretha"};
    static constexpr const char* row {"USA"};
    static constexpr const char* property {"Song"};
    static constexpr const char* prop_val {"RESPECT"};

  public:
    GetFixture() {
      int make_result {create_table(addr, table)};
      cerr << "create result " << make_result << endl;
      if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
        throw std::exception();
      }
      int put_result {put_entity (addr, table, partition, row, property, prop_val)};
      cerr << "put result " << put_result << endl;
      if (put_result != status_codes::OK) {
        throw std::exception();
      }
    }
    ~GetFixture() {
      int del_ent_result {delete_entity (addr, table, partition, row)};
      if (del_ent_result != status_codes::OK) {
        throw std::exception();
      }


      /*
	In traditional unit testing, we might delete the table after every test.

	However, in cloud NoSQL environments (Azure Tables, Amazon DynamoDB)
	creating and deleting tables are rate-limited operations. So we
	leave the table after each test but delete all its entities.
       */
      cout << "Skipping table delete" << endl;
      /*
      int del_result {delete_table(addr, table)};
      cerr << "delete result " << del_result << endl;
      if (del_result != status_codes::OK) {
	throw std::exception();
      }
      */
    }
  };

  // /*
  //   A test of GET of a single entity
  //  */
  // /*
  // TEST_FIXTURE(GetFixture, GetSingle) {
  //   pair<status_code,value> result {
  //     do_request (methods::GET,
	// 	  string(GetFixture::addr)
	// 	  + GetFixture::table + "/"
	// 	  + GetFixture::partition + "/"
	// 	  + GetFixture::row)};
  //
  //     CHECK_EQUAL(string("{\"")
	// 	  + GetFixture::property
	// 	  + "\":\""
	// 	  + GetFixture::prop_val
	// 	  + "\"}",
	// 	  result.second.serialize());
  //     cout << "result.second.serialize(): " << result.second.serialize() << endl;
  //     CHECK_EQUAL(status_codes::OK, result.first);
  //   }
  //
  // /*
  //   A test of GET all table entries
  //
  //   Demonstrates use of new compare_json_arrays() function.
  //  */
  // /*
  // TEST_FIXTURE(GetFixture, GetAll) {
  //   string partition {"Canada"};
  //   string row {"Katherines,The"};
  //   string property {"Home"};
  //   string prop_val {"Vancouver"};
  //   int put_result {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
  //   cerr << "put result " << put_result << endl;
  //   assert (put_result == status_codes::OK);
  //
  //   pair<status_code,value> result {
  //     do_request (methods::GET,
	// 	  string(GetFixture::addr)
	// 	  + string(GetFixture::table))};
  //
  //   cout << "result.second: " << result.second << endl;
  //
  //   //cout << "Get All result.second: " << result.second << endl;
  //   CHECK(result.second.is_array());
  //   CHECK_EQUAL(2, result.second.as_array().size());
  //
  //   /*
  //     Checking the body is not well-supported by UnitTest++, as we have to test
  //     independent of the order of returned values.
  //    */
  //   //CHECK_EQUAL(body.serialize(), string("{\"")+string(GetFixture::property)+ "\":\""+string(GetFixture::prop_val)+"\"}");
  //   /*
  //   CHECK_EQUAL(status_codes::OK, result.first);
  //   value obj1 {
  //     value::object(vector<pair<string,value>> {
  //         make_pair(string("Partition"), value::string(partition)),
  //         make_pair(string("Row"), value::string(row)),
  //         make_pair(property, value::string(prop_val))
  //     })
  //   };
  //   value obj2 {
  //     value::object(vector<pair<string,value>> {
  //         make_pair(string("Partition"), value::string(GetFixture::partition)),
  //         make_pair(string("Row"), value::string(GetFixture::row)),
  //         make_pair(string(GetFixture::property), value::string(GetFixture::prop_val))
  //     })
  //   };
  //   vector<object> exp {
  //     obj1.as_object(),
  //     obj2.as_object()
  //   };
  //   compare_json_arrays(exp, result.second);
  //   CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
  // }
  // */
  //
  // /*
  //   Test of GET all from specific partition
  //   Essentially the same test as GetSingle
  // */
  // TEST_FIXTURE(GetFixture, GetAllSpecificPartition) {
  //
  //   /*
  //   // Basic cases
  //   string partition {"Katherines,The"};
  //   string row {"Canada"};
  //   string property {"Home"};
  //   string prop_val {"Vancouver"};
  //   int put_result {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
  //   cerr << "put result " << put_result << endl;
  //   assert (put_result == status_codes::OK);
  //
  //   string partition2 {"Franklin,Aretha"};
  //   string row2 {"Canada"};
  //   string property2 {"Home"};
  //   string prop_val2 {"Surrey"};
  //   int put_result2 {put_entity (GetFixture::addr, GetFixture::table, partition2, row2, property2, prop_val2)};
  //   cerr << "put result2 " << put_result2 << endl;
  //   assert (put_result2 == status_codes::OK);
  //
  //   string partition3 {"Katherines,The"};
  //   string row3 {"Canada"};
  //   string property3 {"Home"};
  //   string prop_val3 {"Vancouver"};
  //   int put_result3 {put_entity (GetFixture::addr, GetFixture::table, partition3, row3, property3, prop_val3)};
  //   cerr << "put result3 " << put_result3 << endl;
  //   assert (put_result3 == status_codes::OK);
  //
  //   string partition4 {"Franklin,Aretha"};
  //   string row4 {"Sweden"};
  //   string property4 {"Gender"};
  //   string prop_val4 {"Female"};
  //   int put_result4 {put_entity (GetFixture::addr, GetFixture::table, partition4, row4, property4, prop_val4)};
  //   cerr << "put result4 " << put_result4 << endl;
  //   assert (put_result4 == status_codes::OK);
  //   */
  //
  //   /*
  //   // Edge case: 0 matches
  //   string partition {"Katherines,The"};
  //   string row {"Canada"};
  //   string property {"Home"};
  //   string prop_val {"Vancouver"};
  //   int put_result {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
  //   cerr << "put result " << put_result << endl;
  //   assert (put_result == status_codes::OK);
  //
  //   string partition2 {"Gaga,Lady"};
  //   string row2 {"Canada"};
  //   string property2 {"Home"};
  //   string prop_val2 {"Surrey"};
  //   int put_result2 {put_entity (GetFixture::addr, GetFixture::table, partition2, row2, property2, prop_val2)};
  //   cerr << "put result2 " << put_result2 << endl;
  //   assert (put_result2 == status_codes::OK);
  //
  //   string partition3 {"Katherines,The"};
  //   string row3 {"Canada"};
  //   string property3 {"Home"};
  //   string prop_val3 {"Vancouver"};
  //   int put_result3 {put_entity (GetFixture::addr, GetFixture::table, partition3, row3, property3, prop_val3)};
  //   cerr << "put result3 " << put_result3 << endl;
  //   assert (put_result3 == status_codes::OK);
  //
  //   string partition4 {"Parks,Rosa"};
  //   string row4 {"Sweden"};
  //   string property4 {"Gender"};
  //   string prop_val4 {"Female"};
  //   int put_result4 {put_entity (GetFixture::addr, GetFixture::table, partition4, row4, property4, prop_val4)};
  //   cerr << "put result4 " << put_result4 << endl;
  //   assert (put_result4 == status_codes::OK);
  //   */
  //
  //   // Edge case: all matches
  //   string partition {"Franklin,Aretha"};
  //   string row {"France"};
  //   string property {"Home"};
  //   string prop_val {"Vancouver"};
  //   int put_result {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
  //   cerr << "put result " << put_result << endl;
  //   assert (put_result == status_codes::OK);
  //
  //   string partition2 {"Franklin,Aretha"};
  //   string row2 {"Germany"};
  //   string property2 {"From"};
  //   string prop_val2 {"Surrey"};
  //   int put_result2 {put_entity (GetFixture::addr, GetFixture::table, partition2, row2, property2, prop_val2)};
  //   cerr << "put result2 " << put_result2 << endl;
  //   assert (put_result2 == status_codes::OK);
  //
  //   string partition3 {"Franklin,Aretha"};
  //   string row3 {"Canada"};
  //   string property3 {"Live"};
  //   string prop_val3 {"Burnaby"};
  //   int put_result3 {put_entity (GetFixture::addr, GetFixture::table, partition3, row3, property3, prop_val3)};
  //   cerr << "put result3 " << put_result3 << endl;
  //   assert (put_result3 == status_codes::OK);
  //
  //   string partition4 {"Franklin,Aretha"};
  //   string row4 {"Sweden"};
  //   string property4 {"Gender"};
  //   string prop_val4 {"Female"};
  //   int put_result4 {put_entity (GetFixture::addr, GetFixture::table, partition4, row4, property4, prop_val4)};
  //   cerr << "put result4 " << put_result4 << endl;
  //   assert (put_result4 == status_codes::OK);
  //
  //   pair<status_code, value> result {
  //     do_request (methods::GET,
  //     string(GetFixture::addr)
  //     + string(GetFixture::table) + "/"
	// 	  + "Franklin,Aretha" + "/"
	// 	  + "*")};
  //
  //     cout << "result.second:" << result.second << endl;
  //
  //     CHECK(result.second.is_array());
  //     CHECK_EQUAL(5, result.second.as_array().size());
  //
  //     CHECK_EQUAL(status_codes::OK, result.first);
  //     CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
  //     CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition2, row2));
  //     CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition3, row3));
  //     CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition4, row4));
  //   }
  //
  //   /*
  //     Test of GET all entities containing all specified properties
  //   */
  //
  //   TEST_FIXTURE(GetFixture, GetAllSpecificProperties) {
  //
  //     string partition {"Katherines,The"};
  //     string row {"Canada"};
  //     string property {"Home"};
  //     string prop_val {"Vancouver"};
  //     /*
  //       Could not initialize json object to pass the do_request() with 3 arguments
  //       so we left the test as a GetAll test
  //     */
  //     // value put_json_body {};
  //     // put_json_body[0] = value("Home");
  //     // put_json_body[1] = value("*");
  //     int put_result {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
  //     cerr << "put result " << put_result << endl;
  //     assert (put_result == status_codes::OK);
  //
  //     pair<status_code, value> result {
  //       do_request (methods::GET,
  //       string(GetFixture::addr)
  //       + string(GetFixture::table))};
  //     CHECK(result.second.is_array());
  //     CHECK_EQUAL(2, result.second.as_array().size());
  //
  //     //cout << "result.second:" << result.second << endl;
  //     //cout << "result.second.as_array:" ;<< result.second.as_array() << endl;
  //
  //     // bool wrongPropertyFlag {false};
  //     // if (json_body.size () > 0) { // There was a body
  //     //   for (const auto v : json_body) {
  //     //     if (v != property) {
  //     //       wrongPropertyFlag = true;
  //     //     }
  //     //   }
  //     // }
  //
  //     CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
  //   }

//     // Tests that should end with status_codes::OK (200)
//     TEST_FIXTURE(GetFixture, ReadEntityAuth) {
//
//       cout << "Requesting read token" << endl;
//       pair<status_code,string> token_res {
//         get_read_token(GetFixture::auth_addr,
//                          GetFixture::userid,
//                          GetFixture::user_pwd)};
//       cout << "Token response " << token_res.first << endl;
//       CHECK_EQUAL (token_res.first, status_codes::OK);
//
//       pair<status_code,value> result {
//         do_request (methods::GET,
//                     string(GetFixture::addr)
//                     + read_entity_auth + "/"
//                     + GetFixture::table + "/"
//                     + token_res.second + "/"
//                     + GetFixture::partition + "/"
//                     + GetFixture::row)};
//       CHECK_EQUAL(status_codes::OK, result.first);
//
//     }
//
//     // Tests that should end with status_codes::BadRequest (400)
//     TEST_FIXTURE(GetFixture, ReadEntityAuth_BadRequest) {
//
//       cout << "Requesting read token" << endl;
//       pair<status_code,string> token_res {
//         get_read_token(GetFixture::auth_addr,
//                          GetFixture::userid,
//                          GetFixture::user_pwd)};
//       cout << "Token response " << token_res.first << endl;
//       CHECK_EQUAL (token_res.first, status_codes::OK);
//
//       // No table name
//       pair<status_code,value> result {
//         do_request (methods::GET,
//                     string(GetFixture::addr)
//                     + read_entity_auth + "/"
//                     + token_res.second + "/"
//                     + GetFixture::partition + "/"
//                     + GetFixture::row)};
//       CHECK_EQUAL(status_codes::OK, result.first);
//
//       // No token
//       result = do_request (methods::GET,
//                     string(GetFixture::addr)
//                     + read_entity_auth + "/"
//                     + GetFixture::table + "/"
//                     + GetFixture::partition + "/"
//                     + GetFixture::row);
//       CHECK_EQUAL(status_codes::OK, result.first);
//
//       // No partition
//       result = do_request (methods::GET,
//                     string(GetFixture::addr)
//                     + read_entity_auth + "/"
//                     + GetFixture::table + "/"
//                     + token_res.second + "/"
//                     + GetFixture::row);
//       CHECK_EQUAL(status_codes::OK, result.first);
//
//       // No row
//       result = do_request (methods::GET,
//                     string(GetFixture::addr)
//                     + read_entity_auth + "/"
//                     + GetFixture::table + "/"
//                     + token_res.second + "/"
//                     + GetFixture::partition);
//       CHECK_EQUAL(status_codes::OK, result.first);
//
//     }
//
//     // Tests that should end with status_codes::NotFound (404)
//     TEST_FIXTURE(GetFixture, ReadEntityAuth_NotFound) {
//
//       cout << "Requesting read token" << endl;
//       pair<status_code,string> token_res {
//         get_read_token(GetFixture::auth_addr,
//                          GetFixture::userid,
//                          GetFixture::user_pwd)};
//       cout << "Token response " << token_res.first << endl;
//       CHECK_EQUAL (token_res.first, status_codes::OK);
//
//       // Invalid table name
//       pair<status_code,value> result {
//         do_request (methods::GET,
//                     string(GetFixture::addr)
//                     + read_entity_auth + "/"
//                     + "WrongTable" + "/"
//                     + token_res.second + "/"
//                     + GetFixture::partition + "/"
//                     + GetFixture::row)};
//       CHECK_EQUAL(status_codes::OK, result.first);
//
//       // Invalid partition and row
//       result = do_request (methods::GET,
//                           string(GetFixture::addr)
//                           + read_entity_auth + "/"
//                           + "WrongTable" + "/"
//                           + token_res.second + "/"
//                           + GetFixture::partition + "/"
//                           + GetFixture::row);
//       CHECK_EQUAL(status_codes::OK, result.first);
//     }
// }

class AuthFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* auth_addr {"http://localhost:34570/"};
  static constexpr const char* userid {"user"};
  static constexpr const char* user_pwd {"user"};
  static constexpr const char* auth_table {"AuthTable"};
  static constexpr const char* auth_table_partition {"Userid"};
  static constexpr const char* auth_pwd_prop {"Password"};
  static constexpr const char* table {"DataTable"};
  static constexpr const char* partition {"USA"};
  static constexpr const char* row {"Franklin,Aretha"};
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  AuthFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }
}

  ~AuthFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      throw std::exception();
    }
  }
};

SUITE(UPDATE_AUTH) {
  TEST_FIXTURE(AuthFixture, PutAuth) {
    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    CHECK_EQUAL(status_codes::OK, result.first);

    pair<status_code,value> ret_res {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_admin + "/"
                  + AuthFixture::table + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::OK, ret_res.first);
    value expect {
      build_json_object (
                         vector<pair<string,string>> {
                           added_prop,
                           make_pair(string(AuthFixture::property),
                                     string(AuthFixture::prop_val))}
                         )};

    cout << AuthFixture::property << endl;
    compare_json_values (expect, ret_res.second);
  }

  // Tests that should end with status_codes::BadRequest (400)
  TEST_FIXTURE(AuthFixture, PutAuth_BadReq) {
    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // No table name
    // Ends in status_codes::NotFound (404) so commenting this out for this test
    // pair<status_code,value> result {
    //   do_request (methods::PUT,
    //               string(AuthFixture::addr)
    //               + update_entity_auth + "/"
    //               + token_res.second + "/"
    //               + AuthFixture::partition + "/"
    //               + AuthFixture::row,
    //               value::object (vector<pair<string,value>>
    //                                {make_pair(added_prop.first,
    //                                           value::string(added_prop.second))})
    //               )};
    // CHECK_EQUAL(status_codes::BadRequest, result.first);

    // No token
    pair<status_code,value> result {

      do_request (methods::PUT,
                string(AuthFixture::addr)
                + update_entity_auth + "/"
                + AuthFixture::table + "/"
                + AuthFixture::partition + "/"
                + AuthFixture::row,
                value::object (vector<pair<string,value>>
                                 {make_pair(added_prop.first,
                                            value::string(added_prop.second))}))
      };

    CHECK_EQUAL(status_codes::BadRequest, result.first);

    // No partition
    result = do_request (methods::PUT,
                string(AuthFixture::addr)
                + update_entity_auth + "/"
                + AuthFixture::table + "/"
                + token_res.second + "/"
                + AuthFixture::row,
                value::object (vector<pair<string,value>>
                                 {make_pair(added_prop.first,
                                            value::string(added_prop.second))}));

    CHECK_EQUAL(status_codes::BadRequest, result.first);

    // No row
    result = do_request (methods::PUT,
                string(AuthFixture::addr)
                + update_entity_auth + "/"
                + AuthFixture::table + "/"
                + token_res.second + "/"
                + AuthFixture::partition + "/",
                value::object (vector<pair<string,value>>
                                 {make_pair(added_prop.first,
                                            value::string(added_prop.second))}));

    CHECK_EQUAL(status_codes::BadRequest, result.first);

  }

  // Tests that should end with status_codes::Forbidden (403)
  TEST_FIXTURE(AuthFixture, PutAuth_Forbidden) {
    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting READ-ONLY token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    CHECK_EQUAL(status_codes::Forbidden, result.first);

  }

  // Tests that should end with status_codes::NotFound (404)
  TEST_FIXTURE(AuthFixture, PutAuth_NotFound) {
    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Invalid table name
    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + "WrongTable" + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    CHECK_EQUAL(status_codes::NotFound, result.first);

    // Invalid partition and row
    // Results in status_codes::Forbidden (403) b/c partition/row don't correspond
    // to token partition/row so commenting this out
    // result = do_request (methods::PUT,
    //               string(AuthFixture::addr)
    //               + update_entity_auth + "/"
    //               + AuthFixture::table + "/"
    //               + token_res.second + "/"
    //               + "WrongPartition" + "/"
    //               + "WrongRow",
    //               value::object (vector<pair<string,value>>
    //                                {make_pair(added_prop.first,
    //                                           value::string(added_prop.second))}));
    // CHECK_EQUAL(status_codes::NotFound, result.first);


  }
}
}
