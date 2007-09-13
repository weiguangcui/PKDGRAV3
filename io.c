#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <math.h>

#include "iohdf5.h"
#include "pst.h"
#include "io.h"

#define CHUNKSIZE (32*1024)
#define EPSILON (-1e20)

/* Create an HDF5 file for output */
hid_t ioCreate( const char *filename ) {
    hid_t fileID;

    /* Create the output file */
    fileID = H5Fcreate( filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT );
    H5assert(fileID);

    return fileID;
}

static void ioSave(IO io, const char *filename, double dTime,
		   double dEcosmo, double dTimeOld, double dUOld,
		   int bDouble )
{
    hid_t fileID;
    IOHDF5 iohdf5;
    IOHDF5V ioDen;
    IOHDF5V ioPot;
    uint_fast32_t i;

    /* Create the output file */
    fileID = ioCreate(filename);
    iohdf5 = ioHDF5Initialize( fileID, CHUNKSIZE, bDouble );
    ioDen  = ioHDFF5NewVector( iohdf5, "density",  IOHDF5_SINGLE );
    ioPot  = ioHDFF5NewVector( iohdf5, "potential",IOHDF5_SINGLE );

    ioHDF5WriteAttribute( iohdf5, "dTime",   H5T_NATIVE_DOUBLE, &dTime );
    ioHDF5WriteAttribute( iohdf5, "dEcosmo", H5T_NATIVE_DOUBLE, &dEcosmo );
    ioHDF5WriteAttribute( iohdf5, "dTimeOld",H5T_NATIVE_DOUBLE, &dTimeOld );
    ioHDF5WriteAttribute( iohdf5, "dUOld",   H5T_NATIVE_DOUBLE, &dUOld );

    for( i=0; i<io->N; i++ ) {
//	ioHDF5AddDark(iohdf5, io->iMinOrder+i,
//		   io->r[i].v, io->v[i].v,
//		   io->m[i], io->s[i], io->p[i] );
	ioHDF5AddDark(iohdf5, io->iMinOrder+i,
		   io->r[i].v, io->v[i].v,
		   0.0, 0.0, io->p[i] );
	ioHDF5AddVector( ioDen, io->iMinOrder+i, io->d[i] );
	ioHDF5AddVector( ioPot, io->iMinOrder+1, io->p[i] );
    }
    ioHDF5Finish(iohdf5);

    H5assert(H5Fflush(fileID,H5F_SCOPE_GLOBAL));
    H5assert(H5Fclose(fileID));
}

void ioInitialize(IO *pio,MDL mdl)
{
    IO io;
    io = (IO)malloc(sizeof(struct ioContext));
    mdlassert(mdl,io != NULL);
    io->mdl = mdl;

    io->N = 0;
    io->r = NULL;
    io->v = NULL;
//    io->m = NULL;
//    io->s = NULL;
    io->d = NULL;
    io->p = NULL;

    *pio = io;
}

void ioAddServices(IO io,MDL mdl)
{
    mdlAddService(mdl,IO_START_SAVE,io,
		  (void (*)(void *,void *,int,void *,int *)) ioStartSave,
		  sizeof(struct inStartSave),0);
    mdlAddService(mdl,IO_START_RECV,io,
		  (void (*)(void *,void *,int,void *,int *)) ioStartRecv,
		  sizeof(struct inStartRecv),0);
}

/*
**  This service is called from the peer root node (processor 0) and initiates
**  a save process.
*/
void ioStartSave(IO io,void *vin,int nIn,void *vout,int *pnOut)
{
    int id;
    struct inStartSave *save = vin;
    struct inStartRecv recv;
    uint_fast32_t iCount;

    mdlassert(io->mdl,sizeof(struct inStartSave)==nIn);
    mdlassert(io->mdl,mdlSelf(io->mdl)==0);

    mdlSetComm(io->mdl,0); /* Talk to our peers */
    recv.dTime   = save->dTime;
    recv.dEcosmo = save->dEcosmo;
    recv.dTimeOld= save->dTimeOld;
    recv.dUOld   = save->dUOld;
    strcpy(recv.achOutName,save->achOutName);
    recv.bCheckpoint = save->bCheckpoint;
    iCount = save->N / mdlIO(io->mdl);
    for( id=1; id<mdlIO(io->mdl); id++ ) {
	recv.iIndex = iCount * id;
	recv.nCount = iCount;
	if ( id+1 == mdlIO(io->mdl) )
	    recv.nCount = save->N - id*iCount;
	mdlReqService(io->mdl,id,IO_START_RECV,&recv,sizeof(recv));
    }

    recv.iIndex = 0;
    recv.nCount = iCount;
    ioStartRecv(io,&recv,sizeof(recv),NULL,0);

    for( id=1; id<mdlIO(io->mdl); id++ ) {
	mdlGetReply(io->mdl,id,NULL,NULL);
    }
    mdlSetComm(io->mdl,1);

}


static int ioUnpackIO(void *vctx, int nSize, void *vBuff)
{
    IO io = vctx;
    PIO *pio = vBuff;
    uint_fast32_t nIO = nSize / sizeof(PIO);
    uint_fast32_t i, d;

    mdlassert(io->mdl,nIO<=io->nExpected);

    for( i=0; i<nIO; i++ ) {
	uint64_t iOrder = pio[i].iOrder;
	int j = iOrder - io->iMinOrder;

	mdlassert(io->mdl,iOrder>=io->iMinOrder);
	mdlassert(io->mdl,iOrder<io->iMaxOrder);

	for( d=0; d<3; d++ ) {
	    io->r[j].v[d] = pio[i].r[d];
	    io->v[j].v[d] = pio[i].v[d];
	}
//	io->m[j] = pio[i].fMass;
//	io->s[j] = pio[i].fSoft;
	io->d[j] = pio[i].fDensity;
	io->p[j] = pio[i].fPot;

    }

    io->nExpected -= nIO;
    return io->nExpected;
}

/*
**  Here we actually wait for the data from the Work nodes
*/
void ioStartRecv(IO io,void *vin,int nIn,void *vout,int *pnOut)
{
    struct inStartRecv *recv = vin;
    char achOutName[256], *p;

    mdlassert(io->mdl,sizeof(struct inStartRecv)==nIn);
    io->nExpected = recv->nCount;

    if ( io->nExpected > io->N ) {
	if ( io->N ) {
	    free(io->p);
	    free(io->d);
//	    free(io->s);
//	    free(io->m);
	    free(io->v);
	    free(io->r);
	}
	io->N = io->nExpected;
	io->r = malloc(io->N*sizeof(ioV3));
	io->v = malloc(io->N*sizeof(ioV3));
	//io->m = malloc(io->N*sizeof(FLOAT));
	//io->s = malloc(io->N*sizeof(FLOAT));
	io->d = malloc(io->N*sizeof(float));
	io->p = malloc(io->N*sizeof(float));
    }

    io->iMinOrder = recv->iIndex;
    io->iMaxOrder = recv->iIndex + recv->nCount;

    mdlSetComm(io->mdl,1); /* Talk to the work process */
    mdlRecv(io->mdl,-1,ioUnpackIO,io);
    mdlSetComm(io->mdl,0);

    strcpy( achOutName, recv->achOutName );
    p = strstr( achOutName, "&I" );
    if ( p ) {
	int n = p - achOutName;
	sprintf( p, "%03d", mdlSelf(io->mdl) );
	strcat( p, recv->achOutName + n + 2 );
    }
    else {
	p = achOutName + strlen(achOutName);
	sprintf(p,".%03d", mdlSelf(io->mdl));
    }
    ioSave(io, achOutName, recv->dTime, recv->dEcosmo,
	   recv->dTimeOld, recv->dUOld,
	   recv->bCheckpoint ? IOHDF5_DOUBLE : IOHDF5_SINGLE );
}

void ioMakePNG(IO io,void *vin,int nIn,void *vout,int *pnOut)
{
    struct inMakePNG *make = vin;
    float *limg;
    int R, N, i;

    R = make->iResolution;
    N = R * R;

    limg = malloc( N*sizeof(float) );
    assert( limg != NULL );


    for( i=0; i<N; i++ ) limg[i] = EPSILON;



}
