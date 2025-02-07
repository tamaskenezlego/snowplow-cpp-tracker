/*
Copyright (c) 2022 Snowplow Analytics Ltd. All rights reserved.

This program is licensed to you under the Apache License Version 2.0,
and you may not use this file except in compliance with the Apache License Version 2.0.
You may obtain a copy of the Apache License Version 2.0 at http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing,
software distributed under the Apache License Version 2.0 is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the Apache License Version 2.0 for the specific language governing permissions and limitations there under.
*/

#include "../include/snowplow/client_session.hpp"
#include "../include/snowplow/storage/sqlite_storage.hpp"
#include "../include/snowplow/detail/utils/utils.hpp"
#include "catch.hpp"
#include <thread>

using namespace snowplow;
using std::chrono::milliseconds;
using std::this_thread::sleep_for;

TEST_CASE("client_session") {
  SECTION("Correctly initialized using SessionConfiguration") {
    SessionConfiguration session_config("test1.db", 101, 102);
    ClientSession session(session_config);
    REQUIRE(session.get_foreground_timeout() == 101);
    REQUIRE(session.get_background_timeout() == 102);
  }

  SECTION("The Session doesn't change for subsequent tracked events") {
    auto storage = std::make_shared<SqliteStorage>("test1.db");
    storage->delete_session();

    ClientSession cs(storage, 500, 500);

    SelfDescribingJson session_json_1 = cs.update_and_get_session_context("event-id-1", 1653042535123);
    SelfDescribingJson session_json_2 = cs.update_and_get_session_context("event-id-2", 1653042535345);

    json data_1 = session_json_1.get()[SNOWPLOW_DATA];
    json data_2 = session_json_2.get()[SNOWPLOW_DATA];

    REQUIRE(data_1[SNOWPLOW_SESSION_FIRST_ID].get<std::string>() == data_2[SNOWPLOW_SESSION_FIRST_ID].get<std::string>());
    REQUIRE(data_1[SNOWPLOW_SESSION_FIRST_TIMESTAMP].get<std::string>() == data_2[SNOWPLOW_SESSION_FIRST_TIMESTAMP].get<std::string>());
    REQUIRE(data_1[SNOWPLOW_SESSION_INDEX].get<unsigned long long>() == data_2[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(data_1[SNOWPLOW_SESSION_ID].get<std::string>() == data_2[SNOWPLOW_SESSION_ID].get<std::string>());
  }

  SECTION("The Session must persist and update in the background") {
    auto storage = std::make_shared<SqliteStorage>("test1.db");
    storage->delete_session();

    ClientSession cs(storage, 500, 500);

    REQUIRE(false == cs.get_is_background());
    cs.set_is_background(true);
    REQUIRE(true == cs.get_is_background());

    SelfDescribingJson session_json = cs.update_and_get_session_context("event-id", 1653042535123);
    REQUIRE("iglu:com.snowplowanalytics.snowplow/client_session/jsonschema/1-0-2" == session_json.get()[SNOWPLOW_SCHEMA].get<std::string>());

    json data = session_json.get()[SNOWPLOW_DATA];

    REQUIRE("event-id" == data[SNOWPLOW_SESSION_FIRST_ID].get<std::string>());
    REQUIRE("2022-05-20T10:28:55.123Z" == data[SNOWPLOW_SESSION_FIRST_TIMESTAMP].get<std::string>());
    REQUIRE("SQLITE" == data[SNOWPLOW_SESSION_STORAGE].get<std::string>());
    REQUIRE(1 == data[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(1 == data[SNOWPLOW_SESSION_EVENT_INDEX].get<unsigned long long>());

    string user_id = data[SNOWPLOW_SESSION_USER_ID].get<std::string>();
    string current_id = data[SNOWPLOW_SESSION_ID].get<std::string>();

    sleep_for(milliseconds(875));
    session_json = cs.update_and_get_session_context("event-id-2", 1653042535345);
    data = session_json.get()[SNOWPLOW_DATA];

    REQUIRE("event-id-2" == data[SNOWPLOW_SESSION_FIRST_ID].get<std::string>());
    REQUIRE("2022-05-20T10:28:55.345Z" == data[SNOWPLOW_SESSION_FIRST_TIMESTAMP].get<std::string>());
    REQUIRE("SQLITE" == data[SNOWPLOW_SESSION_STORAGE].get<std::string>());
    REQUIRE(2 == data[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(1 == data[SNOWPLOW_SESSION_EVENT_INDEX].get<unsigned long long>());
    REQUIRE(user_id == data[SNOWPLOW_SESSION_USER_ID].get<std::string>());
    REQUIRE(current_id != data[SNOWPLOW_SESSION_ID].get<std::string>());
    REQUIRE(current_id == data[SNOWPLOW_SESSION_PREVIOUS_ID].get<std::string>());
  }

  SECTION("The Session must fetch information from previous sessions") {
    auto storage = std::make_shared<SqliteStorage>("test2.db");
    storage->delete_session();

    ClientSession cs(storage, 10000, 10000);
    SelfDescribingJson session_json = cs.update_and_get_session_context("event-id2", 1653042535123);

    json data = session_json.get()[SNOWPLOW_DATA];
    REQUIRE(1 == data[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(1 == data[SNOWPLOW_SESSION_EVENT_INDEX].get<unsigned long long>());

    ClientSession cs1(storage, 500, 500);

    SelfDescribingJson session_json1 = cs1.update_and_get_session_context("event-id3", 1653042535345);
    REQUIRE("iglu:com.snowplowanalytics.snowplow/client_session/jsonschema/1-0-2" == session_json1.get()[SNOWPLOW_SCHEMA].get<std::string>());

    json data1 = session_json1.get()[SNOWPLOW_DATA];

    REQUIRE("event-id3" == data1[SNOWPLOW_SESSION_FIRST_ID].get<std::string>());
    REQUIRE("2022-05-20T10:28:55.345Z" == data1[SNOWPLOW_SESSION_FIRST_TIMESTAMP].get<std::string>());
    REQUIRE("SQLITE" == data1[SNOWPLOW_SESSION_STORAGE].get<std::string>());
    REQUIRE(2 == data1[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(1 == data1[SNOWPLOW_SESSION_EVENT_INDEX].get<unsigned long long>());

    string user_id1 = data1[SNOWPLOW_SESSION_USER_ID].get<std::string>();
    string current_id1 = data1[SNOWPLOW_SESSION_ID].get<std::string>();

    sleep_for(milliseconds(850));
    session_json1 = cs1.update_and_get_session_context("event-id4", 1653042535678);
    data1 = session_json1.get()[SNOWPLOW_DATA];

    REQUIRE("event-id4" == data1[SNOWPLOW_SESSION_FIRST_ID].get<std::string>());
    REQUIRE("2022-05-20T10:28:55.678Z" == data1[SNOWPLOW_SESSION_FIRST_TIMESTAMP].get<std::string>());
    REQUIRE("SQLITE" == data1[SNOWPLOW_SESSION_STORAGE].get<std::string>());
    REQUIRE(3 == data1[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(user_id1 == data1[SNOWPLOW_SESSION_USER_ID].get<std::string>());
    REQUIRE(current_id1 != data1[SNOWPLOW_SESSION_ID].get<std::string>());
    REQUIRE(current_id1 == data1[SNOWPLOW_SESSION_PREVIOUS_ID].get<std::string>());
  }

  SECTION("If corrupted data makes it into the session database entry use defaults") {
    auto storage = std::make_shared<SqliteStorage>("test3.db");
    storage->set_session("{}"_json);

    ClientSession cs(storage, 500, 500);

    SelfDescribingJson session_json = cs.update_and_get_session_context("event-id3", Utils::get_unix_epoch_ms());
    REQUIRE("iglu:com.snowplowanalytics.snowplow/client_session/jsonschema/1-0-2" == session_json.get()[SNOWPLOW_SCHEMA].get<std::string>());

    json data = session_json.get()[SNOWPLOW_DATA];

    REQUIRE("event-id3" == data[SNOWPLOW_SESSION_FIRST_ID].get<std::string>());
    REQUIRE("SQLITE" == data[SNOWPLOW_SESSION_STORAGE].get<std::string>());
    REQUIRE(1 == data[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(1 == data[SNOWPLOW_SESSION_EVENT_INDEX].get<unsigned long long>());

    string user_id = data[SNOWPLOW_SESSION_USER_ID].get<std::string>();
    string current_id = data[SNOWPLOW_SESSION_ID].get<std::string>();

    sleep_for(milliseconds(850));
    session_json = cs.update_and_get_session_context("event-id3", Utils::get_unix_epoch_ms());
    data = session_json.get()[SNOWPLOW_DATA];

    REQUIRE("event-id3" == data[SNOWPLOW_SESSION_FIRST_ID].get<std::string>());
    REQUIRE("SQLITE" == data[SNOWPLOW_SESSION_STORAGE].get<std::string>());
    REQUIRE(2 == data[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
    REQUIRE(user_id == data[SNOWPLOW_SESSION_USER_ID].get<std::string>());
    REQUIRE(current_id != data[SNOWPLOW_SESSION_ID].get<std::string>());
    REQUIRE(current_id == data[SNOWPLOW_SESSION_PREVIOUS_ID].get<std::string>());
  }

  SECTION("The Session updates using background timeout in background") {
    auto storage = std::make_shared<SqliteStorage>("test1.db");
    storage->delete_session();

    ClientSession cs(storage, 500, 1);
    cs.set_is_background(true);

    SelfDescribingJson session_json_1 = cs.update_and_get_session_context("event-id-1", Utils::get_unix_epoch_ms());
    sleep_for(milliseconds(5));
    SelfDescribingJson session_json_2 = cs.update_and_get_session_context("event-id-2", Utils::get_unix_epoch_ms());

    json data_1 = session_json_1.get()[SNOWPLOW_DATA];
    json data_2 = session_json_2.get()[SNOWPLOW_DATA];

    REQUIRE(data_1[SNOWPLOW_SESSION_INDEX].get<unsigned long long>() < data_2[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());

    cs.set_is_background(false);

    SelfDescribingJson session_json_3 = cs.update_and_get_session_context("event-id-3", Utils::get_unix_epoch_ms());
    sleep_for(milliseconds(5));
    SelfDescribingJson session_json_4 = cs.update_and_get_session_context("event-id-4", Utils::get_unix_epoch_ms());

    json data_3 = session_json_3.get()[SNOWPLOW_DATA];
    json data_4 = session_json_4.get()[SNOWPLOW_DATA];

    REQUIRE(data_3[SNOWPLOW_SESSION_INDEX].get<unsigned long long>() == data_4[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
  }

  SECTION("The Session updates using background timeout after transition to foreground") {
    auto storage = std::make_shared<SqliteStorage>("test1.db");
    storage->delete_session();

    ClientSession cs(storage, 500, 1);

    SelfDescribingJson session_json_1 = cs.update_and_get_session_context("event-id-1", Utils::get_unix_epoch_ms());
    cs.set_is_background(true);
    sleep_for(milliseconds(5));
    cs.set_is_background(false);
    SelfDescribingJson session_json_2 = cs.update_and_get_session_context("event-id-2", Utils::get_unix_epoch_ms());

    json data_1 = session_json_1.get()[SNOWPLOW_DATA];
    json data_2 = session_json_2.get()[SNOWPLOW_DATA];

    REQUIRE(data_1[SNOWPLOW_SESSION_INDEX].get<unsigned long long>() < data_2[SNOWPLOW_SESSION_INDEX].get<unsigned long long>());
  }

  SECTION("The event index increases for subsequent tracked events") {
    auto storage = std::make_shared<SqliteStorage>("test1.db");
    storage->delete_session();

    ClientSession cs(storage, 500, 500);

    SelfDescribingJson session_json_1 = cs.update_and_get_session_context("event-id-1", Utils::get_unix_epoch_ms());
    SelfDescribingJson session_json_2 = cs.update_and_get_session_context("event-id-2", Utils::get_unix_epoch_ms());

    json data_1 = session_json_1.get()[SNOWPLOW_DATA];
    json data_2 = session_json_2.get()[SNOWPLOW_DATA];

    REQUIRE(1 == data_1[SNOWPLOW_SESSION_EVENT_INDEX].get<int>());
    REQUIRE(2 == data_2[SNOWPLOW_SESSION_EVENT_INDEX].get<int>());
  }
}
