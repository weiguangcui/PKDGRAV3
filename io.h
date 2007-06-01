#ifndef IO_HINCLUDED
#define IO_HINCLUDED
#include "iohdf5.h"
#include "mdl.h"

typedef struct ioContext {
    MDL mdl;
    int nExpected;
    int nReceived;
    double dTime;

    int N;

    ioV3 *r;
    ioV3 *v;
    FLOAT *m;

} * IO;

void ioInitialize(IO *,MDL);
void ioAddServices(IO io,MDL mdl);

enum io_services {
    IO_SRV_STOP,
    IO_START_SAVE,
    IO_START_RECV,
    IO_RECV_DATA,
};

/* IO_START_SAVE */
struct inStartSave {
    double dTime;
#ifdef IO_SPLIT
    int nCount[MDL_MAX_IO_PROCS];
#endif
    };
void ioStartSave(IO,void *,int,void *,int *);

/* IO_START_RECV */
struct inStartRecv {
    double dTime;
#ifdef IO_SPLIT
    int nCount;
#endif
    };
void ioStartRecv(IO,void *,int,void *,int *);

/* IO_RECV_DATA */
struct inRecvData {
    };
void ioStartRecv(IO,void *,int,void *,int *);




#endif
