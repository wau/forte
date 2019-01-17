/*
 * @BEGIN LICENSE
 *
 * Forte: an open-source plugin to Psi4 (https://github.com/psi4/psi4)
 * that implements a variety of quantum chemistry methods for strongly
 * correlated electrons.
 *
 * Copyright (c) 2012-2019 by its authors (see COPYING, COPYING.LESSER,
 * AUTHORS).
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

#ifndef _ewci_asci_h_
#define _ewci_asci_h_

#include "base_classes/active_space_method.h"
#include "sparse_ci/determinant.h"

namespace forte {

class EWCI_ASCI : public ActiveSpaceMethod {
  public:
    EWCI_ASCI(StateInfo state, size_t nroot, std::shared_ptr<SCFInfo> scf_info,
              std::shared_ptr<ForteOptions> options, std::shared_ptr<MOSpaceInfo> mo_space_info,
              std::shared_ptr<ActiveSpaceIntegrals> as_ints);

    // ==> Class Interface <==

    /// Compute the energy
    double compute_energy() override;

    /// Update the reference file
    Reference get_reference(int root = 0) override {}

    void set_options(std::shared_ptr<ForteOptions>) override{} // TODO : define

private:
  // ==> Class data <==
    /// HF info
    std::shared_ptr<SCFInfo> scf_info_;
    /// Options
    std::shared_ptr<ForteOptions> options_;
    /// The wave function symmetry
    int wavefunction_symmetry_;
    /// The symmetry of each orbital in Pitzer ordering
    std::vector<int> mo_symmetry_;
    /// The multiplicity of the reference
    int multiplicity_;
    /// M_s of the reference
    int twice_ms_;
    /// Number of irreps
    size_t nirrep_;
    /// The number of frozen core orbitals
    int nfrzc_;
    /// The number of frozen core orbital per irrets
    psi::Dimension frzcpi_;
    /// The number of active orbitals per irrep
    psi::Dimension nactpi_;
    /// The number of active orbitals
    size_t nact_;
    /// The nuclear repulsion energy
    double nuclear_repulsion_energy_;
    /// The reference determinant
    std::vector<Determinant> initial_reference_;

    // * Calculation info
    /// The threshold applied to the primary space
    double spawning_threshold_;
};
}

#endif // _ewci_asci_h_
