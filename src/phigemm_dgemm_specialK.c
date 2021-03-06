/*
 * Copyright (C) 2011-2012 Quantum ESPRESSO Foundation
 * Copyright (C) 2010-2011 Irish Centre for High-End Computing (ICHEC)
 *
 * This file is distributed under the terms of the
 * GNU General Public License. See the file `License'
 * in the root directory of the present distribution,
 * or http://www.gnu.org/copyleft/gpl.txt .
 *
 */


#include "phigemm.h"
#include "phigemm_auxiliary.h"

#if defined(__INTEL) && defined(__MKL) && defined(__OMP) && defined (__EXPERIMENTAL)
#include "mkl.h"
#endif

#define PRECISION_D
#if defined(PRECISION_D) || defined(PRECISION_S)
#define PHIGEMM_FLOPS(m, n, k) (      GEMM_MUL(m, n, k) +      GEMM_ADD(m, n, k))
#else
#define PHIGEMM_FLOPS(m, n, k) (  6 * GEMM_MUL(m, n, k) +  2 * GEMM_ADD(m, n, k))
#endif

#define cublasGemm cublasDgemm
#define gemm_mkl dgemm_
#define PHIGEMM_M phidgemm_specialK
#define PHIGEMM_GEMM_MF phigemm_specialK

#define MAX_N_STREAM 2

#if defined(__PHIGEMM_PROFILE)
void PHIGEMM_GEMM_MF(const char *transa, const char *transb, const int *m,
		const int *n, const int *k, const double *alpha,
		const double *A, const int *lda, const double *B,
		const int *ldb, const double *beta, double *C, const int *ldc,
		int is_splitA, float split,
		const char *file, const char * line);
#else
void PHIGEMM_GEMM_MF(const char *transa, const char *transb, const int *m,
		const int *n, const int *k, const double *alpha,
		const double *A, const int *lda, const double *B,
		const int *ldb, const double *beta, double *C, const int *ldc,
		int is_splitA, float split);
#endif

#if defined(__PHIGEMM_PROFILE)
void PHIGEMM_M (const char *transa, const char *transb, const int *m,
		const int *n, const int *k, const double *alpha,
		const double *A, const int *lda, const double *B,
		const int *ldb, const double *beta, double *C, const int *ldc,
		const char *file, const char * line)
#else
void PHIGEMM_M (const char *transa, const char *transb, const int *m,
		const int *n, const int *k, const double *alpha,
		const double *A, const int *lda, const double *B,
		const int *ldb, const double *beta, double *C, const int *ldc)
#endif
{
	// The method is empty if defined(__PHIGEMM_CPUONLY) *BUT* it is never called by phgemm_dgemm
#if !defined(__PHIGEMM_CPUONLY)

	double * C_buf[MAX_N_STREAM];
	double *devPtrA[MAX_N_STREAM], *devPtrB[MAX_N_STREAM], *devPtrC[MAX_N_STREAM];
	int iDev = 0, i = 0, count = 0, stream = 0;
	int offsetA = 0, offsetB = 0, offsetC = 0;
	int is_transa = 0, is_transb = 0;
	int gpu_lda = 0, gpu_ldb = 0;
	cublasStatus_t status;
	cudaError_t cudaErr;
	cudaStream_t streamPtr[MAX_N_STREAM];
	cublasOperation_t cu_transa, cu_transb;

	int loop_times = 0;

	int size = (* m) * (* n), inc = 1;
	double DA = 1.0;
	double gpu_beta = 0.0;
	int last_split = 0, local_split = myPhiGemmTng.SPLITK_DGEMM, splitted_size;
	size_t mem_buffer = 0L, memsize_gpu = myPhiGemmHdl.smem[iDev];

#if defined(__PHIGEMM_DEBUG)
	double start_axpy, start_gemm_total, stop_axpy, stop_gemm_total;
	double time_axpy = 0;

	start_gemm_total = phigemm_cclock();
#endif

	do{

		loop_times = (* k) / local_split;
		if( (* k) % local_split != 0){
			last_split = local_split + ( (* k) % local_split );
		}

		mem_buffer = ( (*m ) * (last_split != 0 ? last_split : local_split) + (* n) * (last_split != 0 ? last_split : local_split) + (* n) * (* m) ) * 2 * sizeof(double) ;

	}while( (mem_buffer > memsize_gpu) && (local_split/=2) );

	if ( (*transa != 'n') && (*transa != 'N') )	is_transa = 1;
	if ( (*transb != 'n') && (*transb != 'N') ) is_transb = 1;
	cu_transa = ((*transa == 'c')||(*transa == 'C')) ? CUBLAS_OP_C : CUBLAS_OP_N;
	cu_transa = ((*transa == 't')||(*transa == 'T')) ? CUBLAS_OP_T : cu_transa;
	cu_transa = ((*transa == 'n')||(*transa == 'N')) ? CUBLAS_OP_N : cu_transa;
	cu_transb = ((*transb == 'c')||(*transb == 'C')) ? CUBLAS_OP_C : CUBLAS_OP_N;
	cu_transb = ((*transb == 't')||(*transb == 'T')) ? CUBLAS_OP_T : cu_transb;
	cu_transb = ((*transb == 'n')||(*transb == 'N')) ? CUBLAS_OP_N : cu_transb;

	devPtrA[0] = (double *)(myPhiGemmHdl.pmem[iDev]);
	devPtrA[1] = devPtrA[0] + (* m) * (last_split != 0 ? last_split : local_split);
	devPtrB[0] = devPtrA[1] + (* m) * (last_split != 0 ? last_split : local_split);
	devPtrB[1] = devPtrB[0] + (* n) * (last_split != 0 ? last_split : local_split);
	devPtrC[0] = devPtrB[1] + (last_split != 0 ? last_split : local_split) * (* n);
	devPtrC[1] = devPtrC[0] + (* m) * (* n);

	for( i = 0; i < MAX_N_STREAM; i++){
		if(     cudaHostAlloc( (void **) &C_buf[i], (* n) * (* ldc) * sizeof(double), cudaHostAllocPortable) != cudaSuccess )
		{
			printf( "*** ERROR allocating PINNED MEMORY on CPU\n" );
			exit( EXIT_FAILURE );
		}
	}

	splitted_size = local_split;

	gpu_lda = (* m);
	gpu_ldb = splitted_size;

	if ( is_transa ) gpu_lda = splitted_size;
	if ( is_transb ) gpu_ldb = (* n);

	cudaStreamCreate( &streamPtr[0] );
	cudaStreamCreate( &streamPtr[1] );

	for (count = 0; count < loop_times ; count++) {

		if( count == (loop_times - 1) && last_split != 0 ){
			splitted_size = last_split;
			if ( is_transa ) gpu_lda = last_split;
			if ( !is_transb ) gpu_ldb = last_split;
		}

		stream = count % MAX_N_STREAM;

		cublasSetStream( myPhiGemmHdl.handle[ iDev ], streamPtr[stream] );

		if( is_transa ){
			status = cublasSetMatrixAsync ( splitted_size, (* m), sizeof(double), A + offsetA, (* lda), devPtrA[stream], gpu_lda, streamPtr[stream] );
		} else {
			status = cublasSetMatrixAsync ( (* m), splitted_size, sizeof(double), A + offsetA, (* lda), devPtrA[stream], gpu_lda, streamPtr[stream] );
		}

		if(is_transb ){
			status = cublasSetMatrixAsync ( (* n), splitted_size, sizeof(double), B + offsetB, (* ldb), devPtrB[stream], gpu_ldb, streamPtr[stream] );
		} else {
			status = cublasSetMatrixAsync ( splitted_size, (* n), sizeof(double), B + offsetB, (* ldb), devPtrB[stream], gpu_ldb, streamPtr[stream] );
		}

		status = cublasGemm ( myPhiGemmHdl.handle[ iDev ], cu_transa, cu_transb, (* m), (* n), splitted_size, alpha, devPtrA[stream], gpu_lda, devPtrB[stream], gpu_ldb, &gpu_beta, devPtrC[stream], (* m) );

		status = cublasGetMatrixAsync ( (* m), (* n), sizeof(double), devPtrC[stream], (* m), C_buf[stream], *ldc, streamPtr[stream] );

#if defined(__PHIGEMM_DEBUG)
		start_axpy = phigemm_cclock();
#endif

		if( count != 0 ) {
			cudaStreamSynchronize( streamPtr[(stream+1)%MAX_N_STREAM] );

#if defined(__INTEL) && defined(__MKL) && defined(__OMP) && defined (__EXPERIMENTAL)
			/* >>> Alternative way to handle multiple DAXPY:
			 * - OpenMP threads for BLAS are set to 0
			 * - each thread perform a group of single-threaded DAXPY
			 * - mkl_domain_set_num_threads is restored to the default
			 */

			mkl_domain_set_num_threads ( 1, MKL_BLAS );

#pragma omp for private(offsetC)
			for(i=0; i<(*n); i++){
				offsetC = (* ldc) * i;
				daxpy_( m, &DA, C_buf[(stream+1)% MAX_N_STREAM]+offsetC, &inc, C+offsetC, &inc);
			}

			mkl_domain_set_num_threads ( myPhiGemmEnv.cores, MKL_BLAS );
#else

			for(i=0, offsetC=0; i<(*n); i++){
				daxpy_( m, &DA, C_buf[(stream+1)%MAX_N_STREAM]+offsetC, &inc, C+offsetC, &inc);
				offsetC += (* ldc);
			}
#endif

		} else {

			for(i=0, offsetC=0; i<(*n); i++){
				dscal_( m, beta, C+offsetC, &inc );
				offsetC += (* ldc);
			}

		}

#if defined(__PHIGEMM_DEBUG)
		stop_axpy = phigemm_cclock();
		time_axpy += stop_axpy - start_axpy;
#endif

		if( is_transa) offsetA += splitted_size;
		else offsetA += (* m) * splitted_size;

		if( is_transb) offsetB += (* n) * splitted_size;
		else offsetB += splitted_size;
	}

#if defined(__PHIGEMM_DEBUG)
	start_axpy = phigemm_cclock();
#endif

	cudaStreamSynchronize( streamPtr[stream] );
	for(i=0, offsetC=0; i<(*n); i++){
		daxpy_( m, &DA, C_buf[stream]+offsetC, &inc, C+offsetC, &inc);
		offsetC += (* ldc);
	}

#if defined(__PHIGEMM_DEBUG)
	stop_axpy = phigemm_cclock();
	time_axpy += stop_axpy - start_axpy;
#endif

	cudaStreamDestroy( streamPtr[0] );
	cudaStreamDestroy( streamPtr[1] );

	for (i = 0; i < myPhiGemmEnv.numDevices * NSTREAMS; i++)
		cublasSetStream( myPhiGemmHdl.handle[ i ], myPhiGemmHdl.stream[ i ] );

	for( i = 0; i < MAX_N_STREAM; i++){
		cudaFreeHost( C_buf[i] );
	}

#if defined(__PHIGEMM_DEBUG)

	stop_gemm_total = phigemm_cclock();

	double time_total = stop_gemm_total - start_gemm_total;

#if defined(__PHIGEMM_PROFILE)
	printf ("[PHIGEMM_DEBUG - %s:%s - GPU %d] %d %d %d ~ Special K ~ local_split:%d (loop_times=%d, last_split:%d) ~ Total:%9.6fs (axpy:%9.6fs)\n",
			file, line, iDev % myPhiGemmEnv.numDevices, *m, *n, *k, local_split, loop_times, last_split, time_total, time_axpy); fflush(stdout);
#else
	printf ("[PHIGEMM_DEBUG - GPU %d] %d %d %d ~ Special K ~ local_split:%d (loop_times=%d, last_split:%d) ~ Total:%9.6fs (axpy:%9.6fs)\n",
			iDev % myPhiGemmEnv.numDevices, *m, *n, *k, local_split, loop_times, last_split, time_total, time_axpy); fflush(stdout);
#endif
#endif

#if defined(__PHIGEMM_MEMSET)
	cudaMemset( myPhiGemmHdl.pmem[iDev], 0, mem_buffer );
#endif

#endif

	return;
}

