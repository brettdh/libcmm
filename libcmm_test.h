#ifndef libcmm_test_h_incl
#define libcmm_test_h_incl

#define handle_error(str) do { perror(str); exit(-1); } while (0)

#define LISTEN_PORT 4242

#define CHUNKSIZE 40
struct chunk {
    char data[CHUNKSIZE];
};

#endif
