/*
 *@BEGIN LICENSE
 *
 * Libadaptive: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

#ifndef _sparse_ci_h_
#define _sparse_ci_h_

#include "bitset_determinant.h"

#include <libmints/vector.h>
#include <libmints/matrix.h>

#define BIGNUM 1E100
#define MAXIT 100


namespace psi{ namespace libadaptive{

enum DiagonalizationMethod {Full,DavidsonLiuDense,DavidsonLiuSparse,DavidsonLiuString};


/**
 * @brief The SigmaVector class
 * Base class for a sigma vector object.
 */
class SigmaVector
{
public:
    SigmaVector(size_t size) : size_(size) {};

    size_t size() {return size_;}

    virtual void compute_sigma(Matrix& sigma, Matrix& b, int nroot) = 0;
    virtual void get_diagonal(Vector& diag) = 0;

protected:
    size_t size_;
};

/**
 * @brief The SigmaVectorFull class
 * Computes the sigma vector from a full Hamiltonian.
 */
class SigmaVectorFull : public SigmaVector
{
public:
    SigmaVectorFull(SharedMatrix H) : SigmaVector(H->ncol()), H_(H) {};

    void compute_sigma(Matrix& sigma, Matrix& b, int nroot);
    void get_diagonal(Vector& diag);

protected:
    SharedMatrix H_;
};

/**
 * @brief The SigmaVectorSparse class
 * Computes the sigma vector from a sparse Hamiltonian.
 */
class SigmaVectorSparse : public SigmaVector
{
public:
    SigmaVectorSparse(std::vector<std::pair<std::vector<int>,std::vector<double>>>& H) : SigmaVector(H.size()), H_(H) {};

    void compute_sigma(Matrix& sigma, Matrix& b, int nroot);
    void get_diagonal(Vector& diag);

protected:
    std::vector<std::pair<std::vector<int>,std::vector<double>>>& H_;
};

/**
 * @brief The SparseCISolver class
 * This class diagonalizes the Hamiltonian in a basis
 * of determinants.
 */

class SparseCISolver
{
public:    
    // ==> Class Constructor and Destructor <==

    /**
     * Constructor
     */
    SparseCISolver() {};

    /// Destructor
    ~SparseCISolver() {};

    // ==> Class Interface <==

    /**
     * Diagonalize the Hamiltonian in a basis of determinants
     * @param space The basis for the CI given as a vector of BitsetDeterminant objects
     * @param nroot The number of solutions to find
     * @param diag_method The diagonalization algorithm
     */
    void diagonalize_hamiltonian(const std::vector<BitsetDeterminant>& space,
                                   SharedVector& evals,
                                   SharedMatrix& evecs,
                                   int nroot,
                                   DiagonalizationMethod diag_method = DavidsonLiuSparse);

    /**
     * Diagonalize the Hamiltonian in a basis of determinants
     * @param space The basis for the CI given as a vector of BitsetDeterminant objects
     * @param nroot The number of solutions to find
     * @param diag_method The diagonalization algorithm
     */
    void diagonalize_hamiltonian(const std::vector<SharedBitsetDeterminant>& space,
                                   SharedVector& evals,
                                   SharedMatrix& evecs,
                                   int nroot,
                                   DiagonalizationMethod diag_method = DavidsonLiuSparse);

private:
    /// Form the full Hamiltonian and diagonalize it (for debugging)
    void diagonalize_full(const std::vector<BitsetDeterminant>& space,
                          SharedVector& evals,
                          SharedMatrix& evecs,
                          int nroot);

    /// Form the full Hamiltonian and use the Davidson-Liu method to compute the first nroot eigenvalues
    void diagonalize_davidson_liu_dense(const std::vector<BitsetDeterminant>& space,
                                        SharedVector& evals,
                                        SharedMatrix& evecs,
                                        int nroot);

    /// Form a sparse Hamiltonian and use the Davidson-Liu method to compute the first nroot eigenvalues
    void diagonalize_davidson_liu_sparse(const std::vector<BitsetDeterminant>& space,
                                         SharedVector& evals,
                                         SharedMatrix& evecs,
                                         int nroot);

    /// Build the full Hamiltonian matrix
    SharedMatrix build_full_hamiltonian(const std::vector<BitsetDeterminant>& space);

    /// Build a sparse Hamiltonian matrix
    std::vector<std::pair<std::vector<int>,std::vector<double>>> build_sparse_hamiltonian(const std::vector<BitsetDeterminant> &space);

    /// Form the full Hamiltonian and diagonalize it (for debugging)
    void diagonalize_full(const std::vector<SharedBitsetDeterminant>& space,
                          SharedVector& evals,
                          SharedMatrix& evecs,
                          int nroot);

    /// Form the full Hamiltonian and use the Davidson-Liu method to compute the first nroot eigenvalues
    void diagonalize_davidson_liu_dense(const std::vector<SharedBitsetDeterminant>& space,
                                        SharedVector& evals,
                                        SharedMatrix& evecs,
                                        int nroot);

    /// Form a sparse Hamiltonian and use the Davidson-Liu method to compute the first nroot eigenvalues
    void diagonalize_davidson_liu_sparse(const std::vector<SharedBitsetDeterminant>& space,
                                         SharedVector& evals,
                                         SharedMatrix& evecs,
                                         int nroot);

    /// Build the full Hamiltonian matrix
    SharedMatrix build_full_hamiltonian(const std::vector<SharedBitsetDeterminant>& space);

    /// Build a sparse Hamiltonian matrix
    std::vector<std::pair<std::vector<int>,std::vector<double>>> build_sparse_hamiltonian(const std::vector<SharedBitsetDeterminant> &space);

    /// The Davidson-Liu algorithm
    bool davidson_liu(SigmaVector* sigma_vector,SharedVector Eigenvalues,SharedMatrix Eigenvectors,int nroot_s);
};


}}

#endif // _sparse_ci_h_