# phiGEMM

Copyright (C) 2011-2014 Quantum ESPRESSO Foundation
Copyright (C) 2010-2011 Irish Centre for High-End Computing (ICHEC)


## License
 
All the material included in this distribution is free software;
you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation;
either version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
675 Mass Ave, Cambridge, MA 02139, USA.


## Authors & Maintainers

Filippo Spiga <filippo.spiga(at)quantum-espresso.org> (main contact)
Ivan Girotto <ivan.girotto(at)ictp.it>


## Recognized options of the configure

--enable-parallel       : enable the MPI support in case of self-initialization
                          (default: no)
--with-cuda-dir=<path>  : specify the path where CUDA is installed 
                          (mandatory)
--with-magma-dir=<path> : specify the path where MAGMABLAS is installed 
                          (not yet working)                          
--enable-cpu-multithread: enable the multi-threading library on CPU side
                          (default: yes)
--with-cpu-only         : everything is performed only by the CPU
                          (default: no)
--with-gpu-only         : everything is performed only by the GPU
                          (default: no)                         
--enable-debug          : turn on verbose debug messages on the stdout
                          (default: no)
--enable-profiling      : turn on the phiGEMM call-per-call profile
                          (default: no)
--enable-pinned         : the library marks as non-pageable the host memory
                          (default: no) 
--with-special-k        : enable SPECIAL-K for bad-shaped rectangular matrices
                          (default: no)
--enable-multi-gpu      : enable multi-GPU support 
                          (default: no) 


## Releases

v2.0.0   - March 23, 2014
v2.0.0rc - December 15, 2012
v1.9.9   - October 1, 2012
v1.9     - May 23, 2012
v1.8     - April 1, 2012
v1.7     - March 10, 2012
v1.6     - January 19, 2012
v1.5     - December 9, 2011
v1.4     - September 26, 2011
v1.3     - September 10, 2011
v1.2     - August 29, 2011
v1.1     - July 12, 2011 
v1.0     - July 4, 2011
v0.9     - July 2, 2011
v0.8     - June 29, 2011
v0.7     - May 29, 2011
v0.6     - May 9, 2011
v0.5     - May 3, 2011
v0.4     - March 24, 2011
v0.3     - March 24, 2011
v0.2     - February 21, 2011
v0.1     - January 27, 2011


## References

* F. Spiga and I. Girotto, "phiGEMM: a CPU-GPU library for porting Quantum 
ESPRESSO on hybrid systems", Proceeding of 20th Euromicro International 
Conference on Parallel, Distributed and Network-Based Computing (PDP2012), 
Special Session on GPU Computing and Hybrid Computing, IEEE Computer Society,
(ISBN 978-0-7695-4633-9), pp. 368-375 (2012)

* M. Fatica, "Accelerating LINPACK with CUDA on heterogeneous clusters." 
GPGPU-2: Proceedings of 2nd Workshop on General Purpose Processing on 
Graphics Processing Units (New York, NY, USA), ACM, 2009, pp. 46--51.