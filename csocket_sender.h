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
    
    void send_header(struct CMMSocketControlHdr hdr);
};

#endif
