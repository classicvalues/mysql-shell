/*
 * Copyright (c) 2016, 2021, Oracle and/or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "modules/adminapi/cluster/cluster_impl.h"

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "adminapi/common/group_replication_options.h"
#include "modules/adminapi/cluster/check_instance_state.h"
#include "modules/adminapi/cluster/cluster_join.h"
#include "modules/adminapi/cluster/describe.h"
#include "modules/adminapi/cluster/dissolve.h"
#include "modules/adminapi/cluster/options.h"
#include "modules/adminapi/cluster/remove_instance.h"
#include "modules/adminapi/cluster/rescan.h"
#include "modules/adminapi/cluster/reset_recovery_accounts_password.h"
#include "modules/adminapi/cluster/set_instance_option.h"
#include "modules/adminapi/cluster/set_option.h"
#include "modules/adminapi/cluster/set_primary_instance.h"
#include "modules/adminapi/cluster/status.h"
#include "modules/adminapi/cluster/switch_to_multi_primary_mode.h"
#include "modules/adminapi/cluster/switch_to_single_primary_mode.h"
#include "modules/adminapi/common/accounts.h"
#include "modules/adminapi/common/cluster_types.h"
#include "modules/adminapi/common/common.h"
#include "modules/adminapi/common/dba_errors.h"
#include "modules/adminapi/common/errors.h"
#include "modules/adminapi/common/instance_validations.h"
#include "modules/adminapi/common/metadata_storage.h"
#include "modules/adminapi/common/provision.h"
#include "modules/adminapi/common/router.h"
#include "modules/adminapi/common/sql.h"
#include "modules/adminapi/common/validations.h"
#include "modules/adminapi/dba_utils.h"
#include "modules/adminapi/mod_dba_cluster.h"
#include "modules/mysqlxtest_utils.h"
#include "mysqlshdk/include/shellcore/console.h"
#include "mysqlshdk/libs/config/config_server_handler.h"
#include "mysqlshdk/libs/mysql/clone.h"
#include "mysqlshdk/libs/mysql/group_replication.h"
#include "mysqlshdk/libs/mysql/replication.h"
#include "mysqlshdk/libs/mysql/utils.h"
#include "mysqlshdk/libs/textui/textui.h"
#include "scripting/types.h"
#include "shellcore/utils_help.h"
#include "utils/debug.h"
#include "utils/utils_general.h"

namespace mysqlsh {
namespace dba {

Cluster_impl::Cluster_impl(
    const Cluster_metadata &metadata,
    const std::shared_ptr<Instance> &group_server,
    const std::shared_ptr<MetadataStorage> &metadata_storage)
    : Base_cluster_impl(metadata.cluster_name, group_server, metadata_storage),
      m_topology_type(metadata.cluster_topology_type) {
  m_id = metadata.cluster_id;
  m_description = metadata.description;
  m_group_name = metadata.group_name;
}

Cluster_impl::Cluster_impl(
    const std::string &cluster_name, const std::string &group_name,
    const std::shared_ptr<Instance> &group_server,
    const std::shared_ptr<MetadataStorage> &metadata_storage,
    mysqlshdk::gr::Topology_mode topology_type)
    : Base_cluster_impl(cluster_name, group_server, metadata_storage),
      m_group_name(group_name),
      m_topology_type(topology_type) {
  // cluster is being created

  m_primary_master = group_server;

  assert(topology_type == mysqlshdk::gr::Topology_mode::SINGLE_PRIMARY ||
         topology_type == mysqlshdk::gr::Topology_mode::MULTI_PRIMARY);
}

Cluster_impl::~Cluster_impl() {}

void Cluster_impl::sanity_check() const { verify_topology_type_change(); }

/*
 * Verify if the topology type changed and issue an error if needed.
 */
void Cluster_impl::verify_topology_type_change() const {
  // Get the primary UUID value to determine GR mode:
  // UUID (not empty) -> single-primary or "" (empty) -> multi-primary

  std::string gr_primary_uuid =
      mysqlshdk::gr::get_group_primary_uuid(*get_target_server(), nullptr);

  // Check if the topology type matches the real settings used by the
  // cluster instance, otherwise an error is issued.
  // NOTE: The GR primary mode is guaranteed (by GR) to be the same for all
  // instance of the same group.
  if (!gr_primary_uuid.empty() &&
      get_cluster_topology_type() ==
          mysqlshdk::gr::Topology_mode::MULTI_PRIMARY) {
    throw shcore::Exception::runtime_error(
        "The InnoDB Cluster topology type (Multi-Primary) does not match the "
        "current Group Replication configuration (Single-Primary). Please "
        "use <cluster>.rescan() or change the Group Replication "
        "configuration accordingly.");
  } else if (gr_primary_uuid.empty() &&
             get_cluster_topology_type() ==
                 mysqlshdk::gr::Topology_mode::SINGLE_PRIMARY) {
    throw shcore::Exception::runtime_error(
        "The InnoDB Cluster topology type (Single-Primary) does not match the "
        "current Group Replication configuration (Multi-Primary). Please "
        "use <cluster>.rescan() or change the Group Replication "
        "configuration accordingly.");
  }
}

void Cluster_impl::validate_rejoin_gtid_consistency(
    const mysqlshdk::mysql::IInstance &target_instance) {
  auto console = mysqlsh::current_console();
  std::string errant_gtid_set;

  // Get the gtid state in regards to the cluster_session
  mysqlshdk::mysql::Replica_gtid_state state =
      mysqlshdk::mysql::check_replica_gtid_state(
          *get_target_server(), target_instance, nullptr, &errant_gtid_set);

  if (state == mysqlshdk::mysql::Replica_gtid_state::NEW) {
    console->print_info();
    console->print_error(
        "The target instance '" + target_instance.descr() +
        "' has an empty GTID set so it cannot be safely rejoined to the "
        "cluster. Please remove it and add it back to the cluster.");
    console->print_info();

    throw shcore::Exception::runtime_error("The instance '" +
                                           target_instance.descr() +
                                           "' has an empty GTID set.");
  } else if (state == mysqlshdk::mysql::Replica_gtid_state::IRRECOVERABLE) {
    console->print_info();
    console->print_error("A GTID set check of the MySQL instance at '" +
                         target_instance.descr() +
                         "' determined that it is missing transactions that "
                         "were purged from all cluster members.");
    console->print_info();
    throw shcore::Exception::runtime_error(
        "The instance '" + target_instance.descr() +
        "' is missing transactions that "
        "were purged from all cluster members.");
  } else if (state == mysqlshdk::mysql::Replica_gtid_state::DIVERGED) {
    console->print_info();
    console->print_error("A GTID set check of the MySQL instance at '" +
                         target_instance.descr() +
                         "' determined that it contains transactions that "
                         "do not originate from the cluster, which must be "
                         "discarded before it can join the cluster.");

    console->print_info();
    console->print_info(target_instance.descr() +
                        " has the following errant GTIDs that do not exist "
                        "in the cluster:\n" +
                        errant_gtid_set);
    console->print_info();

    console->print_info(
        "Having extra GTID events is not expected, and it is "
        "recommended to investigate this further and ensure that the data "
        "can be removed prior to rejoining the instance to the cluster.");

    if (target_instance.get_version() >=
        mysqlshdk::mysql::k_mysql_clone_plugin_initial_version) {
      console->print_info();
      console->print_info(
          "Discarding these extra GTID events can either be done manually "
          "or by completely overwriting the state of " +
          target_instance.descr() +
          " with a physical snapshot from an existing cluster member. To "
          "achieve this remove the instance from the cluster and add it "
          "back using <Cluster>.<<<addInstance>>>() and setting the "
          "'recoveryMethod' option to 'clone'.");
    }

    console->print_info();

    throw shcore::Exception::runtime_error(
        "The instance '" + target_instance.descr() +
        "' contains errant transactions that did not originate from the "
        "cluster.");
  }
}

void Cluster_impl::adopt_from_gr() {
  shcore::Value ret_val;
  auto console = mysqlsh::current_console();

  auto newly_discovered_instances_list(get_newly_discovered_instances(
      *get_target_server(), get_metadata_storage(), get_id()));

  // Add all instances to the cluster metadata
  for (NewInstanceInfo &instance : newly_discovered_instances_list) {
    mysqlshdk::db::Connection_options newly_discovered_instance;

    newly_discovered_instance.set_host(instance.host);
    newly_discovered_instance.set_port(instance.port);

    log_info("Adopting member %s:%d from existing group", instance.host.c_str(),
             instance.port);
    console->println("Adding Instance '" + instance.host + ":" +
                     std::to_string(instance.port) + "'...");

    // TODO(somebody): what if the password is different on each server?
    // And what if is different from the current session?
    auto md_instance = get_metadata_storage()->get_md_server();

    auto session_data = md_instance->get_connection_options();

    newly_discovered_instance.set_login_options_from(session_data);

    add_metadata_for_instance(newly_discovered_instance);
  }
}

/** Iterates through all the cluster members in a given state calling the given
 * function on each of then.
 * @param states Vector of strings with the states of members on which the
 * functor will be called.
 * @param cnx_opt Connection options to be used to connect to the cluster
 * members
 * @param ignore_instances_vector Vector with addresses of instances to be
 * ignored even if their state is specified in the states vector.
 * @param functor Function that is called on each member of the cluster whose
 * state is specified in the states vector.
 * @param condition Optional condition function. If provided, the result will be
 * evaluated and the functor will only be called if the condition returns true.
 * @param ignore_network_conn_errors Optional flag to indicate whether
 * connection errors should be treated as errors and stop execution in remaining
 * instances, by default is set to true.
 */
void Cluster_impl::execute_in_members(
    const std::vector<mysqlshdk::gr::Member_state> &states,
    const mysqlshdk::db::Connection_options &cnx_opt,
    const std::vector<std::string> &ignore_instances_vector,
    const std::function<bool(const std::shared_ptr<Instance> &instance)>
        &functor,
    const std::function<
        bool(const Instance_md_and_gr_member &instance_with_state)> &condition,
    bool ignore_network_conn_errors) const {
  std::shared_ptr<Instance> instance_session;
  // Note (nelson): should we handle the super_read_only behavior here or should
  // it be the responsibility of the functor?
  auto instance_definitions = get_instances_with_state();

  for (auto &instance_def : instance_definitions) {
    // If the condition functor is given and the condition is not met then
    // continues to the next member
    if (condition && !condition(instance_def)) continue;

    std::string instance_address = instance_def.first.endpoint;

    // if instance is on the list of instances to be ignored, skip it
    if (std::find(ignore_instances_vector.begin(),
                  ignore_instances_vector.end(),
                  instance_address) != ignore_instances_vector.end()) {
      continue;
    }
    // if state list is given but it doesn't match, skip too
    if (!states.empty() &&
        std::find(states.begin(), states.end(), instance_def.second.state) ==
            states.end()) {
      continue;
    }

    auto target_coptions =
        shcore::get_connection_options(instance_address, false);

    target_coptions.set_login_options_from(cnx_opt);
    try {
      log_debug(
          "Opening a new session to instance '%s' while iterating "
          "cluster members",
          instance_address.c_str());
      instance_session = Instance::connect(
          target_coptions, current_shell_options()->get().wizards);
    } catch (const shcore::Error &e) {
      if (ignore_network_conn_errors && e.code() == CR_CONN_HOST_ERROR) {
        log_error("Could not open connection to '%s': %s, but ignoring it.",
                  instance_address.c_str(), e.what());
        continue;
      } else {
        log_error("Could not open connection to '%s': %s",
                  instance_address.c_str(), e.what());
        throw;
      }
    } catch (const std::exception &e) {
      log_error("Could not open connection to '%s': %s",
                instance_address.c_str(), e.what());
      throw;
    }
    bool continue_loop = functor(instance_session);
    if (!continue_loop) {
      log_debug("Cluster iteration stopped because functor returned false.");
      break;
    }
  }
}

mysqlshdk::db::Connection_options Cluster_impl::pick_seed_instance() const {
  bool single_primary;
  std::string primary_uuid = mysqlshdk::gr::get_group_primary_uuid(
      *get_target_server(), &single_primary);
  if (single_primary) {
    if (!primary_uuid.empty()) {
      Instance_metadata info =
          get_metadata_storage()->get_instance_by_uuid(primary_uuid);

      mysqlshdk::db::Connection_options coptions(info.endpoint);
      mysqlshdk::db::Connection_options group_session_target(
          get_target_server()->get_connection_options());

      coptions.set_login_options_from(group_session_target);

      return coptions;
    }
    throw shcore::Exception::runtime_error(
        "Unable to determine a suitable peer instance to join the group");
  } else {
    // instance we're connected to should be OK if we're multi-master
    return get_target_server()->get_connection_options();
  }
}

void Cluster_impl::validate_variable_compatibility(
    const mysqlshdk::mysql::IInstance &target_instance,
    const std::string &var_name) const {
  auto cluster_inst = get_target_server();
  auto instance_val = target_instance.get_sysvar_string(var_name).get_safe();
  auto cluster_val = cluster_inst->get_sysvar_string(var_name).get_safe();
  std::string instance_address =
      target_instance.get_connection_options().as_uri(
          mysqlshdk::db::uri::formats::only_transport());
  log_info(
      "Validating if '%s' variable has the same value on target instance '%s' "
      "as it does on the cluster.",
      var_name.c_str(), instance_address.c_str());
  // If values are different between cluster and target instance throw an
  // exception.
  if (instance_val != cluster_val) {
    auto console = mysqlsh::current_console();
    console->print_error(shcore::str_format(
        "Cannot join instance '%s' to cluster: incompatible '%s' value.",
        instance_address.c_str(), var_name.c_str()));
    throw shcore::Exception::runtime_error(
        shcore::str_format("The '%s' value '%s' of the instance '%s' is "
                           "different from the value of the cluster '%s'.",
                           var_name.c_str(), instance_val.c_str(),
                           instance_address.c_str(), cluster_val.c_str()));
  }
}

/**
 * Get an up-to-date group seeds value based on the current list of active
 * members.
 *
 * An optional instance_session parameter can be provided that will be used to
 * get its current GR group seeds value and add the local address from the
 * active members (avoiding duplicates) to that initial value, allowing to
 * preserve the GR group seeds of the specified instance. If no
 * instance_session is given (nullptr) then the returned groups seeds value
 * will only be based on the currently active members, disregarding any current
 * GR group seed value on any instance (allowing to reset the group seed only
 * based on the currently active members).
 *
 * @param target_instance Session to the target instance to get the group
 *                         seeds for.
 * @return a string with a comma separated list of all the GR local address
 *         values of the currently active (ONLINE or RECOVERING) instances in
 *         the cluster.
 */
std::string Cluster_impl::get_cluster_group_seeds(
    const std::shared_ptr<Instance> &target_instance) const {
  // Get connection option for the metadata.

  std::shared_ptr<Instance> cluster_instance = get_target_server();
  Connection_options cluster_cnx_opt =
      cluster_instance->get_connection_options();

  // Get list of active instances (ONLINE or RECOVERING)
  std::vector<Instance_metadata> active_instances = get_active_instances();

  std::vector<std::string> gr_group_seeds_list;
  // If the target instance is provided, use its current GR group seed variable
  // value as starting point to append new (missing) values to it.
  if (target_instance) {
    // Get the instance GR group seeds and save it to the GR group seeds list.
    std::string gr_group_seeds = *target_instance->get_sysvar_string(
        "group_replication_group_seeds",
        mysqlshdk::mysql::Var_qualifier::GLOBAL);
    if (!gr_group_seeds.empty()) {
      gr_group_seeds_list = shcore::split_string(gr_group_seeds, ",");
    }
  }

  // Get the update GR group seed from local address of all active instances.
  for (Instance_metadata &instance_def : active_instances) {
    std::string instance_address = instance_def.endpoint;
    Connection_options target_coptions =
        shcore::get_connection_options(instance_address, false);
    // It is assumed that the same user and password is used by all members.
    target_coptions.set_login_options_from(cluster_cnx_opt);

    // Connect to the instance.
    std::shared_ptr<Instance> instance;
    try {
      log_debug(
          "Connecting to instance '%s' to get its value for the "
          "group_replication_local_address variable.",
          instance_address.c_str());
      instance = Instance::connect(target_coptions,
                                   current_shell_options()->get().wizards);
    } catch (const shcore::Error &e) {
      if (mysqlshdk::db::is_mysql_client_error(e.code())) {
        log_info(
            "Could not connect to instance '%s', its local address will not "
            "be used for the group seeds: %s",
            instance_address.c_str(), e.format().c_str());
        break;
      } else {
        throw shcore::Exception::runtime_error("While connecting to " +
                                               target_coptions.uri_endpoint() +
                                               ": " + e.what());
      }
    }

    // Get the instance GR local address and add it to the GR group seeds list.
    std::string local_address =
        *instance->get_sysvar_string("group_replication_local_address",
                                     mysqlshdk::mysql::Var_qualifier::GLOBAL);
    if (std::find(gr_group_seeds_list.begin(), gr_group_seeds_list.end(),
                  local_address) == gr_group_seeds_list.end()) {
      // Only add the local address if not already in the group seed list,
      // avoiding duplicates.
      gr_group_seeds_list.push_back(local_address);
    }
  }

  return shcore::str_join(gr_group_seeds_list, ",");
}

std::vector<Instance_metadata> Cluster_impl::get_instances(
    const std::vector<mysqlshdk::gr::Member_state> &states) const {
  std::vector<Instance_metadata> all_instances =
      get_metadata_storage()->get_all_instances(get_id());

  if (states.empty()) {
    return all_instances;
  } else {
    std::vector<Instance_metadata> res;
    std::vector<mysqlshdk::gr::Member> members(
        mysqlshdk::gr::get_members(*get_target_server()));

    for (const auto &i : all_instances) {
      auto m = std::find_if(members.begin(), members.end(),
                            [&i](const mysqlshdk::gr::Member &member) {
                              return member.uuid == i.uuid;
                            });
      if (m != members.end() &&
          std::find(states.begin(), states.end(), m->state) != states.end()) {
        res.push_back(i);
      }
    }
    return res;
  }
}

void Cluster_impl::ensure_metadata_has_server_id(
    const mysqlshdk::mysql::IInstance &target_instance) {
  execute_in_members(
      {}, target_instance.get_connection_options(), {},
      [this](const std::shared_ptr<Instance> &instance) {
        get_metadata_storage()->update_instance_attribute(
            instance->get_uuid(), k_instance_attribute_server_id,
            shcore::Value(instance->get_server_id()));

        return true;
      },
      [](const Instance_md_and_gr_member &md) {
        return md.first.server_id == 0 &&
               (md.second.state == mysqlshdk::gr::Member_state::ONLINE ||
                md.second.state == mysqlshdk::gr::Member_state::RECOVERING);
      });
}

void Cluster_impl::ensure_metadata_has_recovery_accounts() {
  auto endpoints =
      get_metadata_storage()->get_instances_with_recovery_accounts(get_id());

  if (!endpoints.empty()) {
    log_info("Fixing instances missing the replication recovery account...");

    auto console = mysqlsh::current_console();

    execute_in_members(
        {}, get_target_server()->get_connection_options(), {},
        [this, &console,
         &endpoints](const std::shared_ptr<Instance> &instance) {
          std::string recovery_user =
              mysqlshdk::gr::get_recovery_user(*instance);

          bool recovery_is_valid = false;
          if (!recovery_user.empty()) {
            recovery_is_valid =
                shcore::str_beginswith(
                    recovery_user,
                    mysqlshdk::gr::k_group_recovery_old_user_prefix) ||
                shcore::str_beginswith(
                    recovery_user, mysqlshdk::gr::k_group_recovery_user_prefix);
          }

          if (!recovery_user.empty()) {
            if (!recovery_is_valid) {
              console->print_error(
                  "Unsupported recovery account '" + recovery_user +
                  "' has been found for instance '" + instance->descr() +
                  "'. Operations such as "
                  "<Cluster>.<<<resetRecoveryAccountsPassword>>>() and "
                  "<Cluster>.<<<addInstance>>>() may fail. Please remove and "
                  "add the instance back to the Cluster to ensure a supported "
                  "recovery account is used.");
              return true;
            }

            auto it = endpoints.find(instance->get_uuid());
            if (it != endpoints.end()) {
              if (it->second != recovery_user) {
                get_metadata_storage()->update_instance_recovery_account(
                    instance->get_uuid(), recovery_user, "%");
              }
            }

          } else {
            throw std::logic_error(
                "Recovery user account not found for server address: " +
                instance->descr() + " with UUID " + instance->get_uuid());
          }

          return true;
        },
        [](const Instance_md_and_gr_member &md) {
          return (md.second.state == mysqlshdk::gr::Member_state::ONLINE ||
                  md.second.state == mysqlshdk::gr::Member_state::RECOVERING);
        });
  }
}

std::vector<Instance_metadata> Cluster_impl::get_active_instances(
    bool online_only) const {
  if (online_only)
    return get_instances({mysqlshdk::gr::Member_state::ONLINE});
  else
    return get_instances({mysqlshdk::gr::Member_state::ONLINE,
                          mysqlshdk::gr::Member_state::RECOVERING});
}

std::shared_ptr<mysqlsh::dba::Instance> Cluster_impl::get_online_instance(
    const std::string &exclude_uuid) const {
  std::vector<Instance_metadata> instances(get_active_instances());

  // Get the cluster connection credentials to use to connect to instances.
  mysqlshdk::db::Connection_options cluster_cnx_opts =
      get_target_server()->get_connection_options();

  for (const auto &instance : instances) {
    // Skip instance with the provided UUID exception (if specified).
    if (exclude_uuid.empty() || instance.uuid != exclude_uuid) {
      try {
        // Use the cluster connection credentials.
        mysqlshdk::db::Connection_options coptions(instance.endpoint);
        coptions.set_login_options_from(cluster_cnx_opts);

        log_info("Opening session to the member of InnoDB cluster at %s...",
                 coptions.as_uri().c_str());

        // Return the first valid (reachable) instance.
        return Instance::connect(coptions);
      } catch (const std::exception &e) {
        log_debug(
            "Unable to establish a session to the cluster member '%s': %s",
            instance.endpoint.c_str(), e.what());
      }
    }
  }

  // No reachable online instance was found.
  return nullptr;
}

/**
 * Check the instance server UUID of the specified instance.
 *
 * The server UUID must be unique for all instances in a cluster. This function
 * checks if the server_uuid of the target instance is unique among all
 * members of the cluster.
 *
 * @param instance_session Session to the target instance to check its server
 *                         UUID.
 */
void Cluster_impl::validate_server_uuid(
    const mysqlshdk::mysql::IInstance &instance) const {
  // Get the server_uuid of the target instance.
  std::string server_uuid = *instance.get_sysvar_string(
      "server_uuid", mysqlshdk::mysql::Var_qualifier::GLOBAL);

  // Get connection option for the metadata.

  std::shared_ptr<Instance> cluster_instance = get_target_server();
  Connection_options cluster_cnx_opt =
      cluster_instance->get_connection_options();

  // Get list of instances in the metadata
  std::vector<Instance_metadata> metadata_instances = get_instances();

  // Get and compare the server UUID of all instances with the one of
  // the target instance.
  for (Instance_metadata &instance_def : metadata_instances) {
    if (server_uuid == instance_def.uuid) {
      // Raise an error if the server uuid is the same of a cluster member.
      throw shcore::Exception::runtime_error(
          "Cannot add an instance with the same server UUID (" + server_uuid +
          ") of an active member of the cluster '" + instance_def.endpoint +
          "'. Please change the server UUID of the instance to add, all "
          "members must have a unique server UUID.");
    }
  }
}

void Cluster_impl::validate_server_id(
    const mysqlshdk::mysql::IInstance &target_instance) const {
  // Get the server_id of the target instance.
  uint32_t server_id = target_instance.get_server_id();

  // Get connection option for the metadata.
  std::shared_ptr<Instance> cluster_instance = get_target_server();
  Connection_options cluster_cnx_opt =
      cluster_instance->get_connection_options();

  // Get list of instances in the metadata
  std::vector<Instance_metadata> metadata_instances = get_instances();

  // Get and compare the server_id of all instances with the one of
  // the target instance.
  for (const Instance_metadata &instance : metadata_instances) {
    if (server_id != 0 && server_id == instance.server_id) {
      throw std::runtime_error{"The server_id '" + std::to_string(server_id) +
                               "' is already used by instance '" +
                               instance.endpoint + "'."};
    }
  }
}

std::vector<Instance_md_and_gr_member> Cluster_impl::get_instances_with_state()
    const {
  std::vector<std::pair<Instance_metadata, mysqlshdk::gr::Member>> ret;

  std::vector<mysqlshdk::gr::Member> members(
      mysqlshdk::gr::get_members(*get_target_server()));

  std::vector<Instance_metadata> md(get_instances());

  for (const auto &i : md) {
    auto m = std::find_if(members.begin(), members.end(),
                          [&i](const mysqlshdk::gr::Member &member) {
                            return member.uuid == i.uuid;
                          });
    if (m != members.end()) {
      ret.push_back({i, *m});
    } else {
      mysqlshdk::gr::Member mm;
      mm.uuid = i.uuid;
      mm.state = mysqlshdk::gr::Member_state::MISSING;
      ret.push_back({i, mm});
    }
  }

  return ret;
}

std::unique_ptr<mysqlshdk::config::Config> Cluster_impl::create_config_object(
    const std::vector<std::string> &ignored_instances, bool skip_invalid_state,
    bool persist_only) const {
  auto cfg = std::make_unique<mysqlshdk::config::Config>();

  auto console = mysqlsh::current_console();

  // Get all cluster instances, including state information to update
  // auto-increment values.
  std::vector<std::pair<Instance_metadata, mysqlshdk::gr::Member>>
      instance_defs = get_instances_with_state();

  for (const auto &instance_def : instance_defs) {
    // If instance is on the list of instances to be ignored, skip it.
    if (std::find(ignored_instances.begin(), ignored_instances.end(),
                  instance_def.first.endpoint) != ignored_instances.end()) {
      continue;
    }

    // Use the GR state hold by instance_def.state (but convert it to a proper
    // mysqlshdk::gr::Member_state to be handled properly).
    mysqlshdk::gr::Member_state state = instance_def.second.state;

    if (state == mysqlshdk::gr::Member_state::ONLINE ||
        state == mysqlshdk::gr::Member_state::RECOVERING) {
      // Set login credentials to connect to instance.
      // NOTE: It is assumed that the same login credentials can be used to
      //       connect to all cluster instances.
      Connection_options instance_cnx_opts =
          shcore::get_connection_options(instance_def.first.endpoint, false);
      instance_cnx_opts.set_login_options_from(
          get_target_server()->get_connection_options());

      // Try to connect to instance.
      log_debug("Connecting to instance '%s'",
                instance_def.first.endpoint.c_str());
      std::shared_ptr<mysqlsh::dba::Instance> instance;
      try {
        instance = Instance::connect(instance_cnx_opts);
        log_debug("Successfully connected to instance");
      } catch (const std::exception &err) {
        log_debug("Failed to connect to instance: %s", err.what());
        console->print_error(
            "Unable to connect to instance '" + instance_def.first.endpoint +
            "'. Please verify connection credentials and make sure the "
            "instance is available.");

        throw shcore::Exception::runtime_error(err.what());
      }

      // Determine if SET PERSIST is supported.
      mysqlshdk::utils::nullable<bool> support_set_persist =
          instance->is_set_persist_supported();
      mysqlshdk::mysql::Var_qualifier set_type =
          mysqlshdk::mysql::Var_qualifier::GLOBAL;
      bool skip = false;
      if (!support_set_persist.is_null() && *support_set_persist) {
        if (persist_only) {
          set_type = mysqlshdk::mysql::Var_qualifier::PERSIST_ONLY;
        } else {
          set_type = mysqlshdk::mysql::Var_qualifier::PERSIST;
        }
      } else {
        // If we want persist_only but it's not supported, we skip it since it
        // can't help us
        if (persist_only) skip = true;
      }

      // Add configuration handler for server.
      if (!skip) {
        cfg->add_handler(
            instance_def.first.endpoint,
            std::unique_ptr<mysqlshdk::config::IConfig_handler>(
                std::make_unique<mysqlshdk::config::Config_server_handler>(
                    instance, set_type)));
      }

      // Print a warning if SET PERSIST is not supported, for users to execute
      // dba.configureLocalInstance().
      if (support_set_persist.is_null()) {
        console->print_warning(
            "Instance '" + instance_def.first.endpoint +
            "' cannot persist configuration since MySQL version " +
            instance->get_version().get_base() +
            " does not support the SET PERSIST command "
            "(MySQL version >= 8.0.11 required). Please use the dba." +
            "<<<configureLocalInstance>>>() command locally to persist the "
            "changes.");
      } else if (!*support_set_persist) {
        console->print_warning(
            "Instance '" + instance_def.first.endpoint +
            "' will not load the persisted cluster configuration upon reboot "
            "since 'persisted-globals-load' is set to 'OFF'. Please use the "
            "dba.<<<configureLocalInstance>>>"
            "() command locally to persist the changes or set "
            "'persisted-globals-load' to 'ON' on the configuration file.");
      }
    } else {
      // Ignore instance with an invalid state (i.e., do not issue an erro), if
      // skip_invalid_state is true.
      if (skip_invalid_state) continue;

      // Issue an error if the instance is not active.
      console->print_error("The settings cannot be updated for instance '" +
                           instance_def.first.endpoint +
                           "' because it is on a '" +
                           mysqlshdk::gr::to_string(state) + "' state.");

      throw shcore::Exception::runtime_error(
          "The instance '" + instance_def.first.endpoint + "' is '" +
          mysqlshdk::gr::to_string(state) + "'");
    }
  }

  return cfg;
}

namespace {
template <typename T>
struct Option_info {
  bool found_non_default = false;
  bool found_not_supported = false;
  T non_default_value;
};
}  // namespace

void Cluster_impl::query_group_wide_option_values(
    mysqlshdk::mysql::IInstance *target_instance,
    mysqlshdk::utils::nullable<std::string> *out_gr_consistency,
    mysqlshdk::utils::nullable<int64_t> *out_gr_member_expel_timeout) const {
  auto console = mysqlsh::current_console();

  Option_info<std::string> gr_consistency;
  Option_info<int64_t> gr_member_expel_timeout;

  std::vector<std::string> skip_list;
  skip_list.push_back(target_instance->get_canonical_address());

  // loop though all members to check if there is any member that doesn't:
  // - have support for the group_replication_consistency option (null value)
  // or a member that doesn't have the default value. The default value
  // Eventual has the same behavior as members had before the option was
  // introduced. As such, having that value or having no support for the
  // group_replication_consistency is the same.
  // - have support for the group_replication_member_expel_timeout option
  // (null value) or a member that doesn't have the default value. The default
  // value 0 has the same behavior as members had before the option was
  // introduced. As such, having the 0 value or having no support for the
  // group_replication_member_expel_timeout is the same.
  execute_in_members({mysqlshdk::gr::Member_state::ONLINE,
                      mysqlshdk::gr::Member_state::RECOVERING},
                     get_target_server()->get_connection_options(), skip_list,
                     [&gr_consistency, &gr_member_expel_timeout](
                         const std::shared_ptr<Instance> &instance) {
                       {
                         mysqlshdk::utils::nullable<std::string> value;
                         value = instance->get_sysvar_string(
                             "group_replication_consistency",
                             mysqlshdk::mysql::Var_qualifier::GLOBAL);

                         if (value.is_null()) {
                           gr_consistency.found_not_supported = true;
                         } else if (*value != "EVENTUAL" && *value != "0") {
                           gr_consistency.found_non_default = true;
                           gr_consistency.non_default_value = *value;
                         }
                       }

                       {
                         mysqlshdk::utils::nullable<std::int64_t> value;
                         value = instance->get_sysvar_int(
                             "group_replication_member_expel_timeout",
                             mysqlshdk::mysql::Var_qualifier::GLOBAL);

                         if (value.is_null()) {
                           gr_member_expel_timeout.found_not_supported = true;
                         } else if (*value != 0) {
                           gr_member_expel_timeout.found_non_default = true;
                           gr_member_expel_timeout.non_default_value = *value;
                         }
                       }
                       // if we have found both a instance that doesnt have
                       // support for the option and an instance that doesn't
                       // have the default value, then we don't need to look at
                       // any other instance on the cluster.
                       return !(gr_consistency.found_not_supported &&
                                gr_consistency.found_non_default &&
                                gr_member_expel_timeout.found_not_supported &&
                                gr_member_expel_timeout.found_non_default);
                     });

  if (target_instance->get_version() < mysqlshdk::utils::Version(8, 0, 14)) {
    if (gr_consistency.found_non_default) {
      console->print_warning(
          "The " + gr_consistency.non_default_value +
          " consistency value of the cluster "
          "is not supported by the instance '" +
          target_instance->get_connection_options().uri_endpoint() +
          "' (version >= 8.0.14 is required). In single-primary mode, upon "
          "failover, the member with the lowest version is the one elected as"
          " primary.");
    }
  } else {
    *out_gr_consistency = "EVENTUAL";

    if (gr_consistency.found_non_default) {
      // if we found any non default group_replication_consistency value, then
      // we use that value on the instance being added
      *out_gr_consistency = gr_consistency.non_default_value;

      if (gr_consistency.found_not_supported) {
        console->print_warning(
            "The instance '" +
            target_instance->get_connection_options().uri_endpoint() +
            "' inherited the " + gr_consistency.non_default_value +
            " consistency value from the cluster, however some instances on "
            "the group do not support this feature (version < 8.0.14). In "
            "single-primary mode, upon failover, the member with the lowest "
            "version will be the one elected and it doesn't support this "
            "option.");
      }
    }
  }

  if (target_instance->get_version() < mysqlshdk::utils::Version(8, 0, 13)) {
    if (gr_member_expel_timeout.found_non_default) {
      console->print_warning(
          "The expelTimeout value of the cluster '" +
          std::to_string(gr_member_expel_timeout.non_default_value) +
          "' is not supported by the instance '" +
          target_instance->get_connection_options().uri_endpoint() +
          "' (version >= 8.0.13 is required). A member "
          "that doesn't have support for the expelTimeout option has the "
          "same behavior as a member with expelTimeout=0.");
    }
  } else {
    *out_gr_member_expel_timeout = int64_t();

    if (gr_member_expel_timeout.found_non_default) {
      // if we found any non default group_replication_member_expel_timeout
      // value, then we use that value on the instance being added
      *out_gr_member_expel_timeout = gr_member_expel_timeout.non_default_value;

      if (gr_member_expel_timeout.found_not_supported) {
        console->print_warning(
            "The instance '" +
            target_instance->get_connection_options().uri_endpoint() +
            "' inherited the '" +
            std::to_string(gr_member_expel_timeout.non_default_value) +
            "' consistency value from the cluster, however some instances on "
            "the group do not support this feature (version < 8.0.13). There "
            "is a possibility that the cluster member (killer node), "
            "responsible for expelling the member suspected of having "
            "failed, does not support the expelTimeout option. In "
            "this case the behavior would be the same as if having "
            "expelTimeout=0.");
      }
    }
  }
}

void Cluster_impl::update_group_members_for_removed_member(
    const std::string &local_gr_address) {
  // Get the Cluster Config Object
  auto cfg = create_config_object({}, true);

  // Iterate through all ONLINE and RECOVERING cluster members and update
  // their group_replication_group_seeds value by removing the
  // gr_local_address of the instance that was removed
  log_debug("Updating group_replication_group_seeds of cluster members");
  mysqlshdk::gr::update_group_seeds(
      cfg.get(), local_gr_address, mysqlshdk::gr::Gr_seeds_change_type::REMOVE);
  cfg->apply();

  // Update the auto-increment values
  {
    // Auto-increment values must be updated according to:
    //
    // Set auto-increment for single-primary topology:
    // - auto_increment_increment = 1
    // - auto_increment_offset = 2
    //
    // Set auto-increment for multi-primary topology:
    // - auto_increment_increment = n;
    // - auto_increment_offset = 1 + server_id % n;
    // where n is the size of the GR group if > 7, otherwise n = 7.
    //
    // We must update the auto-increment values in Remove_instance for 2
    // scenarios
    //   - Multi-primary Cluster
    //   - Cluster that had more 7 or more members before the
    //   Remove_instance
    //     operation
    //
    // NOTE: in the other scenarios, the Add_instance operation is in charge
    // of updating auto-increment accordingly

    mysqlshdk::gr::Topology_mode topology_mode =
        get_metadata_storage()->get_cluster_topology_mode(get_id());

    // Get the current number of members of the Cluster
    uint64_t cluster_member_count =
        get_metadata_storage()->get_cluster_size(get_id());

    bool update_auto_inc = (cluster_member_count + 1) > 7 ? true : false;

    if (topology_mode == mysqlshdk::gr::Topology_mode::MULTI_PRIMARY &&
        update_auto_inc) {
      // Call update_auto_increment to do the job in all instances
      mysqlshdk::gr::update_auto_increment(
          cfg.get(), mysqlshdk::gr::Topology_mode::MULTI_PRIMARY);

      cfg->apply();
    }
  }
}

void Cluster_impl::add_instance(
    const mysqlshdk::db::Connection_options &instance_def,
    const cluster::Add_instance_options &options,
    Recovery_progress_style progress_style) {
  check_preconditions("addInstance");

  auto primary = acquire_primary();
  auto finally_primary =
      shcore::on_leave_scope([this]() { release_primary(); });

  Scoped_instance target(
      connect_target_instance(instance_def, true, true, true));
  cluster::Cluster_join joiner(this, primary, target, options.gr_options,
                               options.clone_options, options.interactive());

  // Add the Instance to the Cluster

  // Prepare and validate
  joiner.prepare_join(options.label);

  std::string msg =
      "A new instance will be added to the InnoDB cluster. Depending on the "
      "amount of data on the cluster this might take from a few seconds to "
      "several hours.";
  std::string message =
      mysqlshdk::textui::format_markup_text({msg}, 80, 0, false);
  auto console = current_console();
  console->print_info(message);
  console->print_info();

  joiner.join(progress_style);

  // Verification step to ensure the server_id is an attribute on all the
  // instances of the cluster
  ensure_metadata_has_server_id(target);
}

void Cluster_impl::rejoin_instance(const Connection_options &instance_def,
                                   const Group_replication_options &gr_options,
                                   bool interactive,
                                   bool skip_precondition_check) {
  // rejoin the Instance to the Cluster
  if (!skip_precondition_check) check_preconditions("rejoinInstance");

  auto primary = acquire_primary();
  auto finally_primary =
      shcore::on_leave_scope([this]() { release_primary(); });

  Scoped_instance target(
      connect_target_instance(instance_def, true, false, true));
  cluster::Cluster_join joiner(this, primary, target, gr_options, {},
                               interactive);

  // Rejoin the Instance to the Cluster
  bool uuid_mistmatch = false;
  if (joiner.prepare_rejoin(&uuid_mistmatch)) {
    joiner.rejoin();
  }

  if (uuid_mistmatch) {
    update_metadata_for_instance(target);
  }

  // Verification step to ensure the server_id is an attribute on all the
  // instances of the cluster
  ensure_metadata_has_server_id(target);
}

void Cluster_impl::remove_instance(const Connection_options &instance_def,
                                   const mysqlshdk::null_bool &force,
                                   const bool interactive) {
  check_preconditions("removeInstance");

  acquire_primary();
  auto finally_primary =
      shcore::on_leave_scope([this]() { release_primary(); });

  // Create the remove_instance command and execute it.
  cluster::Remove_instance op_remove_instance(instance_def, interactive, force,
                                              this);
  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope(
      [&op_remove_instance]() { op_remove_instance.finish(); });
  // Prepare the remove_instance command execution (validations).
  op_remove_instance.prepare();
  // Execute remove_instance operations.
  op_remove_instance.execute();
}

std::shared_ptr<Instance> connect_to_instance_for_metadata(
    const mysqlshdk::db::Connection_options &instance_definition) {
  std::string instance_address =
      instance_definition.as_uri(mysqlshdk::db::uri::formats::only_transport());

  log_debug("Connecting to '%s' to query for metadata information...",
            instance_address.c_str());

  std::shared_ptr<Instance> classic;
  try {
    classic = Instance::connect(instance_definition,
                                current_shell_options()->get().wizards);
  } catch (const shcore::Error &e) {
    std::stringstream ss;
    ss << "Error opening session to '" << instance_address << "': " << e.what();
    log_warning("%s", ss.str().c_str());

    // TODO(alfredo) - this should be validated before adding the instance
    // Check if we're adopting a GR cluster, if so, it could happen that
    // we can't connect to it because root@localhost exists but root@hostname
    // doesn't (GR keeps the hostname in the members table)
    if (e.code() == ER_ACCESS_DENIED_ERROR) {
      std::stringstream se;
      se << "Access denied connecting to instance " << instance_address << ".\n"
         << "Please ensure all instances in the same group/cluster have"
            " the same password for account '"
         << instance_definition.get_user()
         << "' and that it is accessible from the host mysqlsh is running "
            "from.";
      throw shcore::Exception::runtime_error(se.str());
    }
    throw shcore::Exception::runtime_error(ss.str());
  }

  return classic;
}

void Cluster_impl::add_metadata_for_instance(
    const mysqlshdk::db::Connection_options &instance_definition,
    const std::string &label) const {
  log_debug("Adding instance to metadata");

  auto instance = connect_to_instance_for_metadata(instance_definition);

  add_metadata_for_instance(*instance, label);
}

void Cluster_impl::add_metadata_for_instance(
    const mysqlshdk::mysql::IInstance &instance,
    const std::string &label) const {
  Instance_metadata instance_md(query_instance_info(instance));

  if (!label.empty()) instance_md.label = label;

  instance_md.cluster_id = get_id();

  // Add the instance to the metadata.
  get_metadata_storage()->insert_instance(instance_md);
}

void Cluster_impl::update_metadata_for_instance(
    const mysqlshdk::db::Connection_options &instance_definition) const {
  auto instance = connect_to_instance_for_metadata(instance_definition);

  update_metadata_for_instance(*instance);
}

void Cluster_impl::update_metadata_for_instance(
    const mysqlshdk::mysql::IInstance &instance) const {
  log_debug("Updating instance metadata");

  auto console = mysqlsh::current_console();
  console->print_info("Updating instance metadata...");

  Instance_metadata instance_md(query_instance_info(instance));

  instance_md.cluster_id = get_id();

  // Updates the instance metadata.
  get_metadata_storage()->update_instance(instance_md);

  console->print_info("The instance metadata for '" +
                      instance.get_canonical_address() +
                      "' was successfully updated.");
  console->print_info();
}

void Cluster_impl::remove_instance_metadata(
    const mysqlshdk::db::Connection_options &instance_def) {
  log_debug("Removing instance from metadata");

  std::string port = std::to_string(instance_def.get_port());

  std::string host = instance_def.get_host();

  // Check if the instance was already added
  std::string instance_address = host + ":" + port;

  get_metadata_storage()->remove_instance(instance_address);
}

shcore::Value Cluster_impl::describe() {
  // Throw an error if the cluster has already been dissolved

  check_preconditions("describe");

  // Create the Cluster_describe command and execute it.
  cluster::Describe op_describe(*this);
  // Always execute finish when leaving "try catch".
  auto finally =
      shcore::on_leave_scope([&op_describe]() { op_describe.finish(); });
  // Prepare the Cluster_describe command execution (validations).
  op_describe.prepare();
  // Execute Cluster_describe operations.
  return op_describe.execute();
}

shcore::Value Cluster_impl::status(uint64_t extended) {
  // Throw an error if the cluster has already been dissolved
  check_preconditions("status");

  // Create the Cluster_status command and execute it.
  cluster::Status op_status(*this, extended);
  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope([&op_status]() { op_status.finish(); });
  // Prepare the Cluster_status command execution (validations).
  op_status.prepare();
  // Execute Cluster_status operations.
  return op_status.execute();
}

shcore::Value Cluster_impl::list_routers(bool only_upgrade_required) {
  shcore::Dictionary_t dict = shcore::make_dict();

  (*dict)["clusterName"] = shcore::Value(get_name());
  (*dict)["routers"] = router_list(get_metadata_storage().get(), get_id(),
                                   only_upgrade_required);

  return shcore::Value(dict);
}

void Cluster_impl::reset_recovery_password(const mysqlshdk::null_bool &force,
                                           const bool interactive) {
  check_preconditions("resetRecoveryAccountsPassword");

  // Create the reset_recovery_command.
  cluster::Reset_recovery_accounts_password op_reset(interactive, force, *this);
  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope([&op_reset]() { op_reset.finish(); });
  // Prepare the reset_recovery_accounts_password execution
  op_reset.prepare();
  // Execute the reset_recovery_accounts_password command.
  op_reset.execute();
}

void Cluster_impl::setup_admin_account(const std::string &username,
                                       const std::string &host,
                                       const Setup_account_options &options) {
  check_preconditions("setupAdminAccount");
  Base_cluster_impl::setup_admin_account(username, host, options);
}

void Cluster_impl::setup_router_account(const std::string &username,
                                        const std::string &host,
                                        const Setup_account_options &options) {
  check_preconditions("setupRouterAccount");
  Base_cluster_impl::setup_router_account(username, host, options);
}

shcore::Value Cluster_impl::options(const bool all) {
  // Throw an error if the cluster has already been dissolved
  check_preconditions("options");

  // Create the Cluster_options command and execute it.
  cluster::Options op_option(*this, all);
  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope([&op_option]() { op_option.finish(); });
  // Prepare the Cluster_options command execution (validations).
  op_option.prepare();

  // Execute Cluster_options operations.
  shcore::Value res = op_option.execute();

  return res;
}

void Cluster_impl::dissolve(const mysqlshdk::null_bool &force,
                            const bool interactive) {
  // We need to check if the group has quorum and if not we must abort the
  // operation otherwise GR blocks the writes to preserve the consistency
  // of the group and we end up with a hang.
  // This check is done at check_preconditions()
  check_preconditions("dissolve");

  acquire_primary();
  auto finally_primary =
      shcore::on_leave_scope([this]() { release_primary(); });

  // Dissolve the Cluster
  try {
    // Create the Dissolve command and execute it.
    cluster::Dissolve op_dissolve(interactive, force, this);
    // Always execute finish when leaving "try catch".
    auto finally =
        shcore::on_leave_scope([&op_dissolve]() { op_dissolve.finish(); });
    // Prepare the dissolve command execution (validations).
    op_dissolve.prepare();
    // Execute dissolve operations.
    op_dissolve.execute();
  } catch (...) {
    throw;
  }
}

void Cluster_impl::force_quorum_using_partition_of(
    const Connection_options &instance_def, const bool interactive) {
  check_preconditions("forceQuorumUsingPartitionOf");
  std::shared_ptr<Instance> target_instance;
  const auto console = current_console();

  std::vector<Instance_metadata> online_instances = get_active_instances();

  std::vector<std::string> online_instances_array;
  for (const auto &instance : online_instances) {
    online_instances_array.push_back(instance.endpoint);
  }

  if (online_instances_array.empty()) {
    throw shcore::Exception::logic_error(
        "No online instances are visible from the given one.");
  }

  auto group_peers_print = shcore::str_join(online_instances_array, ",");

  // Remove the trailing comma of group_peers_print
  if (group_peers_print.back() == ',') group_peers_print.pop_back();

  if (interactive) {
    std::string message = "Restoring cluster '" + get_name() +
                          "' from loss of quorum, by using the partition "
                          "composed of [" +
                          group_peers_print + "]\n\n";
    console->print(message);

    console->println("Restoring the InnoDB cluster ...");
    console->println();
  }

  std::string instance_address =
      instance_def.as_uri(mysqlshdk::db::uri::formats::only_transport());

  // TODO(miguel): test if there's already quorum and add a 'force' option to be
  // used if so

  // TODO(miguel): test if the instance if part of the current cluster, for the
  // scenario of restoring a cluster quorum from another

  try {
    log_info("Opening a new session to the partition instance %s",
             instance_address.c_str());
    target_instance = Instance::connect(
        instance_def, current_shell_options()->get().wizards, true);
  }
  CATCH_REPORT_AND_THROW_CONNECTION_ERROR(instance_address)

  // Before rejoining an instance we must verify if the instance's
  // 'group_replication_group_name' matches the one registered in the
  // Metadata (BUG #26159339)
  {
    // Get instance address in metadata.
    std::string md_address = target_instance->get_canonical_address();

    // Check if the instance belongs to the Cluster on the Metadata
    if (!contains_instance_with_address(md_address)) {
      std::string message = "The instance '" + instance_address + "'";
      message.append(" does not belong to the cluster: '" + get_name() + "'.");
      throw shcore::Exception::runtime_error(message);
    }

    if (!validate_cluster_group_name(*target_instance, get_group_name())) {
      std::string nice_error =
          "The instance '" + instance_address +
          "' "
          "cannot be used to restore the cluster as it "
          "may belong to a different cluster as the one registered "
          "in the Metadata since the value of "
          "'group_replication_group_name' does not match the one "
          "registered in the cluster's Metadata: possible split-brain "
          "scenario.";

      throw shcore::Exception::runtime_error(nice_error);
    }
  }

  // Get the instance state
  Cluster_check_info state;

  auto instance_type = get_gr_instance_type(*target_instance);

  if (instance_type != InstanceType::Standalone &&
      instance_type != InstanceType::StandaloneWithMetadata &&
      instance_type != InstanceType::StandaloneInMetadata) {
    state = get_replication_group_state(*target_instance, instance_type);

    if (state.source_state != ManagedInstance::OnlineRW &&
        state.source_state != ManagedInstance::OnlineRO) {
      std::string message = "The instance '" + instance_address + "'";
      message.append(" cannot be used to restore the cluster as it is on a ");
      message.append(ManagedInstance::describe(
          static_cast<ManagedInstance::State>(state.source_state)));
      message.append(" state, and should be ONLINE");

      throw shcore::Exception::runtime_error(message);
    }
  } else {
    std::string message = "The instance '" + instance_address + "'";
    message.append(
        " cannot be used to restore the cluster as it is not an active member "
        "of replication group.");

    throw shcore::Exception::runtime_error(message);
  }

  // Check if there is quorum to issue an error.
  if (mysqlshdk::gr::has_quorum(*target_instance, nullptr, nullptr)) {
    mysqlsh::current_console()->print_error(
        "Cannot perform operation on an healthy cluster because it can only "
        "be used to restore a cluster from quorum loss.");

    throw shcore::Exception::runtime_error(
        "The cluster has quorum according to instance '" + instance_address +
        "'");
  }

  // Get the all instances from MD and members visible by the target instance.
  std::vector<std::pair<Instance_metadata, mysqlshdk::gr::Member>>
      instances_info = get_instances_with_state();

  std::string group_peers;
  int online_count = 0;

  for (auto &instance : instances_info) {
    std::string instance_host = instance.first.endpoint;
    auto target_coptions = shcore::get_connection_options(instance_host, false);
    // We assume the login credentials are the same on all instances
    target_coptions.set_login_options_from(
        target_instance->get_connection_options());

    std::shared_ptr<Instance> instance_session;
    try {
      log_info(
          "Opening a new session to a group_peer instance to obtain the XCOM "
          "address %s",
          instance_host.c_str());
      instance_session = Instance::connect(
          target_coptions, current_shell_options()->get().wizards);

      if (instance.second.state == mysqlshdk::gr::Member_state::ONLINE ||
          instance.second.state == mysqlshdk::gr::Member_state::RECOVERING) {
        // Add GCS address of active instance to the force quorum membership.
        std::string group_peer_instance_xcom_address =
            instance_session
                ->get_sysvar_string("group_replication_local_address")
                .get_safe();

        group_peers.append(group_peer_instance_xcom_address);
        group_peers.append(",");

        online_count++;
      } else {
        // Stop GR on not active instances.
        mysqlshdk::gr::stop_group_replication(*instance_session);
      }
    } catch (const std::exception &e) {
      log_error("Could not open connection to %s: %s", instance_address.c_str(),
                e.what());

      // Only throw errors if the instance is active, otherwise ignore it.
      if (instance.second.state == mysqlshdk::gr::Member_state::ONLINE ||
          instance.second.state == mysqlshdk::gr::Member_state::RECOVERING) {
        throw;
      }
    }
  }

  if (online_count == 0) {
    throw shcore::Exception::logic_error(
        "No online instances are visible from the given one.");
  }

  // Force the reconfiguration of the GR group
  {
    // Remove the trailing comma of group_peers
    if (group_peers.back() == ',') group_peers.pop_back();

    log_info("Setting group_replication_force_members at instance %s",
             instance_address.c_str());

    // Setting the group_replication_force_members will force a new group
    // membership, triggering the necessary actions from GR upon being set to
    // force the quorum. Therefore, the variable can be cleared immediately
    // after it is set.
    target_instance->set_sysvar("group_replication_force_members", group_peers,
                                mysqlshdk::mysql::Var_qualifier::GLOBAL);

    // Clear group_replication_force_members at the end to allow GR to be
    // restarted later on the instance (without error).
    target_instance->set_sysvar("group_replication_force_members",
                                std::string(),
                                mysqlshdk::mysql::Var_qualifier::GLOBAL);
  }

  if (interactive) {
    console->println(
        "The InnoDB cluster was successfully restored using the partition from "
        "the instance '" +
        instance_def.as_uri(mysqlshdk::db::uri::formats::user_transport()) +
        "'.");
    console->println();
    console->println(
        "WARNING: To avoid a split-brain scenario, ensure that all other "
        "members of the cluster are removed or joined back to the group that "
        "was restored.");
    console->println();
  }
}

void Cluster_impl::rescan(const cluster::Rescan_options &options) {
  check_preconditions("rescan");

  acquire_primary();
  auto finally_primary =
      shcore::on_leave_scope([this]() { release_primary(); });

  // Create the rescan command and execute it.
  cluster::Rescan op_rescan(options, this);

  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope([&op_rescan]() { op_rescan.finish(); });

  // Prepare the rescan command execution (validations).
  op_rescan.prepare();

  // Execute rescan operation.
  op_rescan.execute();
}

bool Cluster_impl::get_disable_clone_option() const {
  shcore::Value value;
  if (m_metadata_storage->query_cluster_attribute(
          get_id(), k_cluster_attribute_disable_clone, &value)) {
    return value.as_bool();
  }
  return false;
}

void Cluster_impl::set_disable_clone_option(const bool disable_clone) {
  m_metadata_storage->update_cluster_attribute(
      get_id(), k_cluster_attribute_disable_clone,
      disable_clone ? shcore::Value::True() : shcore::Value::False());
}

bool Cluster_impl::get_manual_start_on_boot_option() const {
  shcore::Value flag;
  if (m_metadata_storage->query_cluster_attribute(
          get_id(), k_cluster_attribute_manual_start_on_boot, &flag))
    return flag.as_bool();
  // default false
  return false;
}

shcore::Value Cluster_impl::check_instance_state(
    const Connection_options &instance_def) {
  check_preconditions("checkInstanceState");

  Scoped_instance target(
      connect_target_instance(instance_def, true, false, true));

  // Create the Cluster Check_instance_state object and execute it.
  cluster::Check_instance_state op_check_instance_state(*this, target);

  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope(
      [&op_check_instance_state]() { op_check_instance_state.finish(); });

  // Prepare the Cluster Check_instance_state  command execution
  // (validations).
  op_check_instance_state.prepare();

  // Execute Cluster Check_instance_state  operations.
  return op_check_instance_state.execute();
}

void Cluster_impl::switch_to_single_primary_mode(
    const Connection_options &instance_def) {
  check_preconditions("switchToSinglePrimaryMode");

  // Switch to single-primary mode

  // Create the Switch_to_single_primary_mode object and execute it.
  cluster::Switch_to_single_primary_mode op_switch_to_single_primary_mode(
      instance_def, this);

  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope([&op_switch_to_single_primary_mode]() {
    op_switch_to_single_primary_mode.finish();
  });

  // Prepare the Switch_to_single_primary_mode command execution (validations).
  op_switch_to_single_primary_mode.prepare();

  // Execute Switch_to_single_primary_mode operation.
  op_switch_to_single_primary_mode.execute();
}

void Cluster_impl::switch_to_multi_primary_mode() {
  check_preconditions("switchToMultiPrimaryMode");

  // Create the Switch_to_multi_primary_mode object and execute it.
  cluster::Switch_to_multi_primary_mode op_switch_to_multi_primary_mode(this);

  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope([&op_switch_to_multi_primary_mode]() {
    op_switch_to_multi_primary_mode.finish();
  });

  // Prepare the Switch_to_multi_primary_mode command execution (validations).
  op_switch_to_multi_primary_mode.prepare();

  // Execute Switch_to_multi_primary_mode operation.
  op_switch_to_multi_primary_mode.execute();
}

void Cluster_impl::set_primary_instance(
    const Connection_options &instance_def) {
  check_preconditions("setPrimaryInstance");

  // Set primary instance

  // Create the Set_primary_instance object and execute it.
  cluster::Set_primary_instance op_set_primary_instance(instance_def, this);

  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope(
      [&op_set_primary_instance]() { op_set_primary_instance.finish(); });

  // Prepare the Set_primary_instance command execution (validations).
  op_set_primary_instance.prepare();

  // Execute Set_primary_instance operation.
  op_set_primary_instance.execute();
}

mysqlshdk::utils::Version Cluster_impl::get_lowest_instance_version() const {
  // Get the server version of the current cluster instance and initialize
  // the lowest cluster session.
  mysqlshdk::utils::Version lowest_version = get_target_server()->get_version();

  // Get address of the cluster instance to skip it in the next step.
  std::string cluster_instance_address =
      get_target_server()->get_canonical_address();

  // Get the lowest server version from the available cluster instances.
  execute_in_members(
      {mysqlshdk::gr::Member_state::ONLINE,
       mysqlshdk::gr::Member_state::RECOVERING},
      get_target_server()->get_connection_options(), {cluster_instance_address},
      [&lowest_version](const std::shared_ptr<Instance> &instance) {
        mysqlshdk::utils::Version version = instance->get_version();
        if (version < lowest_version) {
          lowest_version = version;
        }
        return true;
      });

  return lowest_version;
}

mysqlshdk::mysql::Auth_options Cluster_impl::create_replication_user(
    mysqlshdk::mysql::IInstance *target) {
  assert(target);

  std::string recovery_user = get_replication_user_name(
      target, mysqlshdk::gr::k_group_recovery_user_prefix);

  log_info("Creating recovery account '%s'@'%%' for instance '%s'",
           recovery_user.c_str(), target->descr().c_str());

  auto primary = get_primary_master();

  // Check if the replication user already exists and delete it if it does,
  // before creating it again.
  bool rpl_user_exists = primary->user_exists(recovery_user, "%");
  if (rpl_user_exists) {
    auto console = current_console();
    console->print_note(shcore::str_format(
        "User '%s'@'%%' already existed at instance '%s'. It will be "
        "deleted and created again with a new password.",
        recovery_user.c_str(), primary->descr().c_str()));
    primary->drop_user(recovery_user, "%");
  }

  // Check if clone is available on ALL cluster members, to avoid a failing SQL
  // query because BACKUP_ADMIN is not supported
  bool clone_available_all_members = false;

  mysqlshdk::utils::Version lowest_version = get_lowest_instance_version();
  if (lowest_version >= mysqlshdk::mysql::k_mysql_clone_plugin_initial_version)
    clone_available_all_members = true;

  // Check if clone is supported on the target instance
  bool clone_available_on_target =
      target->get_version() >=
      mysqlshdk::mysql::k_mysql_clone_plugin_initial_version;

  bool clone_available =
      clone_available_all_members && clone_available_on_target;

  return mysqlshdk::gr::create_recovery_user(recovery_user, *primary, {"%"}, {},
                                             clone_available);
}

void Cluster_impl::drop_replication_user(mysqlshdk::mysql::IInstance *target) {
  assert(target);
  auto console = current_console();

  auto primary = get_primary_master();

  std::string recovery_user;
  std::vector<std::string> recovery_user_hosts;
  bool from_metadata = false;
  try {
    std::tie(recovery_user, recovery_user_hosts, from_metadata) =
        get_replication_user(*target);
  } catch (const std::exception &e) {
    console->print_note(
        "The recovery user name for instance '" + target->descr() +
        "' does not match the expected format for users "
        "created automatically by InnoDB Cluster. Skipping its removal.");
  }
  if (!from_metadata) {
    log_info("Removing replication user '%s'", recovery_user.c_str());
    try {
      mysqlshdk::mysql::drop_all_accounts_for_user(*primary, recovery_user);
    } catch (const std::exception &e) {
      console->print_warning("Error dropping recovery accounts for user " +
                             recovery_user + ": " + e.what());
    }
  } else {
    /*
      Since clone copies all the data, including mysql.slave_master_info (where
      the CHANGE MASTER credentials are stored) an instance may be using the
      info stored in the donor's mysql.slave_master_info.

      To avoid such situation, we re-issue the CHANGE MASTER query after
      clone to ensure the right account is used. However, if the monitoring
      process is interrupted or waitRecovery = 0, that won't be done.

      The approach to tackle this issue is to store the donor recovery account
      in the target instance MD.instances table before doing the new CHANGE
      and only update it to the right account after a successful CHANGE MASTER.
      With this approach we can ensure that the account is not removed if it is
      being used.

      As so were we must query the Metadata to check whether the
      recovery user of that instance is registered for more than one instance
      and if that's the case then it won't be dropped.
    */
    if (get_metadata_storage()->is_recovery_account_unique(recovery_user)) {
      log_info("Dropping recovery account '%s'@'%%' for instance '%s'",
               recovery_user.c_str(), target->descr().c_str());

      try {
        primary->drop_user(recovery_user, "%", true);
      } catch (const std::exception &e) {
        console->print_warning(shcore::str_format(
            "Error dropping recovery account '%s'@'%%' for instance '%s'",
            recovery_user.c_str(), target->descr().c_str()));
      }

      // Also update metadata
      try {
        get_metadata_storage()->update_instance_recovery_account(
            target->get_uuid(), "", "");
      } catch (const std::exception &e) {
        log_warning("Could not update recovery account metadata for '%s': %s",
                    target->descr().c_str(), e.what());
      }
    }
  }
}

bool Cluster_impl::contains_instance_with_address(
    const std::string &host_port) const {
  return get_metadata_storage()->is_instance_on_cluster(get_id(), host_port);
}

/*
 * Ensures the cluster object (and metadata) can perform update operations and
 * returns a session to the PRIMARY.
 *
 * For a cluster to be updatable, it's necessary that:
 * - the MD object is connected to the PRIMARY of the
 * primary cluster, so that the MD can be updated (and is also not lagged)
 * - the primary of the cluster is reachable, so that cluster accounts
 * can be created there.
 *
 * An exception is thrown if not possible to connect to the primary.
 *
 * An Instance object connected to the primary is returned. The session
 * is owned by the cluster object.
 */
mysqlsh::dba::Instance *Cluster_impl::acquire_primary(
    mysqlshdk::mysql::Lock_mode /*mode*/,
    const std::string & /*skip_lock_uuid*/) {
  auto console = current_console();

  if (!m_primary_master) {
    m_primary_master = m_target_server;
  }
  std::string uuid = m_primary_master->get_uuid();
  std::string primary_uuid;
  std::string primary_url;

  try {
    primary_url = find_primary_member_uri(m_primary_master, false, nullptr);

    if (primary_url.empty()) {
      // Shouldn't happen, because validation for PRIMARY is done first
      throw std::logic_error("No PRIMARY member found");
    } else if (primary_url !=
               m_primary_master->get_connection_options().uri_endpoint()) {
      mysqlshdk::db::Connection_options copts(primary_url);
      log_info("Connecting to %s...", primary_url.c_str());
      copts.set_login_options_from(m_primary_master->get_connection_options());

      m_primary_master = Instance::connect(copts);

      m_metadata_storage = std::make_shared<MetadataStorage>(m_primary_master);
    }

    return m_primary_master.get();
  } catch (...) {
    console->print_error("A connection to the PRIMARY instance at " +
                         primary_url +
                         " could not be established to perform this action.");
    throw;
  }
}

void Cluster_impl::release_primary(mysqlsh::dba::Instance *primary) {
  (void)primary;
  m_primary_master.reset();
}

std::tuple<std::string, std::vector<std::string>, bool>
Cluster_impl::get_replication_user(
    const mysqlshdk::mysql::IInstance &target_instance) const {
  // First check the metadata for the recovery user
  std::string recovery_user, recovery_user_host;
  bool from_metadata = false;
  std::vector<std::string> recovery_user_hosts;
  std::tie(recovery_user, recovery_user_host) =
      get_metadata_storage()->get_instance_recovery_account(
          target_instance.get_uuid());
  if (recovery_user.empty()) {
    // Assuming the account was created by an older version of the shell,
    // which did not store account name in metadata
    log_info(
        "No recovery account details in metadata for instance '%s', assuming "
        "old account style",
        target_instance.descr().c_str());

    auto unrecorded_recovery_user =
        mysqlshdk::gr::get_recovery_user(target_instance);
    assert(!unrecorded_recovery_user.empty());

    if (shcore::str_beginswith(
            unrecorded_recovery_user,
            mysqlshdk::gr::k_group_recovery_old_user_prefix)) {
      log_info("Found old account style recovery user '%s'",
               unrecorded_recovery_user.c_str());
      // old accounts were created for several hostnames
      recovery_user_hosts = mysqlshdk::mysql::get_all_hostnames_for_user(
          *(get_target_server()), unrecorded_recovery_user);
      recovery_user = unrecorded_recovery_user;
    } else if (shcore::str_beginswith(
                   unrecorded_recovery_user,
                   mysqlshdk::gr::k_group_recovery_user_prefix)) {
      // either the transaction to store the user failed or the metadata was
      // manually changed to remove it.
      throw shcore::Exception::metadata_error(
          "The replication recovery account in use by '" +
          target_instance.descr() +
          "' is not stored in the metadata. Use cluster.rescan() to update the "
          "metadata.");
    } else {
      // account not created by InnoDB cluster
      throw shcore::Exception::runtime_error(
          shcore::str_format("Recovery user '%s' not created by InnoDB Cluster",
                             unrecorded_recovery_user.c_str()));
    }
  } else {
    from_metadata = true;
    // new recovery user format, stored in Metadata.
    recovery_user_hosts.push_back(recovery_user_host);
  }
  return std::make_tuple(recovery_user, recovery_user_hosts, from_metadata);
}

std::shared_ptr<Instance> Cluster_impl::get_session_to_cluster_instance(
    const std::string &instance_address) const {
  // Set login credentials to connect to instance.
  // use the host and port from the instance address
  Connection_options instance_cnx_opts =
      shcore::get_connection_options(instance_address, false);
  // Use the password from the cluster session.
  Connection_options cluster_cnx_opt =
      get_target_server()->get_connection_options();
  instance_cnx_opts.set_login_options_from(cluster_cnx_opt);

  log_debug("Connecting to instance '%s'", instance_address.c_str());
  try {
    // Try to connect to instance
    auto instance = Instance::connect(instance_cnx_opts);
    log_debug("Successfully connected to instance");
    return instance;
  } catch (const std::exception &err) {
    log_debug("Failed to connect to instance: %s", err.what());
    throw;
  }
}

size_t Cluster_impl::setup_clone_plugin(bool enable_clone) {
  // Get all cluster instances
  std::vector<Instance_metadata> instance_defs = get_instances();

  // Counter for instances that failed to be updated
  size_t count = 0;

  auto ipool = current_ipool();

  for (const Instance_metadata &instance_def : instance_defs) {
    std::string instance_address = instance_def.endpoint;

    // Establish a session to the cluster instance
    try {
      Scoped_instance instance(
          ipool->connect_unchecked_endpoint(instance_address));

      // Handle the plugin setup only if the target instance supports it
      if (instance->get_version() >=
          mysqlshdk::mysql::k_mysql_clone_plugin_initial_version) {
        if (!enable_clone) {
          // Uninstall the clone plugin
          log_info("Uninstalling the clone plugin on instance '%s'.",
                   instance_address.c_str());
          mysqlshdk::mysql::uninstall_clone_plugin(*instance, nullptr);
        } else {
          // Install the clone plugin
          log_info("Installing the clone plugin on instance '%s'.",
                   instance_address.c_str());
          mysqlshdk::mysql::install_clone_plugin(*instance, nullptr);

          // Get the recovery account in use by the instance so the grant
          // BACKUP_ADMIN is granted to its recovery account
          {
            std::string recovery_user;
            std::vector<std::string> recovery_user_hosts;
            try {
              std::tie(recovery_user, recovery_user_hosts, std::ignore) =
                  get_replication_user(*instance);
            } catch (const shcore::Exception &re) {
              if (re.is_runtime()) {
                mysqlsh::current_console()->print_error(
                    "Unsupported recovery account has been found for "
                    "instance " +
                    instance->descr() +
                    ". Operations such as "
                    "<Cluster>.<<<resetRecoveryAccountsPassword>>>() and "
                    "<Cluster>.<<<addInstance>>>() may fail. Please remove and "
                    "add the instance back to the Cluster to ensure a "
                    "supported recovery account is used.");
              }
              throw;
            }

            auto primary = get_primary_master();

            // Add the BACKUP_ADMIN grant to the instance's recovery account
            // since it may not be there if the instance was added to a cluster
            // non-supporting clone
            for (const auto &host : recovery_user_hosts) {
              shcore::sqlstring grant("GRANT BACKUP_ADMIN ON *.* TO ?@?", 0);
              grant << recovery_user;
              grant << host;
              grant.done();
              primary->execute(grant);
            }
          }
        }
      }
    } catch (const shcore::Error &err) {
      auto console = mysqlsh::current_console();

      std::string op = enable_clone ? "enable" : "disable";

      std::string err_msg = "Unable to " + op + " clone on the instance '" +
                            instance_address + "': " + err.format();

      // If a cluster member is unreachable, just print a warning. Otherwise
      // print error
      if (err.code() == CR_CONNECTION_ERROR ||
          err.code() == CR_CONN_HOST_ERROR) {
        console->print_warning(err_msg);
      } else {
        console->print_error(err_msg);
      }
      console->print_info();

      // It failed to update this instance, so increment the counter
      count++;
    }
  }

  return count;
}

void Cluster_impl::_set_instance_option(const std::string &instance_def,
                                        const std::string &option,
                                        const shcore::Value &value) {
  auto instance_conn_opt = Connection_options(instance_def);

  // Set Cluster configuration option

  // Create the Cluster Set_instance_option object and execute it.
  std::unique_ptr<cluster::Set_instance_option> op_set_instance_option;

  // Validation types due to a limitation on the expose() framework.
  // Currently, it's not possible to do overloading of functions that overload
  // an argument of type string/int since the type int is convertible to
  // string, thus overloading becomes ambiguous. As soon as that limitation is
  // gone, this type checking shall go away too.
  if (value.type == shcore::String) {
    std::string value_str = value.as_string();
    op_set_instance_option = std::make_unique<cluster::Set_instance_option>(
        *this, instance_conn_opt, option, value_str);
  } else if (value.type == shcore::Integer || value.type == shcore::UInteger) {
    int64_t value_int = value.as_int();
    op_set_instance_option = std::make_unique<cluster::Set_instance_option>(
        *this, instance_conn_opt, option, value_int);
  } else {
    throw shcore::Exception::type_error(
        "Argument #3 is expected to be a string or an integer");
  }

  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope(
      [&op_set_instance_option]() { op_set_instance_option->finish(); });

  // Prepare the Cluster Set_instance_option command execution (validations).
  op_set_instance_option->prepare();

  // Execute Cluster Set_instance_option operations.
  op_set_instance_option->execute();
}

void Cluster_impl::_set_option(const std::string &option,
                               const shcore::Value &value) {
  // Set Cluster configuration option
  // Create the Set_option object and execute it.
  std::unique_ptr<cluster::Set_option> op_cluster_set_option;

  // Validation types due to a limitation on the expose() framework.
  // Currently, it's not possible to do overloading of functions that overload
  // an argument of type string/int since the type int is convertible to
  // string, thus overloading becomes ambiguous. As soon as that limitation is
  // gone, this type checking shall go away too.
  if (value.type == shcore::String) {
    std::string value_str = value.as_string();
    op_cluster_set_option =
        std::make_unique<cluster::Set_option>(this, option, value_str);
  } else if (value.type == shcore::Integer || value.type == shcore::UInteger) {
    int64_t value_int = value.as_int();
    op_cluster_set_option =
        std::make_unique<cluster::Set_option>(this, option, value_int);
  } else if (value.type == shcore::Bool) {
    bool value_bool = value.as_bool();
    op_cluster_set_option =
        std::make_unique<cluster::Set_option>(this, option, value_bool);
  } else {
    throw shcore::Exception::type_error(
        "Argument #2 is expected to be a string, an integer or a boolean.");
  }

  // Always execute finish when leaving "try catch".
  auto finally = shcore::on_leave_scope(
      [&op_cluster_set_option]() { op_cluster_set_option->finish(); });

  // Prepare the Set_option command execution (validations).
  op_cluster_set_option->prepare();

  // Execute Set_instance_option operations.
  op_cluster_set_option->execute();
}

}  // namespace dba
}  // namespace mysqlsh
