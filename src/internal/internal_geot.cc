//------------------------------------------------------------------------------
// Copyright (c) 2017, University of Tennessee
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the University of Tennessee nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL UNIVERSITY OF TENNESSEE BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//------------------------------------------------------------------------------
// This research was supported by the Exascale Computing Project (17-SC-20-SC),
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.
//------------------------------------------------------------------------------
// For assistance with SLATE, email <slate-user@icl.utk.edu>.
// You can also join the "SLATE User" Google group by going to
// https://groups.google.com/a/icl.utk.edu/forum/#!forum/slate-user,
// signing in with your Google credentials, and then clicking "Join group".
//------------------------------------------------------------------------------

#include "slate/internal/device.hh"
#include "internal/internal_batch.hh"
#include "internal/internal.hh"
#include "slate/internal/util.hh"
#include "slate/Matrix.hh"
#include "internal/Tile_lapack.hh"
#include "slate/types.hh"

namespace slate {
namespace internal {

//------------------------------------------------------------------------------
/// Eliminates the first row or column of a matrix using orthogonal
/// transformation.
/// Dispatches to target implementations.
/// @ingroup geot_internal
///
template <Target target, typename scalar_t>
void geot(Matrix<scalar_t>&& A, int priority)
{
    geot(internal::TargetType<target>(),
         A, priority);
}

//------------------------------------------------------------------------------
/// Eliminates the first row or column of a matrix using orthogonal
/// transformation.
/// Host OpenMP task implementation.
/// @ingroup geot_internal
///
template <typename scalar_t>
void geot(internal::TargetType<Target::HostTask>,
          Matrix<scalar_t>& A, int priority)
{

    for (int64_t i = 0; i < A.mt(); ++i) {
        for (int64_t j = 0; j < A.nt(); ++j) {
            if (A.tileIsLocal(i, j)) {
                #pragma omp task shared(A) priority(priority)
                {
                    A.tileGetForWriting(i, j, LayoutConvert::None);
                }
            }
        }
    }

    #pragma omp taskwait

    // V <- A[:, 0]
    std::vector<scalar_t> v(A.m());
    scalar_t* v_ptr = v.data();
    for (int64_t i = 0; i < A.mt(); ++i) {
        auto tile = A.at(i, 0);
        blas::copy(tile.mb(), &tile.at(0, 0),
                   tile.op() == Op::NoTrans ? 1 : tile.stride(),
                   v_ptr, 1);
        v_ptr += tile.mb();
    }

    // Compute the reflector in V.
    scalar_t tau;
    lapack::larfg(A.m(), v.data(), v.data()+1, 1, &tau);
    v.at(0) = scalar_t(1.0);

    // W = C^T * V
    auto AT = transpose(A);
    std::vector<scalar_t> w(AT.m());

    scalar_t* w_ptr = w.data();
    for (int64_t i = 0; i < AT.mt(); ++i) {
        v_ptr = v.data();
        for (int64_t j = 0; j < AT.nt(); ++j) {
            gemv(scalar_t(1.0), AT.at(i, j), v_ptr,
                 j == 0 ? scalar_t(0.0) : scalar_t(1.0), w_ptr);
            v_ptr += AT.tileNb(j);
        }
        w_ptr += AT.tileMb(i);
    }

    // C = C - V * W^T
    v_ptr = v.data();
    for (int64_t i = 0; i < A.mt(); ++i) {
        w_ptr = w.data();
        for (int64_t j = 0; j < A.nt(); ++j) {
            ger(-tau, v_ptr, w_ptr, A.at(i, j));
            w_ptr += A.tileNb(j);
        }
        v_ptr += A.tileMb(i);
    }
}

//------------------------------------------------------------------------------
// Explicit instantiations.
// ----------------------------------------
template
void geot<Target::HostTask, float>(
    Matrix<float>&& A, int priority);

template
void geot<Target::HostTask, double>(
    Matrix<double>&& A,  int priority);

template
void geot< Target::HostTask, std::complex<float> >(
    Matrix< std::complex<float> >&& A,  int priority);

template
void geot< Target::HostTask, std::complex<double> >(
    Matrix< std::complex<double> >&& A,  int priority);

} // namespace internal
} // namespace slate
