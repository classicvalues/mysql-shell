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

#ifndef MODULES_UTIL_DUMP_DUMP_SCHEMAS_H_
#define MODULES_UTIL_DUMP_DUMP_SCHEMAS_H_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "modules/util/dump/dump_schemas_options.h"
#include "modules/util/dump/dumper.h"

namespace mysqlsh {
namespace dump {

class Dump_schemas : public Dumper {
 public:
  Dump_schemas() = delete;
  explicit Dump_schemas(const Dump_schemas_options &options);

  Dump_schemas(const Dump_schemas &) = delete;
  Dump_schemas(Dump_schemas &&) = delete;

  Dump_schemas &operator=(const Dump_schemas &) = delete;
  Dump_schemas &operator=(Dump_schemas &&) = delete;

  virtual ~Dump_schemas() = default;

 protected:
  void create_schema_tasks() override;

  virtual bool dump_all_schemas() const { return false; }

  virtual const std::unordered_set<std::string> &excluded_schemas() const;

  std::unique_ptr<Schema_dumper> schema_dumper(
      const std::shared_ptr<mysqlshdk::db::ISession> &session) const override;

 private:
  const char *name() const override { return "dumpSchemas"; }

  void summary() const override {}

  void on_create_table_task(const Table_task &) override {}

  std::vector<Table_info> get_tables(const std::string &schema);

  std::vector<Table_info> get_views(const std::string &schema);

  std::vector<Table_info> get_tables(const std::string &schema,
                                     const std::string &type);

  const Dump_schemas_options &m_options;
};

}  // namespace dump
}  // namespace mysqlsh

#endif  // MODULES_UTIL_DUMP_DUMP_SCHEMAS_H_
