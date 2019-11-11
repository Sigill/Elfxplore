#ifndef DATABASE2_HXX
#define DATABASE2_HXX

#include "Symbol.hxx"
#include "ArtifactSymbols.hxx"
#include <SQLiteCpp/SQLiteCpp.h>
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

public:
  explicit Database2(const std::string& file);

  void create();

  void truncate_symbols();
  void truncate_symbol_references();

  SQLite::Database& database() { return db; };

  long long last_id();

  void create_artifact(const std::string& name, const std::string& type);

  int artifact_id_by_name(const std::string& name);

  void create_symbol(const std::string& name);

  int symbol_id_by_name(const std::string& name);

  void create_symbol_reference(long long artifact_id, long long symbol_id, const char* category, const std::string& type, long long size);

  void insert_symbol_references(long long artifact_id, const SymbolSet& symbols, const char* category);

  void insert_symbol_references(long long artifact_id, const ArtifactSymbols& symbols);

  void create_dependency(long long dependee_id, long long dependency_id);

private:
  long long get_id(SQLite::Statement& stm);
};

#endif /* DATABASE2_HXX */
