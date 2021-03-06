/*
 * @BEGIN LICENSE
 *
 * Psi4: an open-source quantum chemistry software package
 *
 * Copyright (c) 2007-2016 The Psi4 Developers.
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
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
 * @END LICENSE
 */

#ifndef IAO_BUILDER_H
#define IAO_BUILDER_H

#include "psi4/libmints/matrix.h"
#include "psi4/libmints/integral.h"
#include "psi4/libmints/factory.h"
#include "psi4/libqt/qt.h"
#include "psi4/libmints/molecule.h"
#include "psi4/libmints/wavefunction.h"
#include "psi4/libmints/mintshelper.h"
#include "psi4/libmints/basisset.h"
#include "psi4/libcubeprop/cubeprop.h"

#include "boost/format.hpp"

namespace forte {

class IAOBuilder {

  protected:
    // => Overall Parameters <= //

    /// Print flag
    int print_;
    /// Debug flug
    int debug_;
    /// Bench flag
    int bench_;

    /// Relative convergence criteria
    double convergence_;
    /// Maximum number of iterations
    int maxiter_;

    // => IAO Parameters <= //

    /// Use ghost IAOs?
    bool use_ghosts_;
    /// IAO localization power (4 or 2)
    int power_;
    /// Metric condition for IAO
    double condition_;

    /// Occupied orbitals, in primary basis
    psi::SharedMatrix C_;
    /// Primary orbital basis set
    std::shared_ptr<psi::BasisSet> primary_;
    /// MinAO orbital baiss set
    std::shared_ptr<psi::BasisSet> minao_;

    // => Stars Parameters <= //

    /// Do stars treatment?
    bool use_stars_;
    /// Charge completeness for two-center orbitals
    double stars_completeness_;
    /// List of centers for stars
    std::vector<int> stars_;

    // => IAO Data <= //

    /// Map from non-ghosted to full atoms: true_atoms[ind_true] = ind_full
    std::vector<int> true_atoms_;
    /// Map from non-ghosted IAOs to full IAOs: true_iaos[ind_true] = ind_full
    std::vector<int> true_iaos_;
    /// Map from non-ghosted IAOs to non-ghosted atoms
    std::vector<int> iaos_to_atoms_;

    /// Overlap matrix in full basis
    psi::SharedMatrix S_;
    /// Non-ghosted IAOs in full basis
    psi::SharedMatrix A_;

    /// Set defaults
    void common_init();

  public:
    // => Constructors <= //

    IAOBuilder(std::shared_ptr<psi::BasisSet> primary, std::shared_ptr<psi::BasisSet> minao,
               std::shared_ptr<psi::Matrix> C);

    virtual ~IAOBuilder();

    /// Build IBO with defaults from Options object (including MINAO_BASIS)
    static std::shared_ptr<IAOBuilder> build(std::shared_ptr<psi::BasisSet> primary,
                                             std::shared_ptr<psi::BasisSet> minao,
                                             std::shared_ptr<psi::Matrix> C, psi::Options& options);
    /// Build the IAOs for exporting
    std::map<std::string, psi::SharedMatrix> build_iaos();

    std::vector<std::string> print_IAO(psi::SharedMatrix A, int nmin, int nbf,
                                       psi::SharedWavefunction wfn_);

    std::map<std::string, psi::SharedMatrix>
    ibo_localizer(psi::SharedMatrix L, const std::vector<std::vector<int>>& minao_inds,
                  const std::vector<std::pair<int, int>>& rot_inds, double convergence, int maxiter,
                  int power);

    std::map<std::string, psi::SharedMatrix>
    localize(psi::SharedMatrix Cocc, psi::SharedMatrix Focc, const std::vector<int>& ranges2);

    psi::SharedMatrix reorder_orbitals(psi::SharedMatrix F, const std::vector<int>& ranges);

    psi::SharedMatrix orbital_charges(psi::SharedMatrix L);

    // => Knobs <= //

    void set_print(int print) { print_ = print; }
    void set_debug(int debug) { debug_ = debug; }
    void set_bench(int bench) { bench_ = bench; }
    void set_convergence(double convergence) { convergence_ = convergence; }
    void set_maxiter(int maxiter) { maxiter_ = maxiter; }
    void set_use_ghosts(bool use_ghosts) { use_ghosts_ = use_ghosts; }
    void set_condition(double condition) { condition_ = condition; }
    void set_power(double power) { power_ = power; }
    void set_use_stars(bool use_stars) { use_stars_ = use_stars; }
    void set_stars_completeness(double stars_completeness) {
        stars_completeness_ = stars_completeness;
    }
    void set_stars(const std::vector<int>& stars) { stars_ = stars; }
};

} // namespace forte

#endif
