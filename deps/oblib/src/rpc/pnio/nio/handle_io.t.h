/* errors
1. 0: yield, file is writable, and there is remain data to send.
2. EAGAIN: wait wakeup, 3 cases:
   1. file is not writable, wait epoll to wakeup
   2. no remain data to send, wait poster to wakeup
   3. wait for memory or bandwidth...
3. Exception: should destroy sock.
*/
//int xxx_sk_do_flush(xxx_sk_t* s, int64_t* remain);

/* errors
1. 0: decode success
2. EAGAIN: wait wakeup, 2 cases:
   1. file is not readable to complete a msg.
   2. wait for memory or bandwidth...
3. Exception: should destroy sock.
 */
//int xxx_sk_do_decode(xxx_sk_t* s, xxx_msg_t** msg);
/* errors
1. 0: handle success
2. EAGAIN: wait wakeup, wait for memory or bandwidth.
3. Exception: should destroy sock.
 */
//int xxx_handle_msg(xxx_sk_t* s, xxx_msg_t* msg);

static int my_sk_flush(my_sk_t* s, int64_t time_limit) {
  int err = 0;
  int64_t remain = INT64_MAX;
  while(0 == err && remain > 0 && !is_epoll_handle_timeout(time_limit)) {
    if (0 != (err = my_sk_do_flush(s, &remain))) {
      if (EAGAIN != err) {
        rk_info("do_flush fail: %d", err);
      }
    }
  }
  return remain <= 0 && 0 == err? EAGAIN: err;
}

int my_sk_consume(my_sk_t* s, int64_t time_limit, int64_t* avail_bytes) {
  int err = 0;
  my_msg_t msg = (my_msg_t) { .sz = 0, .payload = NULL };
  pn_comm_t* pn = get_current_pnio();
  if (avail_bytes == NULL && skt(s, IN) && LOAD(&pn->pn_grp->rx_bw) != RATE_UNLIMITED) {
    // push socket to ratelimit list
    my_t* io = structof(s->fty, my_t, sf);
    rl_sock_push(&io->ep->rl_impl, (sock_t*)s);
    err = EAGAIN;
  }
  int64_t rbytes = 0;
  while(0 == err && !is_epoll_handle_timeout(time_limit)) {
    if (0 != (err = my_sk_do_decode(s, &msg, avail_bytes))) {
      if (EAGAIN != err) {
        rk_warn("do_decode fail: %d", err);
      }
    } else if (NULL == msg.payload) {
      // not read a complete package yet
    } else if (0 != (err = my_sk_handle_msg(s, &msg))) {
      rk_info("handle msg fail: %d", err);
    }
  }
  return err;
}

static int my_sk_handle_event_ready(my_sk_t* s) {
  int consume_ret = my_sk_consume(s, get_epoll_handle_time_limit(), NULL);
  int flush_ret = my_sk_flush(s, get_epoll_handle_time_limit());
  return EAGAIN == consume_ret? flush_ret: consume_ret;
}
