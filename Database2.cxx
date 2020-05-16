#include "Database2.hxx"

#include <iostream>
#include <sstream>
#include <cxxabi.h>
#include <algorithm>
#include <cctype>

#include "ArtifactSymbols.hxx"
#include "SymbolReference.hxx"
#include "query-utils.hxx"

namespace {

bool valid_symbol_char(const char c)
{
  return isalnum(c) != 0 || c == '_' || c == '$' || c == '.';
}

} // anonymous namespace

#define LAZYSTM(stm) [this]{ return new SQLite::Statement(db, stm); }

Database2::Database2(const std::string& file)
  : db(file, SQLite::OPEN_CREATE | SQLite::OPEN_READWRITE)
  , create_command_stm(LAZYSTM("insert into commands (directory, executable, args) values (?, ?, ?)"))
  , create_artifact_stm(LAZYSTM("insert into artifacts (name, type, generating_command_id) values (?, ?, ?)"))
  , artifact_id_by_name_stm(LAZYSTM("select id from artifacts where name = ?"))
  , artifact_name_by_id_stm(LAZYSTM("select name from artifacts where id = ?"))
  , artifact_id_by_command_stm(LAZYSTM("select id from artifacts where generating_command_id = ?"))
  , artifact_set_generating_command_stm(LAZYSTM("update artifacts set generating_command_id = ? where id = ?"))
  , artifact_set_type_stm(LAZYSTM("update artifacts set type = ? where id = ?"))
  , create_symbol_stm(LAZYSTM("insert into symbols (name, dname) values (?, ?)"))
  , symbol_id_by_name_stm(LAZYSTM("select id from symbols where name = ?"))
  , create_symbol_reference_stm(LAZYSTM("insert into symbol_references (artifact_id, symbol_id, category, type, size) values (?, ?, ?, ?, ?)"))
  , create_dependency_stm(LAZYSTM("insert into dependencies (dependee_id, dependency_id) values (?, ?)"))
  , find_dependencies_stm(LAZYSTM("select dependency_id from dependencies where dependee_id = ?"))
  , find_dependees_stm(LAZYSTM("select dependee_id from dependencies where dependency_id = ?"))
  , get_sources_stm(LAZYSTM(R"(select artifacts.name from artifacts
inner join dependencies on dependencies.dependency_id = artifacts.id
where dependencies.dependee_id = ? and artifacts.type = "source")"))
  , undefined_symbols_stm(LAZYSTM("select symbol_id from symbol_references where category = \"undefined\" and artifact_id = ?"))
{
  db.exec("PRAGMA encoding='UTF-8';");
  db.exec("PRAGMA journal_mode=WAL;");
  db.exec("PRAGMA page_size=65536;");
  db.exec("PRAGMA locking_mode=EXCLUSIVE;");
  db.exec("PRAGMA synchronous=OFF;");
  db.exec("PRAGMA foreign_keys=ON;");

  create();
}

void Database2::create() {
  const char* queries = R"(
create table if not exists "commands" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "directory" VARCHAR(256) NOT NULL,
  "executable" VARCHAR(256) NOT NULL,
  "args" TEXT NOT NULL
);
create unique index if not exists "unique_commands" on "commands" ("directory", "executable", "args");

create table if not exists "artifacts" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "name" VARCHAR(256) NOT NULL,
  "type" VARCHAR(16) NOT NULL,
  "generating_command_id" INTEGER DEFAULT NULL REFERENCES "commands"
);
create unique index if not exists "unique_artifacts" on "artifacts" ("name");
create index if not exists "artifact_by_type" on "artifacts" ("type");
create index if not exists "generated_artifacts" on "artifacts" ("generating_command_id");

create table if not exists "dependencies" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "dependee_id" INTEGER NOT NULL REFERENCES "artifacts",
  "dependency_id" INTEGER NOT NULL REFERENCES "artifacts"
);
create unique index if not exists "unique_dependency" on "dependencies" ("dependee_id", "dependency_id");

create table if not exists "symbols" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "name" TEXT NOT NULL,
  "dname" TEXT NOT NULL
);
create unique index if not exists "unique_symbol" on "symbols" ("name");
create index if not exists "symbol_by_dname" on "symbols" ("dname");

create table if not exists "symbol_references" (
  "id" INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  "artifact_id" INTEGER NOT NULL REFERENCES "artifacts",
  "symbol_id" INTEGER NOT NULL REFERENCES "symbols",
  "category" VARCHAR(16) NOT NULL,
  "type" VARCHAR(1) NOT NULL,
  "size" INTEGER DEFAULT NULL
);
create index if not exists "symbol_reference_by_artifact" on "symbol_references" ("artifact_id");
create index if not exists "symbol_reference_by_symbol" on "symbol_references" ("symbol_id");
create index if not exists "symbol_reference_by_category" on "symbol_references" ("category");
create index if not exists "symbol_reference_by_type" on "symbol_references" ("type");
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

SQLite::Statement Database2::statement(const std::string& query)
{
  return SQLite::Statement(db, query);
}

void Database2::optimize()
{
  db.exec("analyze;");
}

void Database2::vacuum()
{
  db.exec("vacuum;");
}

long long Database2::last_id()
{
  return db.getLastInsertRowid();
}

long long Database2::create_command(const std::string& directory, const std::string& executable, const std::string& args) {
  auto& stm = *create_command_stm;

  stm.bind(1, directory);
  stm.bind(2, executable);
  stm.bind(3, args);
  stm.exec();
  stm.reset();
  stm.clearBindings();

  return db.getLastInsertRowid();
}

long long Database2::count_artifacts()
{
  auto stm = statement("select count(*) from artifacts");
  return get_id(stm);
}

std::map<std::string, long long> Database2::count_artifacts_by_type()
{
  std::map<std::string, long long> stats;

  auto stm = statement("select type, count(*)from artifacts group by type");

  while(stm.executeStep()) {
    stats.emplace(stm.getColumn(0).getString(), stm.getColumn(1).getInt64());
  }

  return stats;
}

void Database2::create_artifact(const std::string& name, const std::string& type, const long long generating_command_id) {
  auto& stm = *create_artifact_stm;

  stm.bind(1, name);
  stm.bind(2, type);
  if (generating_command_id >= 0)
    stm.bind(3, generating_command_id);
  else
    stm.bind(3);
  stm.exec();
  stm.reset();
  stm.clearBindings();
}

long long Database2::artifact_id_by_name(const std::string& name) {
  auto& stm = *artifact_id_by_name_stm;

  stm.bind(1, name);
  return get_id(stm);
}

std::string Database2::artifact_name_by_id(long long id)
{
  auto& stm = *artifact_name_by_id_stm;

  stm.bind(1, id);
  return get_string(stm);
}

long long Database2::artifact_id_by_command(const long long command_id)
{
  auto& stm = *artifact_id_by_command_stm;

  stm.bind(1, command_id);
  return get_id(stm);
}

void Database2::artifact_set_generating_command(const long long artifact_id, const long long command_id)
{
  auto& stm = *artifact_set_generating_command_stm;
  stm.bind(1, command_id);
  stm.bind(2, artifact_id);
  stm.exec();
  stm.reset();
  stm.clearBindings();
}

void Database2::artifact_set_type(const long long artifact_id, const std::string& type)
{
  auto& stm = *artifact_set_type_stm;
  stm.bind(1, type);
  stm.bind(2, artifact_id);
  stm.exec();
  stm.reset();
  stm.clearBindings();
}

long long Database2::count_symbols()
{
  auto stm = statement("select count(*) from symbols");
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

long long Database2::count_symbol_references()
{
  auto stm = statement("select count(*) from symbol_references");
  return get_id(stm);
}

void Database2::create_symbol_reference(long long artifact_id, long long symbol_id, const char* category, const char type, long long size) {
  auto& stm = *create_symbol_reference_stm;

  const char type_str[2] = {type, 0};

  stm.bind(1, artifact_id);
  stm.bind(2, symbol_id);
  stm.bind(3, category);
  stm.bind(4, type_str);
  stm.bind(5, size);

  stm.exec();
  stm.reset();
  stm.clearBindings();
}

void Database2::insert_symbol_references(long long artifact_id, const SymbolReferenceSet& symbols, const char* category) {
  for(const SymbolReference& symbol : symbols) {

    // Strip symbol version
    auto first_invalid_char = std::find_if_not(symbol.name.begin(), symbol.name.end(), valid_symbol_char);
    const std::string symbol_name(symbol.name.cbegin(), first_invalid_char);

    long long symbol_id = symbol_id_by_name(symbol_name);
    if (symbol_id == -1) {
      create_symbol(symbol_name);
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

long long Database2::count_dependencies()
{
  auto stm = statement("select count(*) from dependencies");
  return get_id(stm);
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

SQLite::Statement Database2::build_get_depend_stm(const std::string& select_field,
                                                  const std::string& match_field,
                                                  const std::vector<std::string>& included_types,
                                                  const std::vector<std::string>& excluded_types)
{
  std::stringstream ss;
  ss << "select " << select_field << " from dependencies";

  if (!included_types.empty() || !excluded_types.empty())
    ss << " inner join artifacts on artifacts.id = dependencies." << select_field;

  ss << " where " << match_field << " = ?";

  if (!included_types.empty())
    ss << " and artifacts.type in " << in_expr(included_types);

  if (!excluded_types.empty())
    ss << " and artifacts.type not in " << in_expr(excluded_types);

  return statement(ss.str());
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

std::vector<long long> Database2::get_ids(SQLite::Statement& stm)
{
  std::vector<long long> ids;

  while (stm.executeStep()) {
    ids.push_back(stm.getColumn(0).getInt64());
  }

  stm.reset();
  stm.clearBindings();

  return ids;
}

std::string Database2::get_string(SQLite::Statement& stm)
{
  std::string str;

  if(stm.executeStep()) {
    str = stm.getColumn(0).getString();
  }

  stm.reset();
  stm.clearBindings();

  return str;
}

std::vector<long long> Database2::dependencies(long long dependee_id)
{
  auto& stm = *find_dependencies_stm;

  stm.bind(1, dependee_id);

  return get_ids(stm);
}

std::vector<long long> Database2::dependees(long long dependency_id)
{
  auto& stm = *find_dependees_stm;

  stm.bind(1, dependency_id);

  return get_ids(stm);
}

std::vector<std::string> Database2::get_sources(const long long command_id)
{
  std::vector<std::string> sources;

  const long long artifact_id = artifact_id_by_command(command_id);

  auto& stm = *get_sources_stm;
  stm.bind(1, artifact_id);
  while(stm.executeStep()) {
    sources.emplace_back(stm.getColumn(0).getString());
  }

  stm.reset();
  stm.clearBindings();

  return sources;
}

std::vector<long long> Database2::undefined_symbols(const long long artifact_id)
{
  undefined_symbols_stm->bind(1, artifact_id);
  return get_ids(*undefined_symbols_stm);
}

std::map<long long, std::vector<std::string> > Database2::resolve_symbols(const std::vector<long long>& symbols)
{
  std::map<long long, std::vector<std::string>> symbol_locations;

  if (!symbols.empty())
  {
    std::stringstream ss;
    ss << R"(
  select symbol_references.symbol_id, artifacts.name from symbol_references
  inner join artifacts on artifacts.id = symbol_references.artifact_id
  where symbol_references.category = "external"
  and symbol_references.symbol_id in )" << in_expr(symbols);

    SQLite::Statement stm = statement(ss.str());

    while(stm.executeStep()) {
      symbol_locations[stm.getColumn(0).getInt64()].emplace_back(stm.getColumn(1).getString());
    }
  }

  return symbol_locations;
}
