#include "logger.hxx"

#include <unistd.h> // isatty(), fileno()


namespace CTXLogger {

severity_level _severity_level = severity_level::info;

bool is_a_tty = false;


void StreamSink::log(const std::string& m) { str << m << std::endl; }

void StreamSink::log(const char* m) { str << m << std::endl; }

FILE*StreamSink::get_standard_stream(const std::ostream& stream)
{
  if (&stream == &std::cout)
    return stdout;
  else if ((&stream == &std::cerr) || (&stream == &std::clog))
    return stderr;

  return 0;
}

bool StreamSink::is_atty(const std::ostream& stream)
{
  FILE* std_stream = get_standard_stream(stream);

  // Unfortunately, fileno() ends with segmentation fault
  // if invalid file descriptor is passed. So we need to
  // handle this case gracefully and assume it's not a tty
  // if standard stream is not detected, and 0 is returned.
  if (!std_stream)
    return false;

  return ::isatty(fileno(std_stream));
}


void Logger::operator|(const char* m) {
  log(m);
}

void Logger::log(const std::string& m) {
  flush();
  sink->log(m);
}

void Logger::log(const char* m) {
  flush();
  sink->log(m);
}

void Logger::operator|(const std::string& m) {
  log(m);
}

void Logger::pop_context() {
  context.pop_back();
}

void Logger::flush() {
  for(const auto& m : context) {
    sink->log(m->getMessage());
  }

  context.clear();
}

ContextMessageGuard::~ContextMessageGuard() noexcept {
  if (!message->isConsumed())
    logger.pop_context();
}

Logger logger;

bool ansi_support = false;

} // namespace CTXLogger
