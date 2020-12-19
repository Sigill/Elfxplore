#ifndef LOGGER_HXX
#define LOGGER_HXX

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <ansi.hxx>

namespace CTXLogger {

enum class severity_level
{
  trace,
  debug,
  info,
  warning,
  error,
  fatal,
  always
};

extern severity_level _severity_level;

inline bool log_enabled(const severity_level lvl) {
  return _severity_level <= lvl;
}

class Message {
private:
  std::stringstream ss;

public:
  Message() = default;
  Message(const Message&) = delete;
  Message(Message&&) = default;
  Message& operator=(const Message&) = delete;
  Message& operator=(Message&&) = default;

  template<typename T>
  Message& operator<<(T&& value) & { ss << std::forward<T>(value); return *this; }

  template<typename T>
  Message&& operator<<(T&& value) && { ss << std::forward<T>(value); return std::move(*this); }

  operator std::string() const & { return ss.str(); }
  operator std::string() && { return std::move(ss).str(); }

  std::string to_string() const & { return ss.str(); }
  std::string to_string() && { return std::move(ss).str(); }
};

class Sink {
public:
  virtual void log(const std::string& m) = 0;
  virtual void log(const char* m) = 0;
};

class StreamSink : public Sink {
private:
  std::ostream& str;

public:
  StreamSink(std::ostream& str) : Sink(), str(str) {}

  void log(const std::string& m) override;
  void log(const char* m) override;

private:
  static FILE* get_standard_stream(const std::ostream& stream);

  static bool is_atty(const std::ostream& stream);
};

class ContextMessage {
private:
  std::string message;
  bool consumed = false;

public:
  ContextMessage() noexcept = default;
  ContextMessage(std::string message) noexcept : message(std::move(message)) {}
  ContextMessage(const ContextMessage&) = delete;
  ContextMessage(ContextMessage&& other) noexcept
    : message(std::move(other.message))
    , consumed(std::exchange(other.consumed, true))
  {}
  ContextMessage& operator=(const ContextMessage&) = delete;
  ContextMessage& operator=(ContextMessage&& other) noexcept {
    message = std::move(other.message);
    consumed = std::exchange(other.consumed, true);
    return *this;
  }

  bool isConsumed() const noexcept { return consumed; }

  std::string getMessage() noexcept {
    consumed = true;
    return std::move(message);
  }
};

class Logger;

class ContextMessageGuard {
private:
  Logger& logger;
  std::shared_ptr<ContextMessage> message;

public:
  ContextMessageGuard(Logger& logger, std::shared_ptr<ContextMessage> message) noexcept;
  ContextMessageGuard(const ContextMessageGuard&) = delete;
  ContextMessageGuard(ContextMessageGuard&&) noexcept = default;
  ContextMessageGuard& operator=(const ContextMessageGuard&) = delete;
  ContextMessageGuard& operator=(ContextMessageGuard&&) = delete;
  ~ContextMessageGuard() noexcept;
};

class ContextMessageGuardFactory {
private:
  Logger& logger;
  bool flush_immediately;

public:
  ContextMessageGuardFactory(Logger& logger, bool flush_immediately = false) : logger(logger), flush_immediately(flush_immediately) {}
  ContextMessageGuard operator<<=(std::string message);
};

class Logger {
private:
  std::unique_ptr<Sink> sink;
  std::vector<std::shared_ptr<ContextMessage>> context;

public:
  Logger() : sink(std::make_unique<StreamSink>(std::cerr)) {}

  void set_sink(std::unique_ptr<Sink> sink) {
    this->sink = std::move(sink);
  }

  ContextMessageGuard make_guard(std::string message) {
    return ContextMessageGuard(*this,
                               context.emplace_back(
                                 std::make_shared<ContextMessage>(
                                   std::move(message)
                                 )
                               )
                              );
  }

  ContextMessageGuard operator%=(std::string message) {
    return make_guard(std::move(message));
  }

  void log(const std::string& m);

  void log(const char* m);

  void operator|(const std::string& m);

  void operator|(const char* m);

  void log_exception(const std::exception& ex, std::size_t depth = 0);

  void flush();

private:
  void pop_context();

  friend ContextMessageGuard;
};

extern Logger logger;

extern bool ansi_support;

inline std::ostream& configure_ansi_support(std::ostream& os) {
  if (ansi_support)
    os << ansi::enable;
  return os;
}

} // namespace CTXLogger

#define LOG_ENABLED(lvl) ::CTXLogger::log_enabled(::CTXLogger::severity_level::lvl)

#define LOG_CONCAT_IMPL(x, y) x ## y
#define LOG_CONCAT(x, y) LOG_CONCAT_IMPL(x, y)

#define LOGGER ::CTXLogger::logger | ::CTXLogger::Message() << ::CTXLogger::configure_ansi_support

#define LOG_FLUSH() ::CTXLogger::logger.flush()


#define LOG(lvl) \
  for(bool live = ::CTXLogger::_severity_level <= ::CTXLogger::severity_level::lvl; live; live = false) \
    ::CTXLogger::logger | ::CTXLogger::Message() << ::CTXLogger::configure_ansi_support

#define LOG_STR(lvl, message) \
  for(bool live = ::CTXLogger::_severity_level <= ::CTXLogger::severity_level::lvl; live; live = false) \
    ::CTXLogger::logger.log(message)


#define LOG_CTX() \
  [[maybe_unused]] const ::CTXLogger::ContextMessageGuard LOG_CONCAT(__context_message_guard, __LINE__) = \
  ::CTXLogger::ContextMessageGuardFactory(::CTXLogger::logger) <<= ::CTXLogger::Message() << ::CTXLogger::configure_ansi_support

#define LOG_CTX_STR(message) \
  [[maybe_unused]] const ::CTXLogger::ContextMessageGuard LOG_CONCAT(__context_message_guard, __LINE__) = \
  ::CTXLogger::ContextMessageGuardFactory(::CTXLogger::logger) <<= message


#define LOG_CTX_FLUSH(lvl) \
  [[maybe_unused]] const ::CTXLogger::ContextMessageGuard LOG_CONCAT(__context_message_guard, __LINE__) = \
  ::CTXLogger::ContextMessageGuardFactory(::CTXLogger::logger, ::CTXLogger::_severity_level <= ::CTXLogger::severity_level::lvl) <<= ::CTXLogger::Message() << ::CTXLogger::configure_ansi_support

#define LOG_CTX_STR_FLUSH(lvl, message) \
  [[maybe_unused]] const ::CTXLogger::ContextMessageGuard LOG_CONCAT(__context_message_guard, __LINE__) = \
  ::CTXLogger::ContextMessageGuardFactory(::CTXLogger::logger, ::CTXLogger::_severity_level <= ::CTXLogger::severity_level::lvl) <<= message


#define LOG_EX(lvl, ex) \
  for(bool live = ::CTXLogger::_severity_level <= ::CTXLogger::severity_level::lvl; live; live = false) \
    LOG_FLUSH(), ::CTXLogger::logger.log_exception(ex)


/*
#if __cplusplus > 201703L
#  define PP_NARG(...) PP_NARG_(_ __VA_OPT__(,)__VA_ARGS__, 1, 0)
#else
#  define PP_NARG(...) PP_NARG_(_, ##__VA_ARGS__, 1, 0)
#endif

#define PP_NARG_(_, ...) PP_ARG_N(__VA_ARGS__)
#define PP_ARG_N(_1, N, ...) N
#define PP_RSEQ_N() 1, 0

#define CAT(a, ...) PRIMITIVE_CAT(a, __VA_ARGS__)
#define PRIMITIVE_CAT(a, ...) a ## __VA_ARGS__

#define IIF(c) PRIMITIVE_CAT(IIF_, c)
#define IIF_0(t, ...) __VA_ARGS__
#define IIF_1(t, ...) t

#define _LOG_CTX_0_() ::CTXLogger::ContextMessageGuardFactory(::CTXLogger::logger) <<= ::CTXLogger::Message() << ::CTXLogger::configure_ansi_support
#define _LOG_CTX_1_(lvl) ::CTXLogger::ContextMessageGuardFactory(::CTXLogger::logger, ::CTXLogger::_severity_level <= ::CTXLogger::severity_level::lvl) <<= ::CTXLogger::Message() << ::CTXLogger::configure_ansi_support

#define LOG_CTX(...) \
  [[maybe_unused]] const ::CTXLogger::ContextMessageGuard LOG_CONCAT(__context_message_guard, __LINE__) = \
  IIF(PP_NARG(__VA_ARGS__))( \
    _LOG_CTX_0_() , \
    _LOG_CTX_1_(__VA_ARGS__) \
  )
*/

#endif // LOGGER_HXX
