#ifndef DATABASE2_HXX
#define DATABASE2_HXX

#include "SymbolReference.hxx"
#include "ArtifactSymbols.hxx"
#include <SQLiteCpp/SQLiteCpp.h>

#include <vector>
#include <map>
#include <string>
#include <utility>
#include <functional>
#include <boost/noncopyable.hpp>

template <typename T>
class Lazy : boost::noncopyable {
private:
  mutable T* t;
  std::function<T*()> initializer;

  inline void initialize() const { if (!t) t = initializer(); }
public:
  inline explicit Lazy(std::function<T*()> initializer) : t(nullptr), initializer(initializer) {}
  inline ~Lazy() { delete t; }

  inline T * get() const { initialize(); return t; }

  inline T & operator*() const { return *get(); }
  inline T * operator->() const { return get(); }
};

class Dependency {
public:
  long long dependee_id, dependency_id;
  Dependency(long long dependee_id, long long dependency_id)
    : dependee_id(dependee_id)
    , dependency_id(dependency_id)
  {}
};

namespace std {

template<> struct less<Dependency>
{
  bool operator()(const Dependency& lhs, const Dependency& rhs) const {
    if (lhs.dependee_id < rhs.dependee_id)
      return true;
    else if (lhs.dependee_id == rhs.dependee_id)
      return lhs.dependency_id < rhs.dependency_id;
    else
      return false;
  }
};

} // namespace std

struct Artifact {
  long long id = -1;
  std::string name;
  std::string type;
  long long generating_command_id;
};

class Database2 {
public:
  SQLite::Database db;

private:
  Lazy<SQLite::Statement> create_command_stm;
  Lazy<SQLite::Statement> create_artifact_stm;
  Lazy<SQLite::Statement> artifact_id_by_name_stm;
  Lazy<SQLite::Statement> artifact_name_by_id_stm;
  Lazy<SQLite::Statement> artifact_by_name_stm;
  Lazy<SQLite::Statement> artifact_set_generating_command_stm;
  Lazy<SQLite::Statement> artifact_set_type_stm;
  Lazy<SQLite::Statement> create_symbol_stm;
  Lazy<SQLite::Statement> symbol_id_by_name_stm;
  Lazy<SQLite::Statement> create_symbol_reference_stm;
  Lazy<SQLite::Statement> create_dependency_stm;
  Lazy<SQLite::Statement> find_dependencies_stm;
  Lazy<SQLite::Statement> find_dependees_stm;
  Lazy<SQLite::Statement> undefined_symbols_stm;

public:
  explicit Database2(const std::string& file);

  void create();

  void truncate_symbols();
  void truncate_symbol_references();

  SQLite::Database& database() { return db; };
  SQLite::Statement statement(const std::string& query);

  void optimize();

  long long last_id();

  void create_command(const std::string& directory, const std::string& executable, const std::string& args, const std::string& output_type);

  void create_artifact(const std::string& name, const std::string& type, const long long generating_command_id = -1);

  bool artifact_by_name(Artifact& artifact);

  long long artifact_id_by_name(const std::string& name);

  std::string artifact_name_by_id(long long id);

  void artifact_set_generating_command(const long long artifact_id, const long long command_id);

  void artifact_set_type(const long long artifact_id, const std::string& type);

  void create_symbol(const std::string& name);

  int symbol_id_by_name(const std::string& name);

  int symbol_name_by_id(long long id);

  void create_symbol_reference(long long artifact_id, long long symbol_id, const char* category, const char type, long long size);

  void insert_symbol_references(long long artifact_id, const SymbolReferenceSet& symbols, const char* category);

  void insert_symbol_references(long long artifact_id, const ArtifactSymbols& symbols);

  void create_dependency(long long dependee_id, long long dependency_id);

  SQLite::Statement build_get_depend_stm(const std::string& select_field,
                                         const std::string& match_field,
                                         const std::vector<std::string>& included_types,
                                         const std::vector<std::string>& excluded_types);

  std::vector<long long> dependencies(long long dependee_id);

  std::vector<long long> dependees(long long dependency_id);

  std::vector<long long> undefined_symbols(const long long artifact_id);

  std::map<long long, std::vector<std::string>> resolve_symbols(const std::vector<long long>& symbols);

  static long long get_id(SQLite::Statement& stm);
  static std::vector<long long> get_ids(SQLite::Statement& stm);

  static std::string get_string(SQLite::Statement& stm);
};

#endif /* DATABASE2_HXX */
