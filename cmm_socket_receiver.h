#ifndef cmm_socket_receiver_h_incl
#define cmm_socket_receiver_h_incl

class CMMSocketReceiver {
  public:
    ssize_t read(void *buf, size_t len);
    CMMSocketReceiver(CMMSocketImpl *sk_);
  private:
    pthread_t tid;
    void RunReceiver(void);
    CMMSocketImpl *sk;
};

#endif
