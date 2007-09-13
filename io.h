#ifndef IO_HINCLUDED
#define IO_HINCLUDED
#include "iohdf5.h"
#include "mdl.h"

typedef struct ioContext {
    MDL mdl;
    double dTime;

    uint_fast64_t iMinOrder;
    uint_fast64_t iMaxOrder;

    uint_fast32_t N;           /* Total allocated on this processor */
    uint_fast32_t nExpected;   /* Number left to be received */

    ioV3 *r;       /* Position */
    ioV3 *v;       /* Velocity */
    //FLOAT *m;      /* Mass */
    //FLOAT *s;      /* Softening */
    float *d;      /* Density */
    float *p;      /* Potential */

} * IO;

void ioInitialize(IO *,MDL);
void ioAddServices(IO io,MDL mdl);

enum io_services {
    IO_SRV_STOP,
    IO_START_SAVE,
    IO_START_RECV,
    IO_RECV_DATA,
    IO_MAKE_PNG
};

/* IO_START_SAVE */
struct inStartSave {
    double dTime;
    double dEcosmo;
    double dTimeOld;
    double dUOld;
    uint_fast64_t N;
    int    bCheckpoint;
    char achOutName[PST_FILENAME_SIZE];
    };
void ioStartSave(IO,void *,int,void *,int *);

/* IO_START_RECV */
struct inStartRecv {
    double dTime;
    double dEcosmo;
    double dTimeOld;
    double dUOld;
    uint_fast64_t iIndex;
    uint_fast32_t nCount;
    int    bCheckpoint;
    char achOutName[PST_FILENAME_SIZE];
    };
void ioStartRecv(IO,void *,int,void *,int *);

/* IO_MAKE_PNG */
struct inMakePNG {
    uint_fast32_t iResolution;  /* Image resolution RxR */
    };
void ioMakePNG(IO,void *,int,void *,int *);


#endif
