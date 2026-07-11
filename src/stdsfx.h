#ifndef STDSFX_H
#define STDSFX_H

#ifdef PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <cmath>
#include <ctime>
#include <cassert>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>

#endif // STDSFX_H