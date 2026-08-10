#pragma once
#include <string>

class Buffer
{
public:
    Buffer();
    ~Buffer();
    void append(const char* str , int len);
    ssize_t size();
    const char* c_str();
    void clear();
    void getLine();
private:
    std::string m_buffer;
};
