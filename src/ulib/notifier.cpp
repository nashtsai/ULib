// ============================================================================
//
// = LIBRARY
//    ULib - c++ library
//
// = FILENAME
//    notifier.cpp
//
// = AUTHOR
//    Stefano Casazza
//
// ============================================================================

#include <ulib/notifier.h>
#include <ulib/net/socket.h>
#include <ulib/utility/interrupt.h>

#if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
#  include "ulib/thread.h"
#endif

#ifdef HAVE_POLL_H
#  include <poll.h>
struct pollfd UNotifier::fds[1];
#else
UEventTime* UNotifier::time_obj;
#endif

#include <errno.h>

/*
typedef union epoll_data {
   void* ptr;
   int fd;
   uint32_t u32;
   uint64_t u64;
} epoll_data_t;

struct epoll_event {
   uint32_t events;   // Epoll events
   epoll_data_t data; // User data variable
};
*/

#ifdef USE_LIBEVENT
void UEventFd::operator()(int _fd, short event)
{
   U_TRACE(0, "UEventFd::operator()(%d,%hd)", _fd, event)

   int ret = (event == EV_READ ? handlerRead() : handlerWrite());

   U_INTERNAL_DUMP("ret = %d", ret)

   if (ret == U_NOTIFIER_DELETE) UNotifier::erase(this);
}
#elif defined(HAVE_EPOLL_WAIT)
int                              UNotifier::epollfd;
int                              UNotifier::add_mask; // EPOLLPRI|EPOLLERR|EPOLLHUP
struct epoll_event*              UNotifier::events;
struct epoll_event*              UNotifier::pevents;
#else
int                              UNotifier::fd_set_max;
int                              UNotifier::fd_read_cnt;
int                              UNotifier::fd_write_cnt;
fd_set                           UNotifier::fd_set_read;
fd_set                           UNotifier::fd_set_write;
#endif
#if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
void*                            UNotifier::pthread;
#endif

int                              UNotifier::nfd_ready; // the number of file descriptors ready for the requested I/O
bool                             UNotifier::bread;
uint32_t                         UNotifier::min_connection;
uint32_t                         UNotifier::num_connection;
uint32_t                         UNotifier::max_connection;
UEventFd*                        UNotifier::handler_event;
UEventFd**                       UNotifier::lo_map_fd;
UGenericHashMap<int,UEventFd*>*  UNotifier::hi_map_fd; // maps a fd to a node pointer

void UNotifier::init(bool bacquisition)
{
   U_TRACE(0, "UNotifier::init(%b)", bacquisition)

#ifndef HAVE_POLL_H
   U_INTERNAL_ASSERT_EQUALS(time_obj, 0)

   U_NEW_ULIB_OBJECT(time_obj, UEventTime);
#endif

#ifdef USE_LIBEVENT
   if (u_ev_base) (void) U_SYSCALL(event_reinit, "%p", u_ev_base); // NB: reinitialized the event base after fork()...
   else           u_ev_base = (struct event_base*) U_SYSCALL_NO_PARAM(event_init);

   U_INTERNAL_ASSERT_POINTER(u_ev_base)
#elif defined(HAVE_EPOLL_WAIT)
   int old = epollfd;

   U_INTERNAL_DUMP("old = %d", old)

#  ifdef HAVE_EPOLL_CREATE1
   epollfd = U_SYSCALL(epoll_create1, "%d", EPOLL_CLOEXEC);
   if (epollfd != -1 || errno != ENOSYS) goto next;
#  endif
   epollfd = U_SYSCALL(epoll_create, "%u", max_connection);
next:
   U_INTERNAL_ASSERT_DIFFERS(epollfd, -1)

   if (old)
      {
      U_INTERNAL_DUMP("num_connection = %u", num_connection)

      if (bacquisition &&
          num_connection)
         {
         U_INTERNAL_ASSERT_POINTER(lo_map_fd)
         U_INTERNAL_ASSERT_POINTER(hi_map_fd)

         // NB: reinitialized all after fork()...

         for (int fd = 1; fd < (int32_t)max_connection; ++fd)
            {
            if ((handler_event = lo_map_fd[fd]))
               {
               U_INTERNAL_DUMP("fd = %d op_mask = %d %B", fd, handler_event->op_mask, handler_event->op_mask)

               (void) U_SYSCALL(epoll_ctl, "%d,%d,%d,%p", old, EPOLL_CTL_DEL, fd, (struct epoll_event*)1);

               struct epoll_event _events = { handler_event->op_mask | add_mask, { handler_event } };

               (void) U_SYSCALL(epoll_ctl, "%d,%d,%d,%p", epollfd, EPOLL_CTL_ADD, fd, &_events);
               }
            }

         if (hi_map_fd->first())
            {
            do {
               handler_event = hi_map_fd->elem();

               U_INTERNAL_DUMP("op_mask = %d %B", handler_event->op_mask, handler_event->op_mask)

               (void) U_SYSCALL(epoll_ctl, "%d,%d,%d,%p", old, EPOLL_CTL_DEL, handler_event->fd, (struct epoll_event*)1);

               struct epoll_event _events = { handler_event->op_mask | add_mask, { handler_event } };

               (void) U_SYSCALL(epoll_ctl, "%d,%d,%d,%p", epollfd, EPOLL_CTL_ADD, handler_event->fd, &_events);
               }
            while (hi_map_fd->next());
            }
         }

      (void) U_SYSCALL(close, "%d", old);

      return;
      }
#endif

   if (lo_map_fd == 0)
      {
      U_INTERNAL_ASSERT_EQUALS(hi_map_fd, 0)
      U_INTERNAL_ASSERT_MAJOR(max_connection, 0)

      lo_map_fd = (UEventFd**) UMemoryPool::_malloc(max_connection, sizeof(UEventFd*), true);

      hi_map_fd = U_NEW(UGenericHashMap<int,UEventFd*>);

      hi_map_fd->allocate(max_connection);

#  if defined(HAVE_EPOLL_WAIT) && !defined(USE_LIBEVENT)
      U_INTERNAL_ASSERT_EQUALS(events, 0)

      pevents = events = (struct epoll_event*) UMemoryPool::_malloc(max_connection+1, sizeof(struct epoll_event), true);
#  endif
      }
}

bool UNotifier::isHandler(int fd)
{
   U_TRACE(0, "UNotifier::isHandler(%d)", fd)

   U_INTERNAL_ASSERT_DIFFERS(fd, -1)

   U_INTERNAL_DUMP("num_connection = %d min_connection = %d", num_connection, min_connection)

   if (num_connection > min_connection)
      {
      U_INTERNAL_ASSERT_POINTER(lo_map_fd)
      U_INTERNAL_ASSERT_POINTER(hi_map_fd)

      if (fd < (int32_t)max_connection)
         {
         if (lo_map_fd[fd]) U_RETURN(true);

         U_RETURN(false);
         }

#  if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
      if (pthread) ((UThread*)pthread)->lock();
#  endif

      if (hi_map_fd->find(fd)) U_RETURN(true);

#  if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
      if (pthread) ((UThread*)pthread)->unlock();
#  endif
      }

   U_RETURN(false);
}

void UNotifier::resetHandler(int fd)
{
   U_TRACE(0, "UNotifier::resetHandler(%d)", fd)

   if (fd < (int32_t)max_connection) lo_map_fd[fd] = 0;
   else
      {
#  if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
      if (pthread) ((UThread*)pthread)->lock();
#  endif

      (void) hi_map_fd->erase(fd);

#  if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
      if (pthread) ((UThread*)pthread)->unlock();
#  endif
      }
}

bool UNotifier::setHandler(int fd)
{
   U_TRACE(0, "UNotifier::setHandler(%d)", fd)

   U_INTERNAL_ASSERT_DIFFERS(fd, -1)
   U_INTERNAL_ASSERT_POINTER(lo_map_fd)
   U_INTERNAL_ASSERT_POINTER(hi_map_fd)

   if (fd < (int32_t)max_connection)
      {
      if (lo_map_fd[fd])
         {
         handler_event = lo_map_fd[fd];

         U_INTERNAL_DUMP("handler_event = %p", handler_event)

         U_RETURN(true);
         }
      }

#if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
   if (pthread) ((UThread*)pthread)->lock();
#endif

   if (hi_map_fd->find(fd))
      {
      handler_event = hi_map_fd->elem();

      U_INTERNAL_DUMP("handler_event = %p", handler_event)

      U_RETURN(true);
      }

#if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
   if (pthread) ((UThread*)pthread)->unlock();
#endif

   U_RETURN(false);
}

void UNotifier::insert(UEventFd* item)
{
   U_TRACE(0, "UNotifier::insert(%p)", item)

   U_INTERNAL_ASSERT_POINTER(item)

   int fd = item->fd;

   U_INTERNAL_DUMP("fd = %d item->op_mask = %B num_connection = %d", fd, item->op_mask, num_connection)

   U_INTERNAL_ASSERT_DIFFERS(fd, -1)

   if (fd < (int32_t)max_connection) lo_map_fd[fd] = item;
   else
      {
#  if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
      if (pthread) ((UThread*)pthread)->lock();
#  endif

      hi_map_fd->insert(fd, item);

#if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
      if (pthread) ((UThread*)pthread)->unlock();
#  endif
      }

#ifdef USE_LIBEVENT
   U_INTERNAL_ASSERT_POINTER(u_ev_base)

   int mask = EV_PERSIST | ((item->op_mask & (EPOLLIN | EPOLLRDHUP)) != 0 ? EV_READ : EV_WRITE);

   U_INTERNAL_DUMP("mask = %B", mask)

   item->pevent = U_NEW(UEvent<UEventFd>(fd, mask, *item));

   (void) UDispatcher::add(*(item->pevent));
#elif defined(HAVE_EPOLL_WAIT)
   U_INTERNAL_ASSERT_MAJOR(epollfd, 0)

   U_INTERNAL_DUMP("add_mask = %d %B", add_mask, add_mask)

   struct epoll_event _events = { item->op_mask | add_mask, { item } };

   (void) U_SYSCALL(epoll_ctl, "%d,%d,%d,%p", epollfd, EPOLL_CTL_ADD, fd, &_events);
#else
   if ((item->op_mask & (EPOLLIN | EPOLLRDHUP)) != 0)
      {
      U_INTERNAL_ASSERT_EQUALS(FD_ISSET(fd, &fd_set_read), 0)
      U_INTERNAL_ASSERT_EQUALS(FD_ISSET(fd, &fd_set_write), 0)

      FD_SET(fd, &fd_set_read);

      U_INTERNAL_ASSERT(fd_read_cnt >= 0)

      ++fd_read_cnt;

#  ifndef _MSWINDOWS_
      U_INTERNAL_DUMP("fd_set_read = %B", __FDS_BITS(&fd_set_read)[0])
#  endif
      }
   else
      {
      U_INTERNAL_ASSERT_DIFFERS(item->op_mask & EPOLLOUT, 0)
      U_INTERNAL_ASSERT_EQUALS(FD_ISSET(fd, &fd_set_write), 0)

      FD_SET(fd, &fd_set_write);

      U_INTERNAL_ASSERT(fd_write_cnt >= 0)

      ++fd_write_cnt;

#  ifndef _MSWINDOWS_
      U_INTERNAL_DUMP("fd_set_write = %B", __FDS_BITS(&fd_set_write)[0])
#  endif
      }

   if (fd_set_max <= fd) fd_set_max = fd + 1;

   U_INTERNAL_DUMP("fd_set_max = %d", fd_set_max)
#endif
}

void UNotifier::handlerDelete(int fd, int mask)
{
   U_TRACE(0, "UNotifier::handlerDelete(%d,%d)", fd, mask)

   U_INTERNAL_ASSERT_MAJOR(fd, 0)

   resetHandler(fd);

#ifdef USE_LIBEVENT
#elif defined(HAVE_EPOLL_WAIT)
#else
   if ((mask & (EPOLLIN | EPOLLRDHUP)) != 0)
      {
      U_INTERNAL_ASSERT(FD_ISSET(fd, &fd_set_read))
      U_INTERNAL_ASSERT_EQUALS(FD_ISSET(fd, &fd_set_write), 0)

      FD_CLR(fd, &fd_set_read);

#  ifndef _MSWINDOWS_
      U_INTERNAL_DUMP("fd_set_read = %B", __FDS_BITS(&fd_set_read)[0])
#  endif

      --fd_read_cnt;

      U_INTERNAL_ASSERT(fd_read_cnt >= 0)
      }
   else
      {
      U_INTERNAL_ASSERT_DIFFERS(mask & EPOLLOUT, 0)
      U_INTERNAL_ASSERT(FD_ISSET(fd, &fd_set_write))
      U_INTERNAL_ASSERT_EQUALS(FD_ISSET(fd, &fd_set_read), 0)

      FD_CLR(fd, &fd_set_write);

#  ifndef _MSWINDOWS_
      U_INTERNAL_DUMP("fd_set_write = %B", __FDS_BITS(&fd_set_write)[0])
#  endif

      --fd_write_cnt;

      U_INTERNAL_ASSERT(fd_write_cnt >= 0)
      }

   if (empty() == false) fd_set_max = getNFDS();
#endif
}

U_NO_EXPORT void UNotifier::handlerDelete(UEventFd* item)
{
   U_TRACE(0, "UNotifier::handlerDelete(%p)", item)

   U_INTERNAL_ASSERT_POINTER(item)

   int fd   = item->fd,
       mask = item->op_mask;

   U_INTERNAL_DUMP("fd = %d op_mask = %B num_connection = %d min_connection = %d", fd, mask, num_connection, min_connection)

   item->handlerDelete();

   handlerDelete(fd, mask);
}

void UNotifier::modify(UEventFd* item)
{
   U_TRACE(0, "UNotifier::modify(%p)", item)

   U_INTERNAL_ASSERT_POINTER(item)

   int fd = item->fd;

   U_INTERNAL_DUMP("fd = %d op_mask = %B", fd, item->op_mask)

#ifdef USE_LIBEVENT
   U_INTERNAL_ASSERT_POINTER(u_ev_base)

   int mask = EV_PERSIST | ((item->op_mask & (EPOLLIN | EPOLLRDHUP)) != 0 ? EV_READ : EV_WRITE);

   U_INTERNAL_DUMP("mask = %B", mask)

   UDispatcher::del(item->pevent);
             delete item->pevent;
                    item->pevent = U_NEW(UEvent<UEventFd>(fd, mask, *item));

   (void) UDispatcher::add(*(item->pevent));
#elif defined(HAVE_EPOLL_WAIT)
   U_INTERNAL_ASSERT_MAJOR(epollfd, 0)

   struct epoll_event _events = { item->op_mask | add_mask, { item } };

   (void) U_SYSCALL(epoll_ctl, "%d,%d,%d,%p", epollfd, EPOLL_CTL_MOD, fd, &_events);
#else
#  ifndef _MSWINDOWS_
   U_INTERNAL_DUMP("fd_set_read  = %B", __FDS_BITS(&fd_set_read)[0])
   U_INTERNAL_DUMP("fd_set_write = %B", __FDS_BITS(&fd_set_write)[0])
#endif

   if ((item->op_mask & (EPOLLIN | EPOLLRDHUP)) != 0)
      {
      U_INTERNAL_ASSERT(FD_ISSET(fd, &fd_set_write))
      U_INTERNAL_ASSERT_EQUALS(FD_ISSET(fd, &fd_set_read), 0)

      FD_SET(fd, &fd_set_read);
      FD_CLR(fd, &fd_set_write);

      ++fd_read_cnt;
      --fd_write_cnt;
      }
   else
      {
      U_INTERNAL_DUMP("op_mask = %d", item->op_mask)

      U_INTERNAL_ASSERT(FD_ISSET(fd, &fd_set_read))
      U_INTERNAL_ASSERT_DIFFERS(item->op_mask & EPOLLOUT, 0)
      U_INTERNAL_ASSERT_EQUALS(FD_ISSET(fd, &fd_set_write), 0)

      FD_CLR(fd, &fd_set_read);
      FD_SET(fd, &fd_set_write);

      --fd_read_cnt;
      ++fd_write_cnt;
      }

#  ifndef _MSWINDOWS_
   U_INTERNAL_DUMP("fd_set_read  = %B", __FDS_BITS(&fd_set_read)[0])
   U_INTERNAL_DUMP("fd_set_write = %B", __FDS_BITS(&fd_set_write)[0])
#endif

   U_INTERNAL_ASSERT(fd_read_cnt  >= 0)
   U_INTERNAL_ASSERT(fd_write_cnt >= 0)
#endif
}

void UNotifier::erase(UEventFd* item)
{
   U_TRACE(1, "UNotifier::erase(%p)", item)

   U_INTERNAL_ASSERT_POINTER(item)

#ifdef U_COVERITY_FALSE_POSITIVE
   if (item->fd > 0)
#endif
   handlerDelete(item);

#ifdef USE_LIBEVENT
   UDispatcher::del(item->pevent);
             delete item->pevent;
                    item->pevent = 0;
#endif
}

int UNotifier::waitForEvent(int fd_max, fd_set* read_set, fd_set* write_set, UEventTime* timeout)
{
   U_TRACE(1, "UNotifier::waitForEvent(%d,%p,%p,%p)", fd_max, read_set, write_set, timeout)

   int result;

#ifdef USE_LIBEVENT
   result = -1;
#elif defined(HAVE_EPOLL_WAIT)
   int _timeout = (timeout ? timeout->getMilliSecond() : -1);
loop:
   result = U_SYSCALL(epoll_wait, "%d,%p,%u,%d", epollfd, events, max_connection, _timeout);
#else
   static struct timeval   tmp;
          struct timeval* ptmp = (timeout == 0 ? 0 : &tmp);
loop:
#  if defined(DEBUG) && !defined(_MSWINDOWS_)
   if ( read_set) U_INTERNAL_DUMP(" read_set = %B", __FDS_BITS( read_set)[0])
   if (write_set) U_INTERNAL_DUMP("write_set = %B", __FDS_BITS(write_set)[0])
#  endif

   if (timeout)
      {
      // On Linux, the function select modifies timeout to reflect the amount of time not slept; most other implementations do not do this.
      // This causes problems both when Linux code which reads timeout is ported to other operating systems, and when code is ported to
      // Linux that reuses a struct timeval for multiple selects in a loop without reinitializing it.
      // Consider timeout to be undefined after select returns

      tmp = *(struct timeval*)timeout;

      U_INTERNAL_DUMP("timeout = { %ld %6ld }", tmp.tv_sec, tmp.tv_usec)
      }

   // If both fields of the timeval structure are zero, then select() returns immediately.
   // (This is useful for polling). If ptmp is NULL (no timeout), select() can block indefinitely...

   result = U_SYSCALL(select, "%d,%p,%p,%p,%p", fd_max, read_set, write_set, 0, ptmp);
#endif

#ifndef USE_LIBEVENT
   if (result == 0) // timeout
      {
      // call the manager of timeout

      if (timeout)
         {
         int ret = timeout->handlerTime();

         // ---------------
         // return value:
         // ---------------
         // -1 - normal
         //  0 - monitoring
         // ---------------

         if (ret == 0)
            {
            if (empty() == false)
               {
#        ifndef HAVE_EPOLL_WAIT
               if (read_set)   *read_set = fd_set_read;
               if (write_set) *write_set = fd_set_write;
#        endif

               goto loop;
               }
            }
         }
      }
   else if (result == -1)
      {
      if (errno == EINTR                           &&
          UInterrupt::checkForEventSignalPending() &&
          UInterrupt::exit_loop_wait_event_for_signal == false)
         {
         goto loop;
         }

#  ifndef HAVE_EPOLL_WAIT
      if (errno == EBADF) // ci sono descrittori di file diventati invalidi (possibile con EPIPE)
         {
         removeBadFd();

         if (empty() == false)
            {
            if (read_set)   *read_set = fd_set_read;
            if (write_set) *write_set = fd_set_write;

            goto loop;
            }
         }
#  endif
      }

#  if defined(DEBUG) && !defined(_MSWINDOWS_)
   if ( read_set) U_INTERNAL_DUMP(" read_set = %B", __FDS_BITS( read_set)[0])
   if (write_set) U_INTERNAL_DUMP("write_set = %B", __FDS_BITS(write_set)[0])
#  endif
#endif

   U_RETURN(result);
}

#ifndef USE_LIBEVENT
U_NO_EXPORT void UNotifier::notifyHandlerEvent()
{
   U_TRACE(0, "UNotifier::notifyHandlerEvent()")

   U_INTERNAL_ASSERT_POINTER(handler_event)

   U_INTERNAL_DUMP("handler_event = %p bread = %b nfd_ready = %d fd = %d op_mask = %d %B",
                    handler_event, bread, nfd_ready, handler_event->fd, handler_event->op_mask, handler_event->op_mask)

   int ret = (LIKELY(bread) ? handler_event->handlerRead()
                            : handler_event->handlerWrite());

   if (ret == U_NOTIFIER_DELETE)
      {
      U_INTERNAL_DUMP("num_connection = %u", num_connection)

#  ifdef HAVE_EPOLL_WAIT
      U_INTERNAL_ASSERT_EQUALS(handler_event, pevents->data.ptr)
#  endif

#  ifdef U_COVERITY_FALSE_POSITIVE
      if (handler_event->fd > 0)
#  endif
      handlerDelete(handler_event);
      }
}
#endif

void UNotifier::waitForEvent(UEventTime* timeout)
{
   U_TRACE(0, "UNotifier::waitForEvent(%p)", timeout)

#ifdef USE_LIBEVENT
   (void) UDispatcher::dispatch(UDispatcher::ONCE);
#elif defined(HAVE_EPOLL_WAIT)
   nfd_ready = waitForEvent(0, 0, 0, timeout);
#else
   U_INTERNAL_ASSERT(fd_read_cnt > 0 || fd_write_cnt > 0)

   fd_set read_set, write_set;

   if (fd_read_cnt)   read_set = fd_set_read;
   if (fd_write_cnt) write_set = fd_set_write;

   nfd_ready = waitForEvent(fd_set_max,
                (fd_read_cnt  ? &read_set
                              : 0),
                (fd_write_cnt ? &write_set
                              : 0),
                timeout);
#endif
#ifndef USE_LIBEVENT
   if (LIKELY(nfd_ready > 0))
      {
#  ifdef HAVE_EPOLL_WAIT
      pevents = events + nfd_ready;
loop:
      --pevents;

      handler_event = (UEventFd*)pevents->data.ptr;

      U_INTERNAL_DUMP("handler_event = %p bread = %b bwrite = %b events[%d].events = %d %B", handler_event,
                      ((pevents->events & (EPOLLIN | EPOLLRDHUP)) != 0),
                      ((pevents->events &  EPOLLOUT)              != 0),
                       (pevents -events), pevents->events, pevents->events)

      U_INTERNAL_ASSERT(pevents >= events)
      U_INTERNAL_ASSERT_DIFFERS(handler_event, 0)

      if (LIKELY(handler_event->fd != -1))
         {
         /**
          * EPOLLIN     = 0x0001
          * EPOLLPRI    = 0x0002
          * EPOLLOUT    = 0x0004
          * EPOLLERR    = 0x0008
          * EPOLLHUP    = 0x0010
          * EPOLLRDNORM = 0x0040
          * EPOLLRDBAND = 0x0080
          * EPOLLWRNORM = 0x0100
          * EPOLLWRBAND = 0x0200
          * EPOLLMSG    = 0x0400
          * EPOLLRDHUP  = 0x2000
          *
          * <10000000 00000100 00000000 00000000>
          *  EPOLLIN       EPOLLRDHUP
          */

         if ((pevents->events & (EPOLLERR | EPOLLHUP)) == 0)
            {
            bread = ((pevents->events & (EPOLLIN | EPOLLRDHUP)) != 0);

            notifyHandlerEvent();
            }
         else
            {
            handler_event->handlerError();

            handler_event->fd = -1;
            }
         }

      if (--nfd_ready) goto loop;

      U_INTERNAL_DUMP("events[%d]: return", (pevents-events))

      U_INTERNAL_ASSERT_EQUALS(pevents, events)
#  else
      int fd, fd_cnt = (fd_read_cnt + fd_write_cnt);

      U_INTERNAL_DUMP("fd_cnt = %d fd_set_max = %d", fd_cnt, fd_set_max)

      U_INTERNAL_ASSERT(nfd_ready <= fd_cnt)

      for (fd = 1; fd < fd_set_max; ++fd)
         {
         bread = (fd_read_cnt && FD_ISSET(fd, &read_set));

         if ((bread                                            ||
              (fd_write_cnt && FD_ISSET(fd, &write_set)) != 0) &&
             setHandler(fd))
            {
            notifyHandlerEvent();

            if (--nfd_ready == 0)
               {
               U_INTERNAL_DUMP("fd = %d: return", fd)

               if (fd_cnt > (fd_read_cnt + fd_write_cnt)) fd_set_max = getNFDS();

               return;
               }
            }
         }
#  endif
      }
#endif
}

void UNotifier::callForAllEntryDynamic(bPFpv function)
{
   U_TRACE(0, "UNotifier::callForAllEntryDynamic(%p)", function)

   U_INTERNAL_DUMP("num_connection = %u", num_connection)

   U_INTERNAL_ASSERT_MAJOR(num_connection, min_connection)

   UEventFd* item;

   for (int fd = 1; fd < (int32_t)max_connection; ++fd)
      {
      if ((item = lo_map_fd[fd]))
         {
         U_INTERNAL_DUMP("fd = %d op_mask = %B", item->fd, item->op_mask)

         if (function(item)) erase(item);
         }
      }

   if (hi_map_fd->first())
      {
      do {
         item = hi_map_fd->elem();

         U_INTERNAL_DUMP("fd = %d op_mask = %B", item->fd, item->op_mask)

         if (function(item)) erase(item);
         }
      while (hi_map_fd->next());
      }
}

void UNotifier::clear()
{
   U_TRACE(0, "UNotifier::clear()")

   U_INTERNAL_DUMP("lo_map_fd = %p", lo_map_fd)

   if (lo_map_fd == 0) return;

   U_INTERNAL_ASSERT_POINTER(hi_map_fd)
   U_INTERNAL_ASSERT_MAJOR(max_connection, 0)

   UEventFd* item;

   U_INTERNAL_DUMP("num_connection = %u", num_connection)

   if (num_connection)
      {
      for (int fd = 1; fd < (int32_t)max_connection; ++fd)
         {
         if ((item = lo_map_fd[fd]))
            {
            U_INTERNAL_DUMP("fd = %d op_mask = %B", item->fd, item->op_mask)

            if (item->fd != -1) erase(item);
            }
         }

      if (hi_map_fd->first())
         {
         do {
            item = hi_map_fd->elem();

            U_INTERNAL_DUMP("fd = %d op_mask = %B", item->fd, item->op_mask)

            if (item->fd != -1) erase(item);
            }
         while (hi_map_fd->next());
         }
      }

#if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
   if (pthread == 0)
#endif
      {
      UMemoryPool::_free(lo_map_fd, max_connection, sizeof(UEventFd*));

      hi_map_fd->deallocate();

      delete hi_map_fd;
      }

#if defined(HAVE_EPOLL_WAIT) && !defined(USE_LIBEVENT)
   U_INTERNAL_ASSERT_POINTER(events)

#if defined(ENABLE_THREAD) && defined(U_SERVER_THREAD_APPROACH_SUPPORT)
   if (pthread == 0)
#endif
   UMemoryPool::_free(events, max_connection + 1, sizeof(struct epoll_event));

   (void) U_SYSCALL(close, "%d", epollfd);
#endif
}

#ifdef USE_LIBEVENT
// nothing
#elif defined(HAVE_EPOLL_WAIT)
#else
int UNotifier::getNFDS() // nfds is the highest-numbered file descriptor in any of the three sets, plus 1.
{
   U_TRACE(0, "UNotifier::getNFDS()")

   int nfds = 0;

   U_INTERNAL_DUMP("fd_set_max = %d", fd_set_max)

   for (int fd = 1; fd < fd_set_max; ++fd)
      {
      if (isHandler(fd))
         {
         U_INTERNAL_DUMP("fd = %d", fd)

         if (nfds < fd) nfds = fd;
         }
      }

   ++nfds;

   U_RETURN(nfds);
}

void UNotifier::removeBadFd()
{
   U_TRACE(1, "UNotifier::removeBadFd()")

   bool bwrite;
   fd_set fdmask;
   fd_set* rmask;
   fd_set* wmask;
   struct timeval polling = { 0L, 0L };

   for (int fd = 1; fd < fd_set_max; ++fd)
      {
      if (setHandler(fd))
         {
         U_INTERNAL_DUMP("fd = %d op_mask = %B handler_event = %p", handler_event->fd, handler_event->op_mask, handler_event)

         bread  = (fd_read_cnt  && ((handler_event->op_mask & (EPOLLIN | EPOLLRDHUP)) != 0));
         bwrite = (fd_write_cnt && ((handler_event->op_mask &  EPOLLOUT)              != 0));

         FD_ZERO(&fdmask);
         FD_SET(fd, &fdmask);

         rmask = (bread  ? &fdmask : 0);
         wmask = (bwrite ? &fdmask : 0);

         U_INTERNAL_DUMP("fdmask = %B", __FDS_BITS(&fdmask)[0])

         nfd_ready = U_SYSCALL(select, "%d,%p,%p,%p,%p", fd+1, rmask, wmask, 0, &polling);

         U_INTERNAL_DUMP("fd = %d op_mask = %B ISSET(read) = %b ISSET(write) = %b", fd, handler_event->op_mask,
                           (rmask ? FD_ISSET(fd, rmask) : false),
                           (wmask ? FD_ISSET(fd, wmask) : false))

         if (nfd_ready)
            {
            if ( nfd_ready == -1 &&
                (nfd_ready = (bread + bwrite)))
               {
               handler_event->handlerError();
               }
            else
               {
               notifyHandlerEvent();
               }
            }
         }
      }
}
#endif

// param timeoutMS specified the timeout value, in milliseconds.
// A negative value indicates no timeout, i.e. an infinite wait

#ifdef HAVE_POLL_H
int UNotifier::waitForEvent(int timeoutMS)
{
   U_TRACE(1, "UNotifier::waitForEvent(%d)", timeoutMS)

   int ret;

#ifdef DEBUG
   if (timeoutMS > 0) U_INTERNAL_ASSERT(timeoutMS >= 100)
#endif

loop:
   // The timeout argument specifies the minimum number of milliseconds that poll() will block
   // (This interval will be rounded up to the system clock granularity, and kernel scheduling
   //  delays mean that the blocking interval may overrun by a small amount)
   // Specifying a negative value in timeout means an infinite timeout
   // Specifying a timeout of zero causes poll() to return immediately, even if no file descriptors are ready

   ret = U_SYSCALL(poll, "%p,%d,%d", fds, 1, timeoutMS);

   U_INTERNAL_DUMP("errno = %d", errno)

   if (ret >  0) U_RETURN(ret);
   if (ret == 0)
      {
      u_errno = errno == EAGAIN;

      U_RETURN(0);
      }

   if (errno == EINTR &&
       UInterrupt::checkForEventSignalPending())
      {
      goto loop;
      }

   U_RETURN(-1);
}
#endif

// param timeoutms specified the timeout value, in milliseconds.
// a negative value indicates no timeout, i.e. an infinite wait

int UNotifier::waitForRead(int fd, int timeoutMS)
{
   U_TRACE(0, "UNotifier::waitForRead(%d,%d)", fd, timeoutMS)

   U_INTERNAL_ASSERT_RANGE(0, fd, 4096)

#ifdef DEBUG
   if (timeoutMS > 0) U_INTERNAL_ASSERT(timeoutMS >= 100)
#endif

#ifdef HAVE_POLL_H
   // NB: POLLRDHUP stream socket peer closed connection, or ***** shut down writing half of connection ****

   fds[0].fd     = fd;
   fds[0].events = POLLIN;

   int ret = waitForEvent(timeoutMS);

   // NB: we don't check for POLLERR or POLLHUP because we have problem in same case (command.test)

   U_INTERNAL_DUMP("revents = %d %B", fds[0].revents, fds[0].revents)
#else

#  ifdef _MSWINDOWS_
   HANDLE h = is_pipe(fd);

   if (h != INVALID_HANDLE_VALUE)
      {
      DWORD count = 0;

      while (U_SYSCALL(PeekNamedPipe, "%p,%p,%ld,%p,%p,%p", h, 0, 0, 0, &count, 0) &&
             count == 0                                                            &&
             timeoutMS > 0)
         {
         Sleep(1000);

         timeoutMS -= 1000;
         }

      U_RETURN(count);
      }
#  endif

   // If both fields of the timeval structure are zero, then select() returns immediately.
   // (This is useful for polling). If ptmp is NULL (no timeout), select() can block indefinitely...

   UEventTime* ptime;

   if (timeoutMS < 0) ptime = 0;
   else
      {
      U_INTERNAL_ASSERT_POINTER(time_obj)

      time_obj->UTimeVal::setMilliSecond(timeoutMS);

      ptime = time_obj;
      }

   fd_set fdmask;
   FD_ZERO(&fdmask);
   FD_SET(fd, &fdmask);

   int ret = waitForEvent(fd + 1, &fdmask, 0, ptime);
#endif

   U_RETURN(ret);
}

int UNotifier::waitForWrite(int fd, int timeoutMS)
{
   U_TRACE(0, "UNotifier::waitForWrite(%d,%d)", fd, timeoutMS)

   U_INTERNAL_ASSERT_RANGE(0, fd, 4096)
   U_INTERNAL_ASSERT_DIFFERS(timeoutMS, 0)

#ifdef DEBUG
   if (timeoutMS != -1) U_INTERNAL_ASSERT(timeoutMS >= 100)
#endif

#ifdef HAVE_POLL_H
   // NB: POLLRDHUP stream socket peer closed connection, or ***** shut down writing half of connection ****

   fds[0].fd      = fd;
   fds[0].events  = POLLOUT;
   fds[0].revents = 0;

   int ret = waitForEvent(timeoutMS);

   // NB: POLLERR Error condition (output only)
   //     POLLHUP Hang up         (output only)

   U_INTERNAL_DUMP("revents = %d %B", fds[0].revents, fds[0].revents)

   if (ret == 1 &&
       (fds[0].revents & (POLLERR | POLLHUP)) != 0)
      {
      U_INTERNAL_ASSERT_EQUALS(::write(fd,fds,1), -1)

      U_RETURN(-1);
      }
#else

#  ifdef _MSWINDOWS_
   if (is_pipe(fd) != INVALID_HANDLE_VALUE) U_RETURN(1);
#  endif

   // If both fields of the timeval structure are zero, then select() returns immediately.
   // (This is useful for polling). If ptmp is NULL (no timeout), select() can block indefinitely...

   UEventTime* ptime;

   if (timeoutMS < 0) ptime = 0;
   else
      {
      U_INTERNAL_ASSERT_POINTER(time_obj)

      time_obj->UTimeVal::setMilliSecond(timeoutMS);

      ptime = time_obj;
      }

   fd_set fdmask;
   FD_ZERO(&fdmask);
   FD_SET(fd, &fdmask);

   int ret = waitForEvent(fd + 1, 0, &fdmask, ptime);
#endif

   U_RETURN(ret);
}

// param timeoutms specified the timeout value, in milliseconds.
// a negative value indicates no timeout, i.e. an infinite wait

int UNotifier::read(int fd, char* buffer, int count, int timeoutMS)
{
   U_TRACE(1, "UNotifier::read(%d,%p,%d,%d)", fd, buffer, count, timeoutMS)

   if (fd < 0 &&
       timeoutMS != -1 &&
       waitForRead(fd, timeoutMS) != 1)
      {
      U_RETURN(-1);
      }

   int iBytesRead;

loop:
#ifdef _MSWINDOWS_
   (void) U_SYSCALL(ReadFile, "%p,%p,%lu,%p,%p", (HANDLE)_get_osfhandle(fd), buffer, count, (DWORD*)&iBytesRead, 0);
#else
   iBytesRead = U_SYSCALL(read, "%d,%p,%u", fd, buffer, count);
#endif

   if (iBytesRead > 0)
      {
      U_INTERNAL_DUMP("BytesRead(%d) = %#.*S", iBytesRead, iBytesRead, buffer)

      U_RETURN(iBytesRead);
      }

   if (iBytesRead == -1                            &&
       ((errno == EINTR                            &&
         UInterrupt::checkForEventSignalPending()) ||
        (errno == EAGAIN                           &&
         timeoutMS != -1                           &&
         waitForRead(fd, timeoutMS) == 1)))
      {
      goto loop;
      }

   U_RETURN(-1);
}

// param timeoutMS specified the timeout value, in milliseconds.
// A negative value indicates no timeout, i.e. an infinite wait

uint32_t UNotifier::write(int fd, const char* str, int count, int timeoutMS)
{
   U_TRACE(1, "UNotifier::write(%d,%.*S,%d,%d)", fd, count, str, count, timeoutMS)

   U_INTERNAL_ASSERT_DIFFERS(fd, -1)

   ssize_t value;
   int byte_written = 0;

   do {
      if (timeoutMS >= 0)
         {
         if (fd < 0) U_RETURN(-1);

         if (waitForWrite(fd, timeoutMS) <= 0) break;

         timeoutMS = -1; // in this way it is only for the first write...
         }

#  ifdef _MSWINDOWS_
      (void) U_SYSCALL(WriteFile, "%p,%p,%lu,%p,%p", (HANDLE)_get_osfhandle(fd), str + byte_written, count - byte_written, (DWORD*)&value, 0);
#  else
      value = U_SYSCALL(write, "%d,%S,%u", fd, str + byte_written, count - byte_written);
#  endif

      if (value ==  0) break;
      if (value == -1)
         {
         if (errno == EINTR &&
             UInterrupt::checkForEventSignalPending())
            {
            continue;
            }

         break;
         }

      U_INTERNAL_DUMP("BytesWritten(%d) = %#.*S", value, value, str + byte_written)

      byte_written += value;
      }
   while (byte_written < count);

   U_RETURN(byte_written);
}

// STREAM

#if defined(U_STDCPP_ENABLE) && defined(DEBUG)
const char* UNotifier::dump(bool reset) const
{
#ifdef USE_LIBEVENT
// nothing
#elif defined(HAVE_EPOLL_WAIT)
   *UObjectIO::os << "epollfd                     " << epollfd        << '\n';
#else
   *UObjectIO::os << "fd_set_max                  " << fd_set_max     << '\n'
                  << "fd_read_cnt                 " << fd_read_cnt    << '\n'
                  << "fd_write_cnt                " << fd_write_cnt   << '\n';
   *UObjectIO::os << "fd_set_read                 ";
   char buffer[70];
    UObjectIO::os->write(buffer, u__snprintf(buffer, sizeof(buffer), "%B", __FDS_BITS(&fd_set_read)[0]));
    UObjectIO::os->put('\n');
   *UObjectIO::os << "fd_set_write                ";
    UObjectIO::os->write(buffer, u__snprintf(buffer, sizeof(buffer), "%B", __FDS_BITS(&fd_set_write)[0]));
    UObjectIO::os->put('\n');
#endif
   *UObjectIO::os << "nfd_ready                   " << nfd_ready      << '\n'
                  << "min_connection              " << min_connection << '\n'
                  << "num_connection              " << num_connection << '\n'
                  << "max_connection              " << max_connection;

   if (reset)
      {
      UObjectIO::output();

      return UObjectIO::buffer_output;
      }

   return 0;
}
#endif
