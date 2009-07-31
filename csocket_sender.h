#ifndef csocket_sender_h_incl
#define csocket_sender_h_incl

class CSocketSender : public CMMThread {
  public:
    explicit CSocketSender(CSocket *csock_);
    
  protected:
    virtual void Run();
    virtual void Finish();
  private:
    CSocket *csock;
    
    void send_request(struct CMMSocketRequest req);
    void do_begin_irob(struct CMMSocketRequest req);
    void do_end_irob(struct CMMSocketRequest req);
    void do_irob_chunk(struct CMMSocketRequest req);

    void send_header(struct CMMSocketControlHdr hdr);
};

#endif
