#ifndef INSTRUMENTATION_HXX
#define INSTRUMENTATION_HXX

#ifdef ENABLE_INSTRUMENTATION

#include "itt_notify.hpp"

#else

#define ITT_DOMAIN(/*const char* */domain)
#define ITT_SCOPE(region, name)
#define ITT_SCOPE_TASK(/*const char* */name)
#define ITT_SCOPE_REGION(/*const char* */name)
#define ITT_FUNCTION_TASK()
#define ITT_ARG(/*const char* */name, /*number or string*/ value)
#define ITT_MARKER(/*const char* */name, /*enum Scope*/scope)
#define ITT_COUNTER(/*const char* */name, /*double */value)
#define ITT_SCOPE_TRACK(/*const char* */group, /*const char* */ track)

#endif

#endif // INSTRUMENTATION_HXX
