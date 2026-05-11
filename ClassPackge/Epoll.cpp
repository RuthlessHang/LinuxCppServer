#include "Epoll.h"
#include "util.h"
#include <cstring>
#include <unistd.h>

#define MAX_EVENTS 1024

Epoll::Epoll() :epfd(-1), events(nullptr)
{
	epfd = epoll_create(1024);
	errif(epfd == -1, "create epoll is failed!!!");
	events = new epoll_event[MAX_EVENTS];
	memset(events, 0, sizeof(events));
}

Epoll::~Epoll()
{
	if (events != nullptr)
	{
		delete[] events;
	}
	if (epfd != -1)
	{
		close(epfd);
	}
}

void Epoll::addEpollEvent(int fd, uint32_t op)
{
	epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.data.fd = fd;
	ev.events = op;
	errif(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1, "add epoll event is failed!!!");
}

std::vector<epoll_event> Epoll::poll(int timeout)
{
	std::vector<epoll_event> active_events;
	int nfds = epoll_wait(epfd, events, MAX_EVENTS, timeout);
	errif(nfds == -1, "epoll wait is failed!!!");
	for(int i = 0; i < nfds; ++i)
	{
		active_events.push_back(events[i]);
	}
	return active_events;
}
