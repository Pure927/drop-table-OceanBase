/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/stmt/drop_table_stmt.h"
#include "storage/db/db.h"

RC DropTableStmt::create(Db *db, const DropTableSqlNode &node, Stmt *&stmt)
{
  stmt = nullptr;
  if (db == nullptr || node.relation_name.empty()) {
    return RC::INVALID_ARGUMENT;
  }
  if (db->find_table(node.relation_name.c_str()) == nullptr) {
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }
  stmt = new DropTableStmt(node.relation_name);
  return RC::SUCCESS;
}
