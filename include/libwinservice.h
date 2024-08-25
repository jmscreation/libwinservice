#pragma once

#ifdef UNICODE
    #undef UNICODE
#endif

#include <iostream>
#include <string>
#include <algorithm>
#include <ctime>
#include <atomic>
#include <assert.h>
#include <windows.h>

#include "libwinservice_threadpool.h"
#include "libwinservice_base.h"
#include "libwinservice_install.h"
#include "libwinservice_ipc.h"