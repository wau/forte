/*
 * @BEGIN LICENSE
 *
 * Forte: an open-source plugin to Psi4 (https://github.com/psi4/psi4)
 * that implements a variety of quantum chemistry methods for strongly
 * correlated electrons.
 *
 * Copyright (c) 2012-2019 by its authors (see COPYING, COPYING.LESSER, AUTHORS).
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * @END LICENSE
 */

#include <fstream>

#include <pybind11/pybind11.h>

namespace py = pybind11;

#include "psi4/libpsi4util/process.h"

#include "orbital-helpers/aosubspace.h"
#include "orbital-helpers/avas.h"
#include "base_classes/forte_options.h"
#include "base_classes/mo_space_info.h"
#include "helpers/timer.h"

#include "sparse_ci/determinant.h"
#include "version.h"
#include "psi4/libpsi4util/PsiOutStream.h"
#include "psi4/psi4-dec.h"

#ifdef HAVE_CHEMPS2
#include "dmrg/dmrgscf.h"
#include "dmrg/dmrgsolver.h"
#endif

#ifdef HAVE_GA
#include <ga.h>
#include <macdecls.h>
#endif

#ifdef HAVE_MPI
#include <mpi.h>
#endif

#include "forte.h"

using namespace psi;

namespace forte {

/// These functions replace the Memory Allocator in GA with C/C++ allocator.
void* replace_malloc(size_t bytes, int, char*) { return malloc(bytes); }
void replace_free(void* ptr) { free(ptr); }

/**
 * @brief Read options from the input file. Called by psi4 before everything else.
 */
void read_options(ForteOptions& options) {
    forte_options(options);
    forte_old_options(options);
}

/**
 * @brief Initialize ambit, MPI, and GA. All functions that need to be called
 * once before running forte should go here.
 * @return The pair (my_proc,n_nodes)
 */
std::pair<int, int> startup() {
    ambit::initialize();

#ifdef HAVE_MPI
    MPI_Init(NULL, NULL);
#endif

    int my_proc = 0;
    int n_nodes = 1;
#ifdef HAVE_GA
    GA_Initialize();
    /// Use C/C++ memory allocators
    GA_Register_stack_memory(replace_malloc, replace_free);
    n_nodes = GA_Nnodes();
    my_proc = GA_Nodeid();
    size_t memory = psi::Process::environment.get_memory() / n_nodes;
#endif

#ifdef HAVE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &my_proc);
    MPI_Comm_size(MPI_COMM_WORLD, &n_nodes);
#endif
    return std::make_pair(my_proc, n_nodes);
}

/**
 * @brief Finalize ambit, MPI, and GA. All functions that need to be called
 * once after running forte should go here.
 */
void cleanup() {

#ifdef HAVE_GA
    GA_Terminate();
#endif

    ambit::finalize();

#ifdef HAVE_MPI
    MPI_Finalize();
#endif
}

psi::SharedMatrix make_aosubspace_projector(psi::SharedWavefunction ref_wfn,
                                            psi::Options& options) {
    // Ps is a psi::SharedMatrix Ps = S^{BA} X X^+ S^{AB}
    auto Ps = create_aosubspace_projector(ref_wfn, options);
    if (Ps) {

        psi::SharedMatrix CPsC = Ps->clone();
        CPsC->transform(ref_wfn->Ca());
        outfile->Printf("\n  Orbital overlap with ao subspace:\n");
        outfile->Printf("    ========================\n");
        outfile->Printf("    Irrep   MO   <phi|P|phi>\n");
        outfile->Printf("    ------------------------\n");
        for (int h = 0; h < CPsC->nirrep(); h++) {
            for (int i = 0; i < CPsC->rowspi(h); i++) {
                outfile->Printf("      %1d   %4d    %.6f\n", h, i + 1, CPsC->get(h, i, i));
            }
        }
        outfile->Printf("    ========================\n");
    }
    return Ps;
}

void banner() {
    outfile->Printf(
        "\n"
        "  Forte\n"
        "  ----------------------------------------------------------------------------\n"
        "  A suite of quantum chemistry methods for strongly correlated electrons\n\n"
        "    git branch: %s - git commit: %s\n\n"
        "  Developed by:\n"
        "    Francesco A. Evangelista, Chenyang Li, Kevin P. Hannon,\n"
        "    Jeffrey B. Schriber, Tianyuan Zhang, Chenxi Cai,\n"
        "    Nan He, Nicholas Stair, Shuhe Wang, Renke Huang\n"
        "  ----------------------------------------------------------------------------\n",
        GIT_BRANCH, GIT_COMMIT_HASH);
    outfile->Printf("\n  Size of Determinant class: %d", sizeof(Determinant));
}

} // namespace forte

///**
// * @brief The main forte function.
// */
// psi::SharedWavefunction run_forte(psi::SharedWavefunction ref_wfn, psi::Options& options) {
//    // Start a timer
//    timer total_time("Forte");

////    forte_banner();

//    auto my_proc_n_nodes = forte_startup();
//    int my_proc = my_proc_n_nodes.first;

//    // Make a MOSpaceInfo object
//    auto mo_space_info = make_mo_space_info(ref_wfn, options);

//    // Sanity check
//    if ((4 * sizeof(Determinant)) < mo_space_info->size("ACTIVE")) {
//        outfile->Printf("\n  FATAL:  The active space you requested (%d) is larger than allowed by
//        "
//                        "the determinant class (%d)!",
//                        mo_space_info->size("ACTIVE"), 4 * sizeof(Determinant));
//        outfile->Printf("\n          Please check your input and/or recompile Forte with a larger
//        "
//                        "determinant dimension");
//        exit(1);
//    }

//    if (((options.get_str("DIAG_ALGORITHM") == "DYNAMIC") or
//         (options.get_bool("ACI_DIRECT_RDMS") == true)) and
//        (mo_space_info->size("ACTIVE") > 64)) {

//        outfile->Printf("\n  FATAL:  Dynamic diagonalization or dynamic RDM builds cannot be used
//        "
//                        "for active spaces larger than 64 orbitals!");

//        exit(1);
//    }

//    // Make a subspace object
//    psi::SharedMatrix Ps = make_aosubspace_projector(ref_wfn, options);

//    // Transform the orbitals
//    make_avas(ref_wfn, options, Ps);

//    std::string job_type = options.get_str("JOB_TYPE");
//    if (job_type != "NONE") {
//        // Transform integrals and run forte only if necessary
//        // Make an integral object
//        auto ints = make_forte_integrals(ref_wfn, options, mo_space_info);

//        // Compute energy
//        forte_old_methods(ref_wfn, options, ints, mo_space_info, my_proc);

//        //        outfile->Printf("\n\n  Your calculation took %.8f seconds\n", total_time.get());
//    }

//    forte_cleanup();
//    return ref_wfn;
//}
