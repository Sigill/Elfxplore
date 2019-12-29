#ifndef DATABASE2_HXX
#define DATABASE2_HXX

#include "SymbolReference.hxx"
#include "ArtifactSymbols.hxx"
#include <SQLiteCpp/SQLiteCpp.h>

#include <set>
#include <vector>
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

class Database2 {
public:
  SQLite::Database db;

private:
  Lazy<SQLite::Statement> create_artifact_stm;
  Lazy<SQLite::Statement> artifact_id_by_name_stm;
  Lazy<SQLite::Statement> create_symbol_stm;
  Lazy<SQLite::Statement> symbol_id_by_name_stm;
  Lazy<SQLite::Statement> create_symbol_reference_stm;
  Lazy<SQLite::Statement> create_dependency_stm;
  Lazy<SQLite::Statement> find_dependencies_stm;
  Lazy<SQLite::Statement> find_dependees_stm;

public:
  explicit Database2(const std::string& file);

  void create();

  void truncate_symbols();
  void truncate_symbol_references();

  SQLite::Database& database() { return db; };
  SQLite::Statement statement(const std::string& query);

  long long last_id();

  void create_artifact(const std::string& name, const std::string& type);

  long long artifact_id_by_name(const std::string& name);

  std::string artifact_name_by_id(long long id);

  std::string artifact_type_by_id(long long id);

  void create_symbol(const std::string& name);

  int symbol_id_by_name(const std::string& name);

  void create_symbol_reference(long long artifact_id, long long symbol_id, const char* category, const char type, long long size);

  void insert_symbol_references(long long artifact_id, const SymbolReferenceSet& symbols, const char* category);

  void insert_symbol_references(long long artifact_id, const ArtifactSymbols& symbols);

  void create_dependency(long long dependee_id, long long dependency_id);

  std::vector<long long> dependencies(long long dependee_id);

  std::vector<long long> dependees(long long dependency_id);

  static long long get_id(SQLite::Statement& stm);
  static std::vector<long long> get_ids(SQLite::Statement& stm);

  static std::string get_string(SQLite::Statement& stm);
};

#endif /* DATABASE2_HXX */
