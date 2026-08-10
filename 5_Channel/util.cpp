#include "util.hpp"
#include <cstdio>
#include <cstdlib>

void errif(bool condition, const char *message)
{
    if (condition)
    {
        perror(message);
        exit(EXIT_FAILURE);
    }
}