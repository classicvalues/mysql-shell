/*
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include "modules/adminapi/mod_dba_replicaset.h"
#include "modules/adminapi/mod_dba_metadata_storage.h"
#include "modules/adminapi/mod_dba_instance.h"
#include "modules/adminapi/mod_dba_common.h"

#include "common/uuid/include/uuid_gen.h"
#include "utils/utils_general.h"
#include "modules/mysqlxtest_utils.h"
#include "xerrmsg.h"
#include "mysqlx_connection.h"
#include "shellcore/shell_core_options.h"
#include "common/process_launcher/process_launcher.h"
#include "modules/mod_mysql_session.h"
#include "modules/mod_mysql_resultset.h"
#include "utils/utils_time.h"

#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <random>
#ifdef _WIN32
#define strerror_r(errno,buf,len) strerror_s(buf,len,errno)
#else
#include <unistd.h>
#include <sys/types.h>
#endif

using namespace std::placeholders;
using namespace mysh;
using namespace mysh::dba;
using namespace shcore;

#define PASSWORD_LENGTH 16

std::set<std::string> ReplicaSet::_add_instance_opts = {"name", "host", "port", "user", "dbUser", "password", "dbPassword", "socket", "ssl_ca", "ssl_cert", "ssl_key", "ssl_key"};
std::set<std::string> ReplicaSet::_remove_instance_opts = {"name", "host", "port", "socket", "ssl_ca", "ssl_cert", "ssl_key", "ssl_key"};

char const *ReplicaSet::kTopologyPrimaryMaster = "pm";
char const *ReplicaSet::kTopologyMultiMaster = "mm";

static std::string generate_password(int password_lenght);

ReplicaSet::ReplicaSet(const std::string &name, const std::string &topology_type,
                       std::shared_ptr<MetadataStorage> metadata_storage) :
  _name(name), _topology_type(topology_type), _json_mode(JSON_STANDARD_OUTPUT),
  _metadata_storage(metadata_storage) {
  init();
}

ReplicaSet::~ReplicaSet() {}

std::string &ReplicaSet::append_descr(std::string &s_out, int UNUSED(indent), int UNUSED(quote_strings)) const {
  s_out.append("<" + class_name() + ":" + _name + ">");
  return s_out;
}

static void append_member_status(shcore::JSON_dumper& dumper,
                                 std::shared_ptr<mysh::Row> member_row,
                                 bool read_write) {
  //dumper.append_string("name", member_row->get_member(1).as_string());
  auto status = member_row->get_member(3);
  dumper.append_string("address", member_row->get_member(4).as_string());
  dumper.append_string("status", status ? status.as_string() : "OFFLINE");
  dumper.append_string("role", member_row->get_member(2).as_string());
  dumper.append_string("mode", read_write ? "R/W" : "R/O");
}

void ReplicaSet::append_json_status(shcore::JSON_dumper& dumper) const {
  bool single_primary_mode = _topology_type == kTopologyPrimaryMaster;

  // Identifies the master node
  std::string master_uuid;
  if (single_primary_mode) {
    auto uuid_result = _metadata_storage->execute_sql("show status like 'group_replication_primary_member'");
    auto uuid_row = uuid_result->call("fetchOne", shcore::Argument_list());
    if (uuid_row)
      master_uuid = uuid_row.as_object<Row>()->get_member(1).as_string();
  }

  std::string query = "select mysql_server_uuid, instance_name, role, MEMBER_STATE, JSON_UNQUOTE(JSON_EXTRACT(addresses, \"$.mysqlClassic\")) as host"
                      " from mysql_innodb_cluster_metadata.instances left join performance_schema.replication_group_members on `mysql_server_uuid`=`MEMBER_ID`"
                      " where replicaset_id = " + std::to_string(_id);
  auto result = _metadata_storage->execute_sql(query);

  auto raw_instances = result->call("fetchAll", shcore::Argument_list());

  // First we identify the master instance
  auto instances = raw_instances.as_array();

  std::shared_ptr<mysh::Row> master;
  int online_count = 0;
  for (auto value : *instances.get()) {
    auto row = value.as_object<mysh::Row>();
    if (row->get_member(0).as_string() == master_uuid)
      master = row;

    auto status = row->get_member(3);
    if (status && status.as_string() == "ONLINE")
      online_count++;
  }

  std::string rset_status;
  switch (online_count) {
    case 0:
    case 1:
    case 2:
      rset_status = "Cluster is NOT tolerant to any failures.";
      break;
    case 3:
      rset_status = "Cluster tolerant to up to ONE failure.";
      break;
      // Add logic on the default for the even/uneven count
    default:
      rset_status = "Cluster tolerant to up to " + std::to_string(online_count - 2) + " failures.";
      break;
  }

  dumper.start_object();
  dumper.append_string("status", rset_status);
  if (!single_primary_mode) {
    //  dumper.append_string("topology", _topology_type);
  }
  dumper.append_string("topology");
  if (single_primary_mode) {
    dumper.start_object();
    if (master) {
      dumper.append_string(master->get_member(1).as_string());
      dumper.start_object();
      append_member_status(dumper, master, true);
      dumper.append_string("leaves");
      dumper.start_object();
    }
    for (auto value : *instances.get()) {
      auto row = value.as_object<mysh::Row>();
      if (row != master) {
        dumper.append_string(row->get_member(1).as_string());
        dumper.start_object();
        append_member_status(dumper, row, single_primary_mode ? false : true);
        dumper.append_string("leaves");
        dumper.start_object();
        dumper.end_object();
        dumper.end_object();
      }
    }
    if (master) {
      dumper.end_object();
      dumper.end_object();
    }
    dumper.end_object();
  } else {
    dumper.start_object();
    for (auto value : *instances.get()) {
      auto row = value.as_object<mysh::Row>();
      dumper.append_string(row->get_member(1).as_string());
      dumper.start_object();
      append_member_status(dumper, row, true);
      dumper.append_string("leaves");
      dumper.start_object();
      dumper.end_object();
      dumper.end_object();
    }
    dumper.end_object();
  }
  dumper.end_object();
}

void ReplicaSet::append_json_description(shcore::JSON_dumper& dumper) const {
  std::string query = "select mysql_server_uuid, instance_name, role,"
                      " JSON_UNQUOTE(JSON_EXTRACT(addresses, \"$.mysqlClassic\")) as host"
                      " from mysql_innodb_cluster_metadata.instances"
                      " where replicaset_id = " + std::to_string(_id);
  auto result = _metadata_storage->execute_sql(query);

  auto raw_instances = result->call("fetchAll", shcore::Argument_list());

  // First we identify the master instance
  auto instances = raw_instances.as_array();

  dumper.start_object();
  dumper.append_string("name", _name);

  // Creates the instances element
  dumper.append_string("instances");
  dumper.start_array();

  for (auto value : *instances.get()) {
    auto row = value.as_object<mysh::Row>();
    dumper.start_object();
    dumper.append_string("name", row->get_member(1).as_string());
    dumper.append_string("host", row->get_member(3).as_string());
    dumper.append_string("role", row->get_member(2).as_string());
    dumper.end_object();
  }
  dumper.end_array();
  dumper.end_object();
}

void ReplicaSet::append_json(shcore::JSON_dumper& dumper) const {
  if (_json_mode == JSON_STANDARD_OUTPUT)
    shcore::Cpp_object_bridge::append_json(dumper);
  else {
    if (_json_mode == JSON_STATUS_OUTPUT)
      append_json_status(dumper);
    else if (_json_mode == JSON_TOPOLOGY_OUTPUT)
      append_json_description(dumper);
  }
}

bool ReplicaSet::operator == (const Object_bridge &other) const {
  return class_name() == other.class_name() && this == &other;
}

#if DOXYGEN_CPP
/**
 * Use this function to retrieve an valid member of this class exposed to the scripting languages.
 * \param prop : A string containing the name of the member to be returned
 *
 * This function returns a Value that wraps the object returned by this function.
 * The content of the returned value depends on the property being requested.
 * The next list shows the valid properties as well as the returned value for each of them:
 *
 * \li name: returns a String object with the name of this ReplicaSet object.
 */
#else
/**
* Returns the name of this ReplicaSet object.
* \return the name as an String object.
*/
#if DOXYGEN_JS
String ReplicaSet::getName() {}
#elif DOXYGEN_PY
str ReplicaSet::get_name() {}
#endif
#endif
shcore::Value ReplicaSet::get_member(const std::string &prop) const {
  shcore::Value ret_val;
  if (prop == "name")
    ret_val = shcore::Value(_name);
  else
    ret_val = shcore::Cpp_object_bridge::get_member(prop);

  return ret_val;
}

void ReplicaSet::init() {
  add_property("name", "getName");
  add_method("addInstance", std::bind(&ReplicaSet::add_instance_, this, _1), "data");
  add_method("rejoinInstance", std::bind(&ReplicaSet::rejoin_instance, this, _1), "data");
  add_method("removeInstance", std::bind(&ReplicaSet::remove_instance_, this, _1), "data");
  add_varargs_method("disable", std::bind(&ReplicaSet::disable, this, _1));
  add_varargs_method("dissolve", std::bind(&ReplicaSet::dissolve, this, _1));
}

#if DOXYGEN_CPP
/**
 * Use this function to add a Instance to the ReplicaSet object
 * \param args : A list of values to be used to add a Instance to the ReplicaSet.
 *
 * This function returns an empty Value.
 */
#else
/**
* Adds a Instance to the ReplicaSet
* \param conn The Connection String or URI of the Instance to be added
*/
#if DOXYGEN_JS
Undefined ReplicaSet::addInstance(String conn) {}
#elif DOXYGEN_PY
None ReplicaSet::add_instance(str conn) {}
#endif
/**
* Adds a Instance to the ReplicaSet
* \param doc The Document representing the Instance to be added
*/
#if DOXYGEN_JS
Undefined ReplicaSet::addInstance(Document doc) {}
#elif DOXYGEN_PY
None ReplicaSet::add_instance(Document doc) {}
#endif
#endif
shcore::Value ReplicaSet::add_instance_(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(1, 2, get_function_name("addInstance").c_str());

  // Check if the ReplicaSet is empty
  if (_metadata_storage->is_replicaset_empty(get_id()))
    throw shcore::Exception::runtime_error("ReplicaSet not initialized. Please add the Seed Instance using: addSeedInstance().");

  // Add the Instance to the Default ReplicaSet
  try {
    ret_val = add_instance(args);
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("addInstance"));

  return ret_val;
}

static void run_queries(mysh::mysql::ClassicSession *session, const std::vector<std::string> &queries) {
  for (auto & q : queries) {
    shcore::Argument_list args;
    args.push_back(shcore::Value(q));
    log_info("DBA: run_sql(%s)", q.c_str());
    session->run_sql(args);
  }
}

shcore::Value ReplicaSet::add_instance(const shcore::Argument_list &args) {
  shcore::Value ret_val;

  bool seed_instance = false;
  std::string uri;

  std::string user;
  std::string super_user_password;
  std::string joiner_host;
  int port = 0;

  // NOTE: This function is called from either the add_instance_ on this class
  //       or the add_instance in Cluster class, hence this just throws exceptions
  //       and the proper handling is done on the caller functions (to append the called function name)

  // Check if we're on a addSeedInstance or not
  if (_metadata_storage->is_replicaset_empty(_id)) seed_instance = true;

  auto options = get_instance_options_map(args);
  
  shcore::Argument_map opt_map(*options);
  opt_map.ensure_keys({"host"}, _add_instance_opts, "instance definition");
  
  
  if (!options->has_key("port"))
    (*options)["port"] = shcore::Value(get_default_port());
  
  port = options->get_int("port");

  // Sets a default user if not specified
  if (options->has_key("user"))
    user = options->get_string("user");
  else if (options->has_key("dbUser"))
    user = options->get_string("dbUser");
  else {
    user = "root";
    (*options)["dbUser"] = shcore::Value(user);
  }

  joiner_host = options->get_string("host");

  std::string password;
  if (options->has_key("password"))
    super_user_password = options->get_string("password");
  else if (options->has_key("dbPassword"))
    super_user_password = options->get_string("dbPassword");
  else
    throw shcore::Exception::argument_error("Missing password for " + build_connection_string(options, false));

  // Check if the instance was already added
  std::string instance_address = joiner_host + ":" + std::to_string(options->get_int("port"));
  std::string instance_public_address;
  if (joiner_host == "localhost")
    instance_public_address = get_my_hostname();
  else
    instance_public_address = joiner_host;
  std::string instance_public_xaddress = instance_public_address;
  instance_public_address.append(":" + std::to_string(options->get_int("port")));

  if (_metadata_storage->is_instance_on_replicaset(get_id(), instance_public_address))
    throw shcore::Exception::runtime_error("The instance '" + instance_public_address + "' already belongs to the ReplicaSet: '" + get_member("name").as_string() + "'.");

  // generate a replication user account + password for this instance
  // This account will be replicated to all instances in the replicaset, so that
  // the newly joining instance can connect to any of them for recovery.
  std::string replication_user;
  std::string replication_user_password = generate_password(PASSWORD_LENGTH);

  MySQL_timer timer;
  std::string tstamp = std::to_string(timer.get_time());
  std::string base_user = "mysql_innodb_cluster_rplusr";
  replication_user = base_user.substr(0, 32 - tstamp.size()) + tstamp;
  // Replication accounts must be created for the real hostname, because the GR
  // plugin will connect to the real host interface of the peer,
  // even if it's localhost

  // TODO: Uncomment this when the logic to retrieve the correct hostname is fixed
  //if (joiner_host == "localhost")
  //  replication_user.append("@'").append(get_my_hostname()).append("'");
  //else
  //  replication_user.append("@'").append(joiner_host).append("'");
  replication_user.append("@'%'");

  int xport = options->get_int("port") * 10;
  std::string mysql_server_uuid;
  // get the server_uuid from the joining instance
  {
    shcore::Argument_list temp_args, new_args;
    new_args.push_back(shcore::Value(options));
    auto session = mysh::connect_session(new_args, mysh::SessionType::Classic);
    mysh::mysql::ClassicSession *classic = dynamic_cast<mysh::mysql::ClassicSession*>(session.get());
    {
      temp_args.clear();
      temp_args.push_back(shcore::Value("SELECT @@server_uuid"));
      auto uuid_raw_result = classic->run_sql(temp_args);
      auto uuid_result = uuid_raw_result.as_object<mysh::mysql::ClassicResult>();
      auto uuid_row = uuid_result->fetch_one(shcore::Argument_list());
      if (uuid_row)
        mysql_server_uuid = uuid_row.as_object<mysh::Row>()->get_member(0).as_string();
    }
    try {
      temp_args.clear();
      temp_args.push_back(shcore::Value("SELECT @@mysqlx_port"));
      auto raw_result = classic->run_sql(temp_args);
      auto result = raw_result.as_object<mysh::mysql::ClassicResult>();
      auto xport_row = result->fetch_one(shcore::Argument_list());
      if (xport_row)
        xport = (int)xport_row.as_object<mysh::Row>()->get_member(0).as_int();
    } catch (std::exception &e) {
      log_info("Could not query xplugin port, using default value: %s", e.what());
    }
  }
  // Call the gadget to bootstrap the group with this instance
  if (seed_instance) {
    create_repl_account(instance_address, replication_user, replication_user_password);

    // Call mysqlprovision to bootstrap the group using "start"
    do_join_replicaset(user + "@" + instance_address,
                       "",
                       super_user_password,
                       replication_user, replication_user_password);
  } else {
    // We need to retrieve a peer instance, so let's use the Seed one
    std::string peer_instance = get_peer_instances().front();
    create_repl_account(peer_instance, replication_user, replication_user_password);

    // substitute the public hostname for localhost if we're running locally
    // that is because root@localhost exists by default, while root@hostname doesn't
    // so trying the 2nd will probably not work
    auto sep = peer_instance.find(':');
    if (peer_instance.substr(0, sep) == get_my_hostname())
      peer_instance = "localhost:" + peer_instance.substr(sep + 1);

    // Call mysqlprovision to do the work
    do_join_replicaset(user + "@" + instance_address,
                       user + "@" + peer_instance,
                       super_user_password,
                       replication_user, replication_user_password);
  }

  MetadataStorage::Transaction tx(_metadata_storage);

  // OK, if we reached here without errors we can update the metadata with the host
  auto result = _metadata_storage->insert_host(args);

  // And the instance
  uint64_t host_id = result->get_member("autoIncrementValue").as_int();

  shcore::Argument_list args_instance;
  Value::Map_type_ref options_instance(new shcore::Value::Map_type);
  args_instance.push_back(shcore::Value(options_instance));

  (*options_instance)["role"] = shcore::Value("HA");

  (*options_instance)["endpoint"] = shcore::Value(instance_public_address);

  instance_public_xaddress.append(":" + std::to_string(xport));
  (*options_instance)["xendpoint"] = shcore::Value(instance_public_xaddress);

  (*options_instance)["mysql_server_uuid"] = shcore::Value(mysql_server_uuid);

  if (options->has_key("name"))
    (*options_instance)["instance_name"] = (*options)["name"];
  else
    (*options_instance)["instance_name"] = shcore::Value(instance_public_address);

  _metadata_storage->insert_instance(args_instance, host_id, get_id());

  tx.commit();

  return ret_val;
}

bool ReplicaSet::do_join_replicaset(const std::string &instance_url,
                                    const std::string &peer_instance_url,
                                    const std::string &super_user_password,
                                    const std::string &repl_user, const std::string &repl_user_password) {
  shcore::Value ret_val;
  int exit_code = -1;

  bool is_seed_instance = peer_instance_url.empty() ? true : false;

  std::string command, errors;

  if (is_seed_instance) {
    exit_code = _cluster->get_provisioning_interface()->start_replicaset(instance_url,
                repl_user, super_user_password,
                repl_user_password,
                _topology_type == kTopologyMultiMaster,
                errors);
  } else {
    exit_code = _cluster->get_provisioning_interface()->join_replicaset(instance_url,
                repl_user, peer_instance_url,
                super_user_password, repl_user_password,
                _topology_type == kTopologyMultiMaster,
                errors);
  }

  if (exit_code == 0) {
    if (is_seed_instance)
      ret_val = shcore::Value("The instance '" + instance_url + "' was successfully added as seeding instance to the MySQL Cluster.");
    else
      ret_val = shcore::Value("The instance '" + instance_url + "' was successfully added to the MySQL Cluster.");
  } else
    throw shcore::Exception::runtime_error(errors);

  return exit_code == 0;
}

#if DOXYGEN_CPP
/**
 * Use this function to rejoin an Instance to the ReplicaSet
 * \param args : A list of values to be used to add a Instance to the ReplicaSet.
 *
 * This function returns an empty Value.
 */
#else
/**
* Rejoin a Instance to the ReplicaSet
* \param name The name of the Instance to be rejoined
*/
#if DOXYGEN_JS
Undefined ReplicaSet::rejoinInstance(String name, Dictionary options) {}
#elif DOXYGEN_PY
None ReplicaSet::rejoin_instance(str name, Dictionary options) {}
#endif
#endif // DOXYGEN_CPP
shcore::Value ReplicaSet::rejoin_instance(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(1, 2, get_function_name("rejoinInstance").c_str());
  // Check if the ReplicaSet is empty
  if (_metadata_storage->is_replicaset_empty(get_id()))
    throw shcore::Exception::runtime_error(
      "ReplicaSet not initialized. Please add the Seed Instance using: addSeedInstance().");

  // Add the Instance to the Default ReplicaSet
  try {
    std::string super_user_password;
    std::string host;
    int port = 0;

    auto options = get_instance_options_map(args);
    shcore::Argument_map opt_map(*options);
    opt_map.ensure_keys({"host"}, _add_instance_opts, "instance definition");
    
    if (!options->has_key("port"))
      (*options)["port"] = shcore::Value(get_default_port());
    
    port = options->get_int("port");
    
    std::string peer_instance = _metadata_storage->get_seed_instance(get_id());
    if (peer_instance.empty()) {
      throw shcore::Exception::runtime_error("Cannot rejoin instance. There are no remaining available instances in the replicaset.");
    }
    std::string user;
    // Sets a default user if not specified
    if (options->has_key("user"))
      user = options->get_string("user");
    else if (options->has_key("dbUser"))
      user = options->get_string("dbUser");
    else {
      user = "root";
      (*options)["dbUser"] = shcore::Value(user);
    }
    host = options->get_string("host");

    std::string password;
    if (options->has_key("password"))
      super_user_password = options->get_string("password");
    else if (options->has_key("dbPassword"))
      super_user_password = options->get_string("dbPassword");
    else
      throw shcore::Exception::argument_error("Missing password for " + build_connection_string(options, false));

    do_join_replicaset(user + "@" + host + ":" + std::to_string(port),
                       user + "@" + peer_instance,
                       super_user_password,
                       "", "");
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("addInstance"));

  return ret_val;
}

#if DOXYGEN_CPP
/**
 * Use this function to remove a Instance from the ReplicaSet object
 * \param args : A list of values to be used to remove a Instance to the Cluster.
 *
 * This function returns an empty Value.
 */
#else
/**
* Removes a Instance from the ReplicaSet
* \param name The name of the Instance to be removed
*/
#if DOXYGEN_JS
Undefined ReplicaSet::removeInstance(String name) {}
#elif DOXYGEN_PY
None ReplicaSet::remove_instance(str name) {}
#endif

/**
* Removes a Instance from the ReplicaSet
* \param doc The Document representing the Instance to be removed
*/
#if DOXYGEN_JS
Undefined ReplicaSet::removeInstance(Document doc) {}
#elif DOXYGEN_PY
None ReplicaSet::remove_instance(Document doc) {}
#endif
#endif

shcore::Value ReplicaSet::remove_instance_(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(1, get_function_name("removeInstance").c_str());

  // Remove the Instance from the Default ReplicaSet
  try {
    ret_val = remove_instance(args);
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("removeInstance"));

  return ret_val;
}

shcore::Value ReplicaSet::remove_instance(const shcore::Argument_list &args) {
  args.ensure_count(1, get_function_name("removeInstance").c_str());

  std::string uri;

  std::string name;
  std::string port;


  auto options = get_instance_options_map(args);

  // No required options set before refactoring
  shcore::Argument_map opt_map(*options);
  opt_map.ensure_keys({}, _remove_instance_opts, "instance definition");

  if (!options->has_key("port"))
    (*options)["port"] = shcore::Value(get_default_port());

  port = std::to_string(options->get_int("port"));

  // get instance admin and user information from the current active session of the shell
  // Note: when separate metadata session vs active session is supported, this should
  // be changed to use the active shell session
  auto instance_session(_metadata_storage->get_dba()->get_active_session());
  std::string instance_admin_user = instance_session->get_user();
  std::string instance_admin_user_password = instance_session->get_password();

  std::string host = options->get_string("host");
  std::string public_localhost_name = get_my_hostname();

  // Check if the instance was already added
  std::string instance_address = host + ":" + port;
  std::string instance_public_address;
  if (host == "localhost")
    instance_public_address = public_localhost_name;
  else
    instance_public_address = host;

  instance_public_address.append(":" + port);


  // Check if the instance exists on the ReplicaSet
  //std::string instance_address = options->get_string("host") + ":" + std::to_string(options->get_int("port"));

  if (!_metadata_storage->is_instance_on_replicaset(get_id(), instance_public_address)) {
    std::string message = "The instance '" + instance_address + "'";

    if (instance_public_address != instance_address)
      message.append("(" + instance_public_address + ")");

    message.append(" does not belong to the ReplicaSet: '" + get_member("name").as_string() + "'.");

    throw shcore::Exception::runtime_error(message);
  }

  MetadataStorage::Transaction tx(_metadata_storage);

  // Update the Metadata

  // TODO: do we remove the host? we check if is the last instance of that host and them remove?
  // auto result = _metadata_storage->remove_host(args);

  // TODO: the instance_name can be actually a name, check TODO above
  _metadata_storage->remove_instance(instance_public_address);

  // call provisioning to remove the instance from the replicaset
  int exit_code = -1;
  std::string errors, instance_url;

  instance_url = instance_admin_user + "@";

  if (host == public_localhost_name) {
    instance_url.append("localhost");

    // Override the host since admin user is root@localhost
    // And wil be needed for the SQL operations below
    (*options)["host"] = shcore::Value("localhost");
  } else
    instance_url.append(host);

  instance_url.append(":" + port);


  exit_code = _cluster->get_provisioning_interface()->leave_replicaset(instance_url, instance_admin_user_password, errors);

  if (exit_code != 0)
    throw shcore::Exception::runtime_error(errors);
  else
    tx.commit();

  // Drop replication user
  shcore::Argument_list temp_args, new_args;

  (*options)["dbUser"] = shcore::Value(instance_admin_user);
  (*options)["dbPassword"] = shcore::Value(instance_admin_user_password);
  new_args.push_back(shcore::Value(options));

  auto session = mysh::connect_session(new_args, mysh::SessionType::Classic);
  mysh::mysql::ClassicSession *classic = dynamic_cast<mysh::mysql::ClassicSession*>(session.get());

  temp_args.clear();
  temp_args.push_back(shcore::Value("SET sql_log_bin = 0"));
  classic->run_sql(temp_args);

  // TODO: This is NO longer deleting anything since the user was created using a timestamp
  // instead of the port
  std::string replication_user = "mysql_innodb_cluster_rplusr" + port;
  run_queries(classic, {
    "DROP USER IF EXISTS '" + replication_user + "'@'" + host + "'"
  });

  temp_args.clear();
  temp_args.push_back(shcore::Value("SET sql_log_bin = 1"));
  classic->run_sql(temp_args);

  return shcore::Value();
}

shcore::Value ReplicaSet::dissolve(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(0, 1, get_function_name("dissolve").c_str());

  try {
    bool force = false;
    shcore::Value::Map_type_ref options;

    if (args.size() == 1)
      options = args.map_at(0);

    if (options) {
      
      shcore::Argument_map opt_map(*options);
      
      opt_map.ensure_keys({}, {"force"}, "dissolve options");

      if (options->has_key("force"))
        force = opt_map.bool_at("force");
    }

    if (force)
      disable(shcore::Argument_list());
    else if (_metadata_storage->is_replicaset_active(get_id()))
      throw shcore::Exception::runtime_error("Cannot dissolve the ReplicaSet: the ReplicaSet is active.");

    MetadataStorage::Transaction tx(_metadata_storage);

    // remove all the instances from the ReplicaSet
    auto instances = _metadata_storage->get_replicaset_instances(get_id());

    for (auto value : *instances.get()) {
      auto row = value.as_object<mysh::Row>();

      std::string instance_name = row->get_member(1).as_string();
      shcore::Argument_list args_instance;
      args_instance.push_back(shcore::Value(instance_name));

      remove_instance(args_instance);
    }

    // Remove the replicaSet
    _metadata_storage->drop_replicaset(get_id());

    tx.commit();
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("dissolve"));

  return ret_val;
}

shcore::Value ReplicaSet::disable(const shcore::Argument_list &args) {
  shcore::Value ret_val;

  args.ensure_count(0, get_function_name("disable").c_str());

  try {
    MetadataStorage::Transaction tx(_metadata_storage);
    auto instance_session(_metadata_storage->get_dba()->get_active_session());
    std::string instance_admin_user = instance_session->get_user();
    std::string instance_admin_user_password = instance_session->get_password();

    // Get all instances of the replicaset
    auto instances = _metadata_storage->get_replicaset_instances(get_id());

    auto local_public_host = get_my_hostname();

    for (auto value : *instances.get()) {
      int exit_code;
      auto row = value.as_object<mysh::Row>();

      std::string instance_name = row->get_member(1).as_string();

      shcore::Value::Map_type_ref data = shcore::get_connection_data(instance_name, false);
      if (data->has_key("host")) {
        auto host = data->get_string("host");

        if (host == local_public_host)
          instance_name = "localhost:" + std::to_string(data->get_int("port"));
      }

      std::string instance_url = instance_admin_user + "@" + instance_name;
      std::string errors;

      // Leave the replicaset
      exit_code = _cluster->get_provisioning_interface()->leave_replicaset(instance_url,
                  instance_admin_user_password, errors);
      if (exit_code != 0)
        throw shcore::Exception::runtime_error(errors);
    }

    // Update the metadata to turn 'active' off
    _metadata_storage->disable_replicaset(get_id());
    tx.commit();
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("disable"));

  return ret_val;
}

std::vector<std::string> ReplicaSet::get_peer_instances() {
  std::vector<std::string> result;

  // We need to retrieve a peer instance, so let's use the Seed one
  auto instances = _metadata_storage->get_replicaset_instances(get_id());
  if (instances) {
    for (auto value : *instances) {
      auto row = value.as_object<mysh::Row>();
      std::string peer_instance = row->get_member("instance_name").as_string();
      result.push_back(peer_instance);
    }
  }
  return result;
}

/**
 * Create an account in the replicaset.
 */
void ReplicaSet::create_repl_account(const std::string &dest_uri,
                                     const std::string &username,
                                     const std::string &password) {
  auto instance_session(_metadata_storage->get_dba()->get_active_session());

  shcore::Argument_list args;
  auto options = get_connection_data(dest_uri, false);
  // currently assuming metadata connection account is also valid in the cluster
  (*options)["dbUser"] = shcore::Value(instance_session->get_user());
  (*options)["dbPassword"] = shcore::Value(instance_session->get_password());
  args.push_back(shcore::Value(options));

  std::shared_ptr<mysh::ShellDevelopmentSession> session;
  mysh::mysql::ClassicSession *classic;
retry:
  try {
    log_info("Creating account '%s' at instance %s:%i", username.c_str(),
             (*options)["host"].as_string().c_str(),
             (int)(*options)["port"].as_int());
    session = mysh::connect_session(args, mysh::SessionType::Classic);
    classic = dynamic_cast<mysh::mysql::ClassicSession*>(session.get());
  } catch (shcore::Exception &e) {
    // Check if we're getting access denied to the local host
    // If that's the case, retry the connection via localhost instead of the
    // public hostname
    if (e.is_mysql() && e.code() == 1045 &&
        (*options)["host"].as_string() == get_my_hostname()) {
      log_info("Access denied connecting to '%s', retrying via localhost",
               (*options)["host"].as_string().c_str());
      (*options)["host"] = shcore::Value("localhost");
      args.clear();
      args.push_back(shcore::Value(options));
      goto retry;
    }
    throw;
  } catch (std::exception &e) {
    log_error("Could not open connection to %s: %s", dest_uri.c_str(),
              e.what());
    throw;
  }
  try {
    run_queries(classic, {
      "START TRANSACTION",
      "DROP USER IF EXISTS " + username,
      "CREATE USER IF NOT EXISTS " + username + " IDENTIFIED BY '" + password + "'",
      "GRANT REPLICATION SLAVE ON *.* to " + username,
      "COMMIT"
    });
  } catch (...) {
    shcore::Argument_list args;
    args.push_back(shcore::Value("ROLLBACK"));
    classic->run_sql(args);
    throw;
  }
}

static std::string generate_password(int password_lenght) {
  std::random_device rd;
  std::string pwd;
  static const char *alphabet = "1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ~@#%$^&*()-_=+]}[{|;:.>,</?";
  std::uniform_int_distribution<int> dist(0, strlen(alphabet) - 1);

  for (int i = 0; i < password_lenght; i++)
    pwd += alphabet[dist(rd)];

  return pwd;
}
