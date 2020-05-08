#include "mdlcuda.h"
#include <stdio.h>
#include <assert.h>
#include <algorithm>

namespace mdl {

#define CUDA_CHECK(f,a) {cudaError_t rc = (f)a; if (rc!=cudaSuccess) Abort(rc,#f,__FILE__,__LINE__);}
#define CUDA_RETURN(f,a) {cudaError_t rc = (f)a; if (rc!=cudaSuccess) return rc;}

void Abort(cudaError_t rc, const char *fname, const char *file, int line) {
    fprintf(stderr,"%s error %d in %s(%d)\n%s\n", fname, rc, file, line, cudaGetErrorString(rc));
    exit(1);
    }

/*****************************************************************************\
* CUDA : CUDA devices manager
\*****************************************************************************/

CUDA::CUDA() : nDevices(0) {}

// Create a set of device objects, each with "n" streams
void CUDA::initialize(int nStreamsPerDevice) {
    if (cudaGetDeviceCount(&nDevices) != cudaSuccess) nDevices = 0;
    for(auto iDevice=0; iDevice<nDevices; ++iDevice)
    	devices.emplace_back(iDevice,nStreamsPerDevice);
    }

// If we can start some work then do so.
void CUDA::initiate() {
    while (!empty()) { // A message is waiting. Find a stream if we can.
	assert(devices.size()>0); // This would be bad
	Device &device = devices.front();
	if (!device.empty()) {
	    cudaMessage &M = dequeue();
	    device.launch(M); // Launch message M on the given device.
	    // Now move this device to priority location in the devices list
	    // auto n = device.free(); // number of free streams: find one with fewer
	    // auto isless = [&](Device &i) {return i.free()<n;};
	    // auto iless = std::find_if(devices.begin(),devices.end(),isless);
	    // devices.splice(iless,devices,devices.begin());
	    }
	else break; // No free streams to launch work at this time
	}
    }

/*****************************************************************************\
* Device : Control for a single device
\*****************************************************************************/

Device::Device(int iDevice, int nStreams) : iDevice(iDevice), nStreams(nStreams) {
    for(auto i=0; i<nStreams; ++i) {
    	free_streams.enqueue(new Stream(this));
	}
    }

void Device::launch(cudaMessage &M) {
    if (free_streams.empty()) abort();
    auto &stream = free_streams.dequeue();
    auto stm = stream.getStream(); // the CUDA stream
    stream.message = &M; // Save the message (for kernel_finished)
    M.launch(stm,stream.pCudaBufIn,stream.pCudaBufOut); // message specific launch operation
    // Ask CUDA to notify us when the prior queued work has finished
    cudaLaunchHostFunc(stm,Device::kernel_finished,&stream);
    }

// Static "void *" version: recover the Stream object and call
void CUDART_CB Device::kernel_finished( void*  userData ) {
    auto stream = reinterpret_cast<Stream *>(userData);
    stream->device->kernel_finished(stream);
    }

// Here we move the Stream back to the free list and return the message
// to the requester. CAREFUL: this is called from a special CUDA thread.
void Device::kernel_finished( Stream *stream ) {
    stream->message->sendBack();
    stream->message = NULL;
    free_streams.enqueue(stream);
    }

/*****************************************************************************\
* Stream : a stream on a specific device
\*****************************************************************************/

Stream::Stream(class Device *device) : device(device), message(0) {
    CUDA_CHECK(cudaSetDevice,(device->iDevice)); // Stream is for this device
    CUDA_CHECK(cudaStreamCreate, (&stream));     // CUDA stream
    CUDA_CHECK(cudaMalloc,(&pCudaBufIn,  requestBufferSize));
    CUDA_CHECK(cudaMalloc,(&pCudaBufOut, resultsBufferSize));
    }

// Destroy the stream on the correct device.
Stream::~Stream() {
    CUDA_CHECK(cudaSetDevice,(device->iDevice));
    CUDA_CHECK(cudaFree,(pCudaBufIn));
    CUDA_CHECK(cudaFree,(pCudaBufOut));
    cudaStreamDestroy(stream);
    }

// Activate the appropriate device, and return the stream
cudaStream_t Stream::getStream() {
    CUDA_CHECK(cudaSetDevice,(device->iDevice));
    return stream;
    }

} // namespace mdl