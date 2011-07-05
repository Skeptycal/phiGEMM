/*
 * Copyright (C) 2010-2011 Irish Centre for High-End Computing (ICHEC)
 *
 * This file is distributed under the terms of the
 * GNU General Public License. See the file `License'
 * in the root directory of the present distribution,
 * or http://www.gnu.org/copyleft/gpl.txt .
 *
 * author(s):	Philip Yang   (phi@cs.umd.edu)
 * 				Filippo Spiga (filippo.spiga@ichec.ie)
 * 				Ivan Girotto  (ivan.girotto@ichec.ie)
 *
 */

#ifndef __PHIGEMM_AUXILIARY_H__
#define __PHIGEMM_AUXILIARY_H__

#include "phigemm_common.h"

#include "cublas_api.h"
#include "cublas_v2.h"

#define imin(a,b) (((a)<(b))?(a):(b))
#define imax(a,b) (((a)<(b))?(b):(a))

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct timestruct
{
	unsigned int sec;
	unsigned int usec;
} TimeStruct;

void estmSplitFactor(const char* optype, char transa, char transb);
TimeStruct get_current_time(void);
double GetTimerValue(TimeStruct time_1, TimeStruct time_2);

double phigemm_cclock(void);

extern cudaStream_t phiStreams[MAX_GPUS*NSTREAM_PER_DEVICE];
extern cublasHandle_t phiHandles[MAX_GPUS*NSTREAM_PER_DEVICE];

#ifdef __cplusplus
}
#endif

#endif // __PHIGEMM_AUXILIARY_H__
