#include "util.hpp"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>

void errif(bool condition, const char *message)
{
    if (condition)
    {
        perror(message);
        exit(EXIT_FAILURE);
    }
}

// Set a file descriptor to non-blocking mode
void setNonBlocking(int fd) 
{   
    fcntl(fd , F_SETFL ,fcntl(fd , F_GETFL) | O_NONBLOCK);
}