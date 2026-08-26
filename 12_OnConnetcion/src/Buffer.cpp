#include "Buffer.hpp"
#include <iostream>
#include <cstring>

Buffer::Buffer()
{
}

Buffer::~Buffer()
{
}

void Buffer::append(const char* str , int len)
{
    for (int i = 0 ; i < len ; i++)
    {
        if (str[i] == '\0')
        {
            break;
        }
        m_buffer.push_back(str[i]);
    }
}
        
ssize_t Buffer::size()
{
    return m_buffer.size();
}
        
const char* Buffer::c_str()
{
    return m_buffer.c_str();
}
        
void Buffer::clear()
{
    m_buffer.clear();
}
        
void Buffer::getLine()
{
    m_buffer.clear();
    std::getline(std::cin , m_buffer);
}
        
void Buffer::setBuffer(const char* str)
{
    m_buffer.clear();
    m_buffer.append(str);
}
