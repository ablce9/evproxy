#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>

struct event_base *base;
struct sockaddr_in dst_sockaddr;
int dst_socklen;

const char *program = "evproxy";
static void eventcb(struct bufferevent *bev, short what, void *ctx);
static void writecb(struct bufferevent *bev, void *ctx);

static void
syntax()
{
  fprintf(stderr, "%s src-addr dst-addr\n", program);
  exit(1);
}

void
streamcb(struct bufferevent *bev, void *ctx)
{
  struct bufferevent *partner = ctx;
  struct evbuffer *src = bufferevent_get_input(bev), *dst = bufferevent_get_output(partner);
  size_t buf_size = evbuffer_get_length(src);

  if (!partner || !buf_size)
    {
      evbuffer_drain(src, buf_size);
      return;
    }

  if (evbuffer_add_buffer(dst, src) < 0)
    fprintf(stderr, "fatal: evbuffer_add_buffer\n");

  // Keep doing proxy until there is no data
  bufferevent_setcb(partner, streamcb, writecb, eventcb, bev);
  bufferevent_enable(partner, EV_READ|EV_WRITE);
}

void
eventcb(struct bufferevent *bev, short what, void *ctx)
{
  struct bufferevent *partner = ctx;

  if (what & (BEV_EVENT_EOF|BEV_EVENT_ERROR))
    {
      if (partner != NULL)
	{
	  /* Flush leftover */
	  streamcb(bev, ctx);

	  if (evbuffer_get_length(bufferevent_get_output(partner)))
	    bufferevent_disable(partner, EV_READ);

	  else
	    /* We have nothing left to say to the other
	     * side; close it! */
	    bufferevent_free(partner);
	}
      bufferevent_free(bev);
    }
}

static void
writecb(struct bufferevent *bev, void *ctx)
{
  struct bufferevent *partner = ctx;
  struct evbuffer *src = bufferevent_get_input(bev);
  size_t buf_size = evbuffer_get_length(src);

  if (buf_size)
    {
      bufferevent_disable(partner, EV_READ);
      bufferevent_setcb(bev, streamcb, writecb, eventcb, partner);
    }
}

static void
acceptcb(struct evconnlistener *listener, evutil_socket_t fd,
	 struct sockaddr *a, int slen, void *p)
{
  struct bufferevent *bev, *partner;

  bev = bufferevent_socket_new(base, fd,
			       BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
  partner = bufferevent_socket_new(base, -1,
				   BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
  if (bufferevent_socket_connect(partner, (struct sockaddr*)&dst_sockaddr, dst_socklen) < 0)
    {
      fprintf(stderr, "fatal: connect\n");
      bufferevent_free(bev);
      bufferevent_free(partner);
      exit(1);
    }

  printf("accepting client\n");
  bufferevent_setcb(bev, streamcb, writecb, eventcb, partner);
  bufferevent_enable(bev, EV_READ|EV_WRITE);
}

int
main(int cc, char **argv)
{
  struct sockaddr_in src_sockaddr;
  struct evconnlistener *listener;
  int i = 1, p, socklen;

  if (cc < 3)
    syntax();

  memset(&src_sockaddr, 0, sizeof(struct sockaddr_in));
  socklen = sizeof(src_sockaddr);
  if (evutil_parse_sockaddr_port(argv[i], (struct sockaddr*)&src_sockaddr, &socklen) < 0)
    {
      p = atoi(argv[i]);
      if (p < 1 || p > 65535)
	syntax();
      struct sockaddr_in *sin = &src_sockaddr;
      sin->sin_port = htons(p);
      sin->sin_addr.s_addr = htonl(0x7f000001);
      sin->sin_family = AF_INET;
      socklen = sizeof(struct sockaddr_in);
    }

  memset(&dst_sockaddr, 0, sizeof(struct sockaddr_in));
  dst_socklen = sizeof(dst_sockaddr);
  if (evutil_parse_sockaddr_port(argv[i+1], (struct sockaddr*)&dst_sockaddr, &dst_socklen) < 0)
    syntax();

  base = event_base_new();
  assert(base);
  listener = evconnlistener_new_bind(base, acceptcb, NULL,
				     LEV_OPT_CLOSE_ON_FREE|LEV_OPT_CLOSE_ON_EXEC|LEV_OPT_REUSEABLE,
				     -1, (struct sockaddr*)&src_sockaddr, socklen);
  event_base_dispatch(base);
  evconnlistener_free(listener);
  event_base_free(base);
  return 0;
}
