/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
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
#include "mysqlshdk/libs/oci/oci_options.h"
#include "mysqlshdk/include/shellcore/shell_options.h"
#include "mysqlshdk/libs/config/config_file.h"
#include "mysqlshdk/libs/oci/oci_rest_service.h"
#include "mysqlshdk/libs/oci/oci_setup.h"
#include "mysqlshdk/libs/storage/utils.h"

namespace mysqlshdk {
namespace oci {
using Status_code = Response::Status_code;

namespace {
void validate_config_profile(const std::string &config_path,
                             const std::string &profile) {
  mysqlshdk::oci::Oci_setup oci_setup(config_path);
  if (!oci_setup.has_profile(profile)) {
    throw std::runtime_error("The indicated OCI Profile does not exist.");
  }
}
}  // namespace

void Oci_options::do_unpack(shcore::Option_unpacker *unpacker) {
  switch (target) {
    case OBJECT_STORAGE:
      unpacker->optional(kOsNamespace, &os_namespace)
          .optional(kOsBucketName, &os_bucket_name)
          .optional(kOciConfigFile, &config_path)
          .optional(kOciProfile, &config_profile);
      break;
  }
}

void Oci_options::load_defaults() {
  if (config_path.is_null()) {
    config_path = mysqlsh::current_shell_options()->get().oci_config_file;
  }

  if (config_profile.is_null()) {
    config_profile = mysqlsh::current_shell_options()->get().oci_profile;
  }

  if (os_namespace.is_null()) {
    check_option_values();

    Oci_rest_service identity(Oci_service::IDENTITY, *this);
    std::string raw_data;
    identity.get("/20160918/tenancies/" + identity.get_tenancy_id(), {},
                 &raw_data);

    auto data = shcore::Value::parse(raw_data).as_map();
    os_namespace = data->get_string("name");

    mysqlshdk::config::Config_file config(mysqlshdk::config::Case::SENSITIVE,
                                          mysqlshdk::config::Escape::NO);
  }
}

void Oci_options::check_option_values() {
  if (!config_profile.is_null()) {
    validate_config_profile(*config_path, *config_profile);
  }

  switch (target) {
    case OBJECT_STORAGE:
      if (os_bucket_name.is_null()) {
        throw std::runtime_error("The osBucketName option is missing.");
      }
      break;
  }
}

bool parse_oci_options(
    Oci_uri_type type, const std::string &in_path,
    const std::unordered_map<std::string, std::string> &in_options,
    Oci_options *out_options, std::string *out_path) {
  assert(out_options);
  assert(out_path);

  bool ret_val = false;
  const auto scheme = mysqlshdk::storage::utils::get_scheme(in_path);

  // The osBucketName only should be used when passing a raw path
  bool os_bucket_found = in_options.find(kOsBucketName) != in_options.end();

  if (!scheme.empty() && os_bucket_found) {
    throw std::runtime_error(
        "The option osBucketName can not be used when the path contains a "
        "scheme.'");
  }

  if (mysqlshdk::storage::utils::scheme_matches(scheme, "oci+os") ||
      os_bucket_found) {
    std::string file;

    size_t min_parts = type == Oci_uri_type::FILE ? 6 : 5;

    // Parses the URI to get the required options
    if (!scheme.empty()) {
      const auto parts = shcore::str_split(in_path, "/", min_parts - 1);
      if (parts.size() < min_parts) {
        if (type == Oci_uri_type::FILE) {
          throw std::runtime_error(
              "Invalid URI. Use oci+os://region/namespace/bucket/file");
        } else {
          throw std::runtime_error(
              "Invalid URI. Use oci+os://region/namespace/bucket[/directory]");
        }
      }

      // The values from the URL are only taken if they were not set already
      // i.e. out_optoins may already have them because they were passed as
      // individual parameters, in such case we ignore the URI ones
      if (out_options->os_namespace.is_null())
        out_options->os_namespace = parts[3];

      if (out_options->os_bucket_name.is_null())
        out_options->os_bucket_name = parts[4];

      // out_path will contain the file or directory name, in the case of
      // directories it might be empty indicating it is the root directory of
      // the bucket
      if (parts.size() == 6)
        *out_path = parts[5];
      else
        *out_path = "";
    } else {
      *out_path = in_path;
      out_options->os_bucket_name =
          in_options.at(mysqlshdk::oci::kOsBucketName);
    }

    if (in_options.find(mysqlshdk::oci::kOsNamespace) != in_options.end())
      out_options->os_namespace = in_options.at(mysqlshdk::oci::kOsNamespace);

    if (in_options.find(mysqlshdk::oci::kOciConfigFile) != in_options.end())
      out_options->config_path = in_options.at(mysqlshdk::oci::kOciConfigFile);

    if (in_options.find(mysqlshdk::oci::kOciProfile) != in_options.end())
      out_options->config_profile = in_options.at(mysqlshdk::oci::kOciProfile);

    out_options->check_option_values();
    out_options->load_defaults();

    ret_val = true;
  }

  return ret_val;
}

}  // namespace oci
}  // namespace mysqlshdk