/*
 * Copyright (C) 2010-2011 Irish Centre for High-End Computing (ICHEC)
 *
 * This file is distributed under the terms of the
 * GNU General Public License. See the file `License'
 * in the root directory of the present distribution,
 * or http://www.gnu.org/copyleft/gpl.txt .
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#if defined(__PHIGEMM_PARA)
#include <mpi.h>
#endif

#include <time.h>
#include <sys/types.h>
#include <sys/time.h>

#include "phigemm.h"
#include "phigemm_auxiliary.h"

#include "cuda.h"

#include "cublas_api.h"
#include <cuda_runtime.h>
#include "cublas_v2.h"


#if defined(__CUDA_GET_MEM_HACK)
#define __GPU_MEM_AMOUNT_HACK__ 2400000000
#endif

#define __SCALING_MEM_FACTOR__ 0.80

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(__PHIGEMM_PROFILE)
FILE *phiProfileFile;
#endif

static int is_phigemm_init = 0;
static int is_alloc_external = 0;

#if defined(__PHIGEMM_PROFILE)
const char base[] = "phigemm.profile";
#endif

extern int phiGemmNumDevices;

// ----

/* This routine computes the memory required to store the consideres matrices */
size_t memOccupancy(int is_splitA, float split, int m_in, int n_in, int k_in) {

	int m_split, n_split, tmp;

	if (is_splitA) {
		tmp = (m_in) * split;
//		if (m_in < 128)
			m_split = tmp;
//		else
//			m_split = floor(tmp/64.0)*64;

		return ( m_split*k_in/phiGemmNumDevices + k_in*n_in + m_split*n_in/phiGemmNumDevices );

	} else {
		tmp = (n_in) * split;
//		if (n_in < 128)
			n_split = tmp;
//		else
//			n_split = floor(tmp/64.0)*64;

		return( m_in*k_in + k_in*n_split/phiGemmNumDevices + m_in*n_split/phiGemmNumDevices );
	}
}

/* This routine computes the recursive split */
void bestFit(int is_splitA, float split, int m, int n, int k, int type_size, int *p1, int *p2) {

	size_t memsize_gpu = scratch_size[0] * phiGemmNumDevices;
	size_t mem_gpu = memOccupancy(is_splitA, split, m, n, k) * type_size;

	int tmp_m = m;
	int tmp_n = n;

	// This is ok, much better if we consider  padded matrices...
	const int step = 64;

#if 0
	/* repeat until the "new" matrices fit the GPU memory */
	while (mem_gpu > memsize_gpu) {
		if (is_splitA) {
			/* I can assume (to check!!!) that tmp_m is never too small ... */
			tmp_m = tmp_m - step;

			*p1 = tmp_m;
			*p2 = m - (*p1);

			/* A:(p1*split x k), B:(k x n), C: (p1*split x n)
			 * ---> do they fit the GPU memory?
			 */
			mem_gpu = memOccupancy(is_splitA, split, *p1, n, k) * type_size;
		} else {
			/* I can assume (to check!!!) that tmp_n is never too small ... */
			tmp_n = tmp_n - step;

			*p1 = tmp_n;
			*p2 = n - (*p1);

			/* A:(m x k), B:(k x p1*split), C: (m x p1*split)
			 * ---> do they fit the GPU memory?
			 */
			mem_gpu = memOccupancy(is_splitA, split, m, *p1, k) * type_size;
		}
#if defined(__PHIGEMM_DEBUG_4)
		fprintf( stdout,"[PHIGEMM_DEBUG][4] SPLITTING > p1: %d\tp2: %d\tsize (byte):%lu\n", *p1, *p2, mem_gpu); fflush(stdout);
#endif
	}
#else
	/* ORIGINAL */
	if (is_splitA) {
		*p1 = m/2;
		*p2 = m - (*p1);
	} else {
		*p1 = n/2;
		*p2 = n - (*p1);
	}
#endif
	return;
}

/* This routine returns the selected strategy for CPU-GPU splitting */
int cpuGPUheuristic(int m, int n, int k, char type, int enable_k) {

	double ratio_km = (double) k/m;
	double ratio_kn = (double) k/n;
	double threshold = SPLITK_FACTOR*2; // default 20

	double LOWER_LIMIT_NM = 64;
	double UPPER_LIMIT_NM = 256;
	double UPPER_LIMIT_K = 1025; // 1024 is a good dimension....

	/* 0: CPU-only
	 * 1: special-K
	 * 2: standard (split A or B)
	 */

	// Un-comment ONLY for debug/testing purposes...
	// return 2;

#if !defined(__PHIGEMM_DISABLE_SPECIALK)
	if ((type == 'd' || type == 'z') && enable_k) {

#if defined(__PHIGEMM_DEBUG_4)
	printf("[PHIGEMM_DEBUG][4] ratio_km=%f, ratio_kn=%f, threshold=%f\n", ratio_km, ratio_kn, threshold); fflush(stdout);
#endif

	// Matrices are small but not so small...
		if ( (n >= LOWER_LIMIT_NM) && (m >= LOWER_LIMIT_NM) ){
			// over the UPPER limit, they have to be rectangular...
			if ( ((n >= UPPER_LIMIT_K) && (m >= UPPER_LIMIT_K)) && ((ratio_km >= SPLITK_FACTOR) || (ratio_kn >= SPLITK_FACTOR)) )
				return 1;
			// below the UPPER limit, they have to be very rectangular...
			if ( ((n < UPPER_LIMIT_K) && (m < UPPER_LIMIT_K)) && ((ratio_km >= threshold) || (ratio_kn >= threshold)) )
				return 1;
		}
	}
#endif

	if ( (n < UPPER_LIMIT_NM) ||  (m < UPPER_LIMIT_NM) ) return 0;

	return 2;
}


// ----


int phiGemmIsInit()
{
	return is_phigemm_init;
}

double phigemm_cclock(void)
{
	struct timeval tv;
	struct timezone tz;
	double t;

	gettimeofday(&tv, &tz);

	t = (double)tv.tv_sec;
	t += ((double)tv.tv_usec)/1000000.0;

	return t;
}


int stringCmp( const void *a, const void *b)
{
	return strcmp((const char*)a,(const char*)b);
}


void phigemmSetSplitFactor(float *x) {
#if defined(__PHIGEMM_EXPLICIT_SPLITFACTOR)
	float tmp,tmp2;
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		/* 0:SGEMM, 1:DGEMM, 2: CGEMM, 3:ZGEMM */
		tmp =  (100.0f * x[i])/( 1.0f - x[i]);
		tmp2 = 100.0f;
		phiGemmSplitFactor[i] = tmp / (tmp + tmp2);
	}
#endif
	return;
}


float phigemmGetSplitFactor(int selection) {
#if defined(__PHIGEMM_EXPLICIT_SPLITFACTOR)
	return phiGemmSplitFactor[selection];
#else
	return phiGemmPrevSplitFactor[selection];
#endif
}


void estmSplitFactor(const char* optype, char transa, char transb)
{
	float envar_split;
	char *value = NULL;

#if !defined(__PHIGEMM_HACK_CPUONLY)
	/* split factor may vary between S/D/Z GEMMs */

	/* SGEMM */
	value = getenv("PHI_SGEMM_SPLIT");
	if (value != NULL)
	{
		envar_split = atof(value);
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] SGEMM split factor from environment variable: %f \n", envar_split);
#endif
	} else {
		/* Default split if no env variables are specified */
		envar_split = 0.85;
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] SGEMM default split factor: %f \n", envar_split);
#endif
	}
	phiGemmSplitFactor[0] = envar_split;
	phiGemmPrevSplitFactor[0] = envar_split;
	phiGemmLowerPositiveSplitFactor[0] = 0.995 ;

	/* DGEMM */
	value = getenv("PHI_DGEMM_SPLIT");
	if (value != NULL)
	{
		envar_split = atof(value);
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] DGEMM split factor from environment variable: %f \n", envar_split);
#endif
	} else {
		/* Default split if no env variables are specified */
		envar_split = 0.875;
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] DGEMM default split factor: %f \n", envar_split);
#endif
	}
	phiGemmSplitFactor[1] = envar_split;
	phiGemmPrevSplitFactor[1] = envar_split;
	phiGemmLowerPositiveSplitFactor[1] = 0.995 ;

	/* CGEMM */
	value = getenv("PHI_CGEMM_SPLIT");
	if (value != NULL)
	{
		envar_split = atof(value);
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] CGEMM split factor from environment variable: %f \n", envar_split);
#endif
	} else {

		/* Default split if no env variables are specified */
		envar_split = 0.9;
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] CGEMM  default split factor: %f \n", envar_split);
#endif
	}
	phiGemmSplitFactor[2] = envar_split;
	phiGemmPrevSplitFactor[2] = envar_split;
	phiGemmLowerPositiveSplitFactor[2] = 0.995 ;

	/* ZGEMM */
	value = getenv("PHI_ZGEMM_SPLIT");
	if (value != NULL)
	{
		envar_split = atof(value);
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] ZGEMM split factor from environment variable: %f \n", envar_split);
#endif
	} else {

		/* Default split if no env variables are specified */
		envar_split = 0.925;
#if defined(__PHIGEMM_DEBUG)
		printf ("[PHIGEMM_DEBUG] ZGEMM  default split factor: %f \n", envar_split);
#endif
	}
	phiGemmSplitFactor[3] = envar_split;
	phiGemmPrevSplitFactor[3] = envar_split;
	phiGemmLowerPositiveSplitFactor[3] = 0.995 ;

#endif

	/* This is to avoid not-defined OMP_NUM_THREADS in the environment.
	 * Default threads num = 1 */
	value = getenv("OMP_NUM_THREADS");
	if (value != NULL)
	{
		phiGemmCPUThreads = atoi(value);
	} else {

		/* Default threads num = 1 */
		phiGemmCPUThreads = 1;
	}
#if defined(__PHIGEMM_DEBUG)
	printf ("[PHIGEMM_DEBUG] phiGemmCPUThreads: %d \n", phiGemmCPUThreads);
#endif

}


void phiGemmInit( int nGPU, phiGemmMemDevPtr* dev_ptr, phiGemmMemSizes* dev_memsize, int * deviceToBond )
{

        unsigned int i;

	/* Skip all the initialization: phiGEMM becomes a simple interface to CPU GEMM so it is possible
	 * to capture all the GEMM call and profile them */
#if !defined(__PHIGEMM_HACK_CPUONLY)

	struct cudaDeviceProp deviceProp;
	int deviceCount;

	if ( is_phigemm_init == 1 )
		return;

	is_alloc_external = 1;

	cudaGetDeviceCount(&deviceCount);

	if (deviceCount == 0) {
		printf("*** phiGEMM *** ERROR *** no CUDA-capable devices were found on node.\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	if (nGPU > deviceCount) {
		printf("*** phiGEMM *** ERROR *** Requested %d devices, found on the node only %d. Initialization fails!\n", nGPU, deviceCount);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	phiGemmNumDevices = nGPU;

#if defined(__PHIGEMM_DEBUG)
	printf("[PHIGEMM_DEBUG] %d GPUs detected.\n", phiGemmNumDevices);
	fflush(stdout);
#endif

	/* find the split factor */



#if defined(__PHIGEMM_DEBUG)
	printf("[PHIGEMM_DEBUG] The (initial) split factors are: %g %g %g %g\n", phiGemmSplitFactor[0], phiGemmSplitFactor[1], phiGemmSplitFactor[2], phiGemmSplitFactor[3]);
	fflush(stdout);
#endif

	/* Init GPU data structures for managing multiGPU */
	for( i = 0; i < phiGemmNumDevices * NSTREAMS; i++ )
	{
		dev_scratch[ i ] = NULL;
		scratch_size[ i ] = 0;
		phiHandles[ i ] = NULL;
		phiStreams[ i ] = NULL;
	}

	for (i = 0; i < phiGemmNumDevices * NSTREAMS; i++) {

		/* Assign devices to processes
		 * note: one process may have assigned more than one device */
		deviceIds[i] = deviceToBond[i % phiGemmNumDevices];

		scratch_size[ i ] = ( *dev_memsize )[ i % phiGemmNumDevices ] / NSTREAMS;

		/* SAFE pointer operation! Remember that void pointers cannot be
		 * directly dereferenced because 'void' is NOT a real type! */
		size_t offset = ( i / phiGemmNumDevices ) * scratch_size[ i ];
		char * tmp_ptr = (char*) ( ( *dev_ptr )[ i % phiGemmNumDevices ] );
		dev_scratch[ i ] = (void*) (tmp_ptr + offset) ;


#if defined(__PHIGEMM_DEBUG)
		printf("[PHIGEMM_DEBUG] %lu Bytes of memory is allocated externally on GPU %d\n", (unsigned long)scratch_size[i], deviceIds[i]);
		fflush(stdout);
#endif
	}

	cudaError_t err;

	for (i = 0; i < phiGemmNumDevices * NSTREAMS; i++) {

		/* Attempt to establish a runtime API context */
		if ( cudaSetDevice( deviceIds[i % phiGemmNumDevices]) != cudaSuccess) {
			printf("*** phiGEMM *** ERROR *** cudaSetDevice(%d) failed!\n",i );
			fflush(stdout);
			exit( EXIT_FAILURE );
		}

		/* Attempt to initialize CUBLAS */
		if ( cublasCreate( &phiHandles[ i ] ) != CUBLAS_STATUS_SUCCESS ) {
			printf("*** phiGEMM *** ERROR *** cublasInit() for device %d failed!\n",i);
			fflush(stdout);
			exit( EXIT_FAILURE );
		}

		if( cudaStreamCreate( &phiStreams[ i ] ) != CUBLAS_STATUS_SUCCESS ) {
			printf("*** phiGEMM *** ERROR *** creating stream %d for device %d failed!\n", i, i % phiGemmNumDevices);
			fflush(stdout);
			exit( EXIT_FAILURE );
		}
		cublasSetStream( phiHandles[ i ], phiStreams[ i ] );
	}

	/* set the initialization flag */
	is_phigemm_init = 1;
#endif

	/* Now there is only one generic split factor. Parameters are temporary ignored... */
	estmSplitFactor("xxx", 'n', 'n');

#if defined(__PHIGEMM_PROFILE)

	char *value = NULL;
	char finalFileName [ FILENAME_MAX ];

	value = getenv("PHIGEMM_PROFILE_PREFIX");

#if defined(__PHIGEMM_PARA)

	MPI_Comm_rank(MPI_COMM_WORLD, &i);
	if (value != NULL)
		sprintf(finalFileName, "%s.%d.%s.csv", base, i, value);
	else
		sprintf(finalFileName, "%s.%d.csv", base, i);
#else
	if (value != NULL)
		sprintf(finalFileName, "%s.%s.csv", base, value);
	else
		sprintf(finalFileName, "%s.csv", base);
#endif

	phiProfileFile = fopen (finalFileName, "a");

#endif
}

void phiGemmShutdown()
{
	/* Skip all the initialization: phiGEMM becomes a simple interface to CPU GEMM so it is possible
	 * to capture all the GEMM call and profile them */
#if !defined(__PHIGEMM_HACK_CPUONLY)

	int i;

#if defined(__PHIGEMM_DEBUG)
	printf("[PHIGEMM_DEBUG] *** shutdown *** is_phigemm_init:%d, is_alloc_external:%d, devices: %d\n",is_phigemm_init, is_alloc_external, phiGemmNumDevices);
	fflush(stdout);
#endif

	if ( !is_phigemm_init )
		return;

	for (i = 0; i < phiGemmNumDevices ; i++) {

		/* Attempt to establish a runtime API context */
		if ( cudaSetDevice( deviceIds[i % phiGemmNumDevices] ) != cudaSuccess) {
			printf("*** phiGEMM: *** ERROR *** cudaSetDevice(%d) failed!\n",i);
			exit(EXIT_FAILURE);
		}

		cudaStreamDestroy( phiStreams[ i ] );
		cublasDestroy( phiHandles[ i ]);
	}

	for ( i = 0; i < phiGemmNumDevices; i++ ){

		if ( !is_alloc_external )
			cudaFree(dev_scratch[i]);
	}

	is_phigemm_init = 0;
#endif

#if defined(__PHIGEMM_PROFILE)
	fclose (phiProfileFile);
#endif
}


void selfPhigemmInit(){

	/* *** This routine is experimental *** */

        int i;

	/* Skip all the initialization: phiGEMM becomes a simple interface to CPU GEMM so it is possible
	 * to capture all the GEMM call and profile them */
#if !defined(__PHIGEMM_HACK_CPUONLY)

	int lNumDevicesThisNode, deviceCount, ierr ;
	int ngpus_detected, ngpus_used, ngpus_per_process;

	size_t free, total;

	struct cudaDeviceProp deviceProp;

#if defined(__PHIGEMM_DEBUG)
	printf("[PHIGEMM_DEBUG] *** Self-Init ***\n");
	fflush(stdout);
#endif

#if defined(__PHIGEMM_PARA)

	int lRank, lSize, sDeviceBoundTo, tmp;
	char lNodeName[MPI_MAX_PROCESSOR_NAME];
	int lNodeNameLength, sIsCudaCapable, lNumRanksThisNode;
	int lRankThisNode = 0, lSizeThisNode = 0;
	char *lNodeNameRbuf;
	int *lRanksThisNode;

	MPI_Group lWorldGroup;
	MPI_Group lThisNodeGroup;
	MPI_Comm  lThisNodeComm;

	MPI_Comm_rank(MPI_COMM_WORLD, &lRank);
	MPI_Comm_size(MPI_COMM_WORLD, &lSize);

	MPI_Get_processor_name(lNodeName, &lNodeNameLength);

	lNodeNameRbuf = (char*) malloc(lSize * MPI_MAX_PROCESSOR_NAME * sizeof(char));

	MPI_Allgather(lNodeName, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, lNodeNameRbuf, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);

	// lRanksThisNode is a list of the global ranks running on this node
	lRanksThisNode = (int*) malloc(lSize * sizeof(int));

	for(i=0; i<lSize; i++)
	{
		if(strncmp(lNodeName, (lNodeNameRbuf + i * MPI_MAX_PROCESSOR_NAME), MPI_MAX_PROCESSOR_NAME) == 0)
		{
			lRanksThisNode[lNumRanksThisNode] = i;
			lNumRanksThisNode++;
		}
	}

	/* Create a communicator consisting of the ranks running on this node. */
	MPI_Comm_group(MPI_COMM_WORLD, &lWorldGroup);
	MPI_Group_incl(lWorldGroup, lNumRanksThisNode, lRanksThisNode, &lThisNodeGroup);
	MPI_Comm_create(MPI_COMM_WORLD, lThisNodeGroup, &lThisNodeComm);
	MPI_Comm_rank(lThisNodeComm, &lRankThisNode);
	MPI_Comm_size(lThisNodeComm, &lSizeThisNode);

	/* Attach all MPI processes on this node to the available GPUs in round-robin fashion */
	cudaGetDeviceCount(&lNumDevicesThisNode);

	if (lNumDevicesThisNode == 0 && lRankThisNode == 0)
	{
		printf("***ERROR: no CUDA-capable devices were found on node %s.\n", lNodeName);
		MPI_Abort( MPI_COMM_WORLD, EXIT_FAILURE );
	}

	ngpus_detected = lNumDevicesThisNode;

	if ( (lSizeThisNode % lNumDevicesThisNode ) != 0  )
	{
		printf("***WARNING: unbalanced configuration (%d MPI per node, %d GPUs per node)\n", lSizeThisNode, lNumDevicesThisNode);
		fflush(stdout);
	}

	if (ngpus_detected <= lSizeThisNode ){
		/* if GPUs are less then (or equal of) the number of  MPI processes on a single node,
		 * then PWscf uses all the GPU and one single GPU is assigned to one or multiple MPI processes with overlapping. */
		ngpus_used = ngpus_detected;
		ngpus_per_process = 1;
	} else {
		/* multi-GPU in parallel calculations is allowed ONLY if CUDA >= 4.0 */
#if defined(__CUDA_3)
		ngpus_used = ngpus_detected;
		ngpus_per_process = 1;
#else
		/* if GPUs are more than the MPI processes on a single node,
		 * then PWscf uses all the GPU and one or more GPUs are assigned to every single MPI processes without overlapping.
		 * *** NOT IMPLEMENTED YET *** */
		ngpus_used = ngpus_detected;
		ngpus_per_process = 1;
#endif
	}

	for (i = 0; i < ngpus_per_process; i++) {

		deviceIds[i] = lRankThisNode % lNumDevicesThisNode;

		/* query the real free memory, taking into account the "stack" */
		if ( cudaSetDevice(deviceIds[i]) != cudaSuccess) {
			printf("*** ERROR *** cudaSetDevice(%d) failed!", deviceIds[i] );
			exit(EXIT_FAILURE);
		}

		scratch_size[i] = (size_t) 0;

		ierr = cudaMalloc ( (void**) &(dev_scratch[i]), scratch_size[i] );
		if ( ierr != cudaSuccess) {
			fprintf( stderr, "\nError in (first zero) memory allocation [GPU %d] , program will be terminated!!! Bye...\n\n", deviceIds[i]);
			exit(EXIT_FAILURE);
		}
	}

	/* is this barrier smart? I guess yes... */
	MPI_Barrier(lThisNodeComm);

	for (i = 0; i < ngpus_per_process; i++) {
		cudaMemGetInfo((size_t*)&free,(size_t*)&total);

		// see cuda_env.h for a description of the hack
#if defined(__CUDA_GET_MEM_HACK)
		free = (size_t)  __GPU_MEM_AMOUNT_HACK__;
#else
		cudaMemGetInfo((size_t*)&free,(size_t*)&total);

#if defined(__PHIGEMM_DEBUG)
		printf("[PHIGEMM_DEBUG - GPU %d - rank: %d (internal rank:%d) ] before: %lu (total: %lu)\n", deviceIds[i], lRank, lRankThisNode, (unsigned long)free, (unsigned long)total); fflush(stdout);
#endif
#endif

		int procs_per_gpu = (lSizeThisNode < lNumDevicesThisNode) ? lSizeThisNode : lSizeThisNode / lNumDevicesThisNode;
		scratch_size[i] = (size_t) (free * __SCALING_MEM_FACTOR__ / procs_per_gpu);

		ierr = cudaMalloc ( (void**) &(dev_scratch[i]), (size_t) scratch_size[i] );
		if ( ierr != cudaSuccess) {
			fprintf( stderr, "\nError in memory allocation [GPU %d] , program will be terminated (%d)!!! Bye...\n\n", deviceIds[i], ierr );
			exit(EXIT_FAILURE);
		}

#if defined(__PHIGEMM_DEBUG)
		cudaMemGetInfo((size_t*)&free,(size_t*)&total);
		printf("[PHIGEMM_DEBUG - GPU %d - rank: %d (internal rank:%d)] after: %lu (total: %lu)\n", deviceIds[i], lRank, lRankThisNode, (unsigned long)free, (unsigned long)total); fflush(stdout);
#endif
	}


	is_alloc_external = 0; //quite important that this is 0!

	/* set the initialization flag */
	is_phigemm_init = 1;
#else

	cudaGetDeviceCount(&lNumDevicesThisNode);

	if (lNumDevicesThisNode == 0)
	{
		fprintf( stderr,"***ERROR*** no CUDA-capable devices were found on the machine.\n");
		exit(EXIT_FAILURE);
	}

#if defined(__PHIGEMM_DEBUG)
	printf("[PHIGEMM_DEBUG] %d GPUs detected.\n", lNumDevicesThisNode);
	fflush(stdout);
#endif

	ngpus_detected = lNumDevicesThisNode;

	/* multi-GPU in serial calculations is allowed ONLY if CUDA >= 4.0 */
#if defined(__PHIGEMM_MULTI_GPU) && !defined(__CUDA_3)
	ngpus_used = ngpus_per_process = lNumDevicesThisNode;
#else
	ngpus_used = ngpus_per_process = 1;
#endif

#if defined(__PHIGEMM_DEBUG)
	printf("[PHIGEMM_DEBUG] %d GPUs used.\n", ngpus_per_process);
	fflush(stdout);
#endif

	/* Init GPU data structures for managing multiGPU */
	for( i = 0; i < ngpus_per_process; i++ )
	{
		dev_scratch[ i ] = NULL;
		scratch_size[ i ] = 0;
	}

	for (i = 0; i < ngpus_per_process; i++) {

		/* Bond devices. NOTE: qe_gpu_bonded[0] is ALWAYS the main device for non multi-GPU kernels.*/
		deviceIds[i] = i;

		/* query the real free memory, taking into account the "stack" */
		if ( cudaSetDevice(deviceIds[i]) != cudaSuccess) {
			printf("*** ERROR *** cudaSetDevice(%d) failed!", deviceIds[i] );
			exit(EXIT_FAILURE);
		}

		scratch_size[i] = (size_t) 0;

		ierr = cudaMalloc ( (void**) &(dev_scratch[i]), scratch_size[i] );
		if ( ierr != cudaSuccess) {
			fprintf( stderr, "\nError in (first zero) memory allocation [GPU %d] , program will be terminated!!! Bye...\n\n", deviceIds[i]);
			exit(EXIT_FAILURE);
		}

		cudaMemGetInfo((size_t*)&free,(size_t*)&total);

		// see cuda_env.h for a description of the hack
#if defined(__CUDA_GET_MEM_HACK)
		free = (size_t)  __GPU_MEM_AMOUNT_HACK__;
#else
		cudaMemGetInfo((size_t*)&free,(size_t*)&total);

#if defined(__PHIGEMM_DEBUG)
		printf("[PHIGEMM_DEBUG - GPU %d] before: %lu (total: %lu)\n", deviceIds[i], (unsigned long)free, (unsigned long)total); fflush(stdout);
#endif
#endif
		scratch_size[i] = (size_t) (free * __SCALING_MEM_FACTOR__);

		ierr = cudaMalloc ( (void**) &(dev_scratch[i]), (size_t) scratch_size[i] );
		if ( ierr != cudaSuccess) {
			fprintf( stderr, "\nError in memory allocation [GPU %d] , program will be terminated (%d)!!! Bye...\n\n", deviceIds[i], ierr );
			exit(EXIT_FAILURE);
		}

#if defined(__PHIGEMM_DEBUG)
		cudaMemGetInfo((size_t*)&free,(size_t*)&total);
		printf("[PHIGEMM_DEBUG - GPU %d] after: %lu (total: %lu)\n", deviceIds[i], (unsigned long)free, (unsigned long)total); fflush(stdout);
#endif
	}

#endif

	if ( cudaSetDevice(deviceIds[0]) != cudaSuccess) {
		printf("*** ERROR *** cudaSetDevice(0) failed!");
		exit(EXIT_FAILURE);
	}

	phiGemmNumDevices = ngpus_per_process;

	is_alloc_external = 0; //quite important that this is 0!

	/* find the split factor */
#if defined(__PHIGEMM_EXPLICIT_SPLITFACTOR)

#if defined(__PHIGEMM_DEBUG)
	printf("*** phiGEMM *** The (explicit) split factors are: %g %g %g %g\n", phiGemmSplitFactor[0], phiGemmSplitFactor[1], phiGemmSplitFactor[2], phiGemmSplitFactor[3]);
	fflush(stdout);
#endif

#else

	/* Now there is only one generic split factor. Parameters are temporary ignored... */
	estmSplitFactor("xxx", 'n', 'n');

#if defined(__PHIGEMM_DEBUG)
	printf("*** phiGEMM *** The (initial) split factors are: %g %g %g %g\n", phiGemmSplitFactor[0], phiGemmSplitFactor[1], phiGemmSplitFactor[2], phiGemmSplitFactor[3]);
	fflush(stdout);
#endif

#endif

	/* Init GPU data structures for managing multiGPU */
	for( i = 0; i < phiGemmNumDevices ; i++ )
	{
		phiHandles[ i ] = NULL;
		phiStreams[ i ] = NULL;
	}

	cudaError_t err;

	for (i = 0; i < phiGemmNumDevices ; i++) {

		/* Attempt to establish a runtime API context */
		if ( cudaSetDevice( deviceIds[i]) != cudaSuccess) {
			printf("*** phiGEMM *** ERROR *** cudaSetDevice(%d) failed!\n",i );
			fflush(stdout);
			exit( EXIT_FAILURE );
		}

		/* Attempt to initialize CUBLAS */
		if ( cublasCreate( &phiHandles[ i ] ) != CUBLAS_STATUS_SUCCESS ) {
			printf("*** phiGEMM *** ERROR *** cublasInit() for device %d failed!\n",i);
			fflush(stdout);
			exit( EXIT_FAILURE );
		}

		if( cudaStreamCreate( &phiStreams[ i ] ) != CUBLAS_STATUS_SUCCESS ) {
			printf("*** phiGEMM *** ERROR *** creating stream %d for device %d failed!\n", i, i);
			fflush(stdout);
			exit( EXIT_FAILURE );
		}
		cublasSetStream( phiHandles[ i ], phiStreams[ i ] );
	}

	/* set the initialization flag */
	is_phigemm_init = 1;
#endif

#if defined(__PHIGEMM_PROFILE)

	char *value = NULL;
	char finalFileName [ FILENAME_MAX ];

	value = getenv("PHIGEMM_PROFILE_PREFIX");

#if defined(__PHIGEMM_PARA)

	MPI_Comm_rank(MPI_COMM_WORLD, &i);
	if (value != NULL)
		sprintf(finalFileName, "%s.%d.%s.csv", base, i, value);
	else
		sprintf(finalFileName, "%s.%d.csv", base, i);
#else
	if (value != NULL)
		sprintf(finalFileName, "%s.%s.csv", base, value);
	else
		sprintf(finalFileName, "%s.csv", base);
#endif

	phiProfileFile = fopen (finalFileName, "a");

#endif
}

void phiGemmSetAvaiableScratchSpace(int gpu_id, size_t new_dev_memsize) {
	scratch_size[ deviceIds[gpu_id] ] = (size_t) new_dev_memsize;

#if defined(__PHIGEMM_DEBUG)
    printf("[PHIGEMM_DEBUG] %lu Bytes of GPU memory available %d\n", (unsigned long)scratch_size[gpu_id], deviceIds[gpu_id]);
   fflush(stdout);
#endif
}


void phigemminit_(int nGPU, phiGemmMemDevPtr* ptr, phiGemmMemSizes* dev_memsize, int * deviceToBond ){ phiGemmInit( nGPU, ptr, dev_memsize, deviceToBond); }

void phigemmshutdown_(){ phiGemmShutdown(); }

int phigemmisinit_(){return phiGemmIsInit();}

void phigemmsetsplitfactor_(float *x) { phigemmSetSplitFactor(x); }

void selfphigemminit_(){return selfPhigemmInit(); }

void phiremmsetavaiablescratchspace_(int gpu_id, size_t new_dev_memsize) { phiGemmSetAvaiableScratchSpace(gpu_id, new_dev_memsize); }

#ifdef __cplusplus
}
#endif
