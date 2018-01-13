#include "common.h"
#include <unistd.h>

timer common_logger_timer;
bool common_logger_isatty = (bool)isatty(STDOUT_FILENO);
