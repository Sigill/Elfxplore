#include "Database2.hxx"

#include <iostream>
#include <cxxabi.h>

#define LAZYSTM(stm) [this]{ return new SQLite::Statement(db, stm); }

Database2::Database2(const std::string& file)
  : db(file, SQLite::OPEN_CREATE | SQLite::OPEN_READWRITE)
  , create_artifact_stm(LAZYSTM("insert into artifacts (name, type) values (?, ?)"))
  , artifact_id_by_name_stm(LAZYSTM("select id from artifacts where name = ?"))
  , create_symbol_stm(LAZYSTM("insert into symbols (name, dname) values (?, ?)"))
  , symbol_id_by_name_stm(LAZYSTM("select id from symbols where name = ?"))
  , create_symbol_reference_stm(LAZYSTM("insert into symbol_references (artifact_id, symbol_id, category, type, size) values (?, ?, ?, ?, ?)"))
  , create_dependency_stm(LAZYSTM("insert into dependencies (dependee_id, dependency_id) values (?, ?)"))
{
  db.exec("PRAGMA encoding='UTF-8';");
  db.exec("PRAGMA journal_mode=WAL;");
  db.exec("PRAGMA page_size=65536;");
  db.exec("PRAGMA locking_mode=EXCLUSIVE;");
  db.exec("PRAGMA synchronous=OFF;");
}

void Database2::create() {
  const char* queries = R"(
create table if not exists "artifacts" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "name" VARCHAR(256) NOT NULL,
  "type" VARCHAR(16) NOT NULL
);
create unique index "unique_artifacts" on "artifacts" ("name");

create table if not exists "dependencies" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "dependee_id" INTEGER NOT NULL,
  "dependency_id" INTEGER NOT NULL
);
create unique index "unique_dependency" on "dependencies" ("dependee_id", "dependency_id");

create table if not exists "symbols" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "name" TEXT NOT NULL,
  "dname" TEXT NOT NULL
);
create unique index "unique_symbol" on "symbols" ("name");
create index "symbol_by_dname" on "symbols" ("dname");

create table if not exists "symbol_references" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "artifact_id" INTEGER NOT NULL,
  "symbol_id" INTEGER NOT NULL,
  "category" VARCHAR(16) NOT NULL,
  "type" VARCHAR(1) NOT NULL,
  "size" INTEGER DEFAULT NULL
);
create unique index "unique_symbol_reference" on "symbol_references" ("artifact_id", "symbol_id", "category", "type");
create index "symbol_reference_by_artifact" on "symbol_references" ("artifact_id");
create index "symbol_reference_by_symbol" on "symbol_references" ("symbol_id");
create index "symbol_reference_by_category" on "symbol_references" ("category");
create index "symbol_reference_by_type" on "symbol_references" ("type");
)";

  db.exec(queries);
}

void Database2::truncate_symbols() {
  db.exec("delete from symbols;");
  truncate_symbol_references();
}

void Database2::truncate_symbol_references() {
  db.exec("delete from symbol_references;");
}

long long Database2::last_id()
{
  return db.getLastInsertRowid();
}

void Database2::create_artifact(const std::string& name, const std::string& type) {
  auto& stm = *create_artifact_stm;

  stm.bind(1, name);
  stm.bind(2, type);
  stm.exec();
  stm.reset();
  stm.clearBindings();
}

int Database2::artifact_id_by_name(const std::string& name) {
  auto& stm = *artifact_id_by_name_stm;

  stm.bind(1, name);
  return get_id(stm);
}

void Database2::create_symbol(const std::string& name) {
  int status;
  char* dname = abi::__cxa_demangle(name.c_str(), 0, 0, &status);

  auto& stm = *create_symbol_stm;

  stm.bind(1, name);
  if (status == 0)
    stm.bind(2, dname);
  else
    stm.bind(2, "");
  stm.exec();
  stm.reset();
  stm.clearBindings();

  free(dname);
}

int Database2::symbol_id_by_name(const std::string& name) {
  auto& stm = *symbol_id_by_name_stm;

  stm.bind(1, name);
  return get_id(stm);
}

void Database2::create_symbol_reference(long long artifact_id, long long symbol_id, const char* category, const std::string& type, long long size) {
  auto& stm = *create_symbol_reference_stm;

  stm.bind(1, artifact_id);
  stm.bind(2, symbol_id);
  stm.bind(3, category);
  stm.bind(4, type);
  stm.bind(5, size);

  stm.exec();
  stm.reset();
  stm.clearBindings();
}

void Database2::insert_symbol_references(long long artifact_id, const SymbolSet& symbols, const char* category) {
  for(const Symbol& symbol : symbols) {
    long long symbol_id = symbol_id_by_name(symbol.name);
    if (symbol_id == -1) {
      create_symbol(symbol.name);
      symbol_id = db.getLastInsertRowid();
    }

    create_symbol_reference(artifact_id, symbol_id, category, symbol.type, symbol.size);
  }
}

void Database2::insert_symbol_references(long long artifact_id, const ArtifactSymbols& symbols) {
  insert_symbol_references(artifact_id, symbols.undefined, "undefined");
  insert_symbol_references(artifact_id, symbols.external, "external");
  insert_symbol_references(artifact_id, symbols.internal, "internal");
}

void Database2::create_dependency(long long dependee_id, long long dependency_id)
{
  auto& stm = *create_dependency_stm;

  stm.bind(1, dependee_id);
  stm.bind(2, dependency_id);

  stm.exec();
  stm.reset();
  stm.clearBindings();
}

long long Database2::get_id(SQLite::Statement& stm) {
  long long id = -1;

  if(stm.executeStep()) {
    id = stm.getColumn(0).getInt64();
  }

  stm.reset();
  stm.clearBindings();

  return id;
}
