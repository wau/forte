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

#include "psi4/libpsi4util/process.h"
#include "psi4/libmints/molecule.h"
#include "psi4/libmints/wavefunction.h"
#include "psi4/libpsio/psio.h"
#include "psi4/libpsio/psio.hpp"

#include "psi4/libmints/local.h"
#include "psi4/libmints/matrix.h"
#include "psi4/libmints/vector.h"
#include "psi4/liboptions/liboptions.h"
#include "base_classes/reference.h"

#include "localize.h"

using namespace psi;

namespace forte {

LOCALIZE::LOCALIZE(std::shared_ptr<psi::Wavefunction> wfn, psi::Options& options,
                   std::shared_ptr<ForteIntegrals> ints, std::shared_ptr<MOSpaceInfo> mo_space_info)
    : wfn_(wfn), ints_(ints) {
    nfrz_ = mo_space_info->size("FROZEN_DOCC");
    nrst_ = mo_space_info->size("RESTRICTED_DOCC");
    namo_ = mo_space_info->size("ACTIVE");

    if (wfn_->nirrep() > 1) {
        throw psi::PSIEXCEPTION("\n\n ERROR: Localizer only implemented for C1 symmetry!");
    }

    int nel = 0;
    int natom = psi::Process::environment.molecule()->natom();
    for (int i = 0; i < natom; i++) {
        nel += static_cast<int>(psi::Process::environment.molecule()->Z(i));
    }
    nel -= options.get_int("CHARGE");

    // The wavefunction multiplicity
    multiplicity_ = options.get_int("MULTIPLICITY");
    outfile->Printf("\n MULT: %d", multiplicity_);

    // The number of active electrons
    int nactel = nel - 2 * nfrz_ - 2 * nrst_;

    naocc_ = ((nactel - (nactel % 2)) / 2) + (nactel % 2);
    navir_ = namo_ - naocc_;

    abs_act_ = mo_space_info->get_absolute_mo("ACTIVE");

    local_type_ = options.get_str("LOCALIZE_TYPE");

    if (local_type_ == "BOYS" or local_type_ == "SPLIT_BOYS") {
        local_method_ = "BOYS";
    }
    if (local_type_ == "PM" or local_type_ == "SPLIT_PM") {
        local_method_ = "PIPEK_MEZEY";
    }
}

void LOCALIZE::split_localize() {
    psi::SharedMatrix Ca = ints_->Ca();
    psi::SharedMatrix Cb = ints_->Cb();

    psi::Dimension nsopi = wfn_->nsopi();
    int nirrep = wfn_->nirrep();
    int off = 0;
    if (multiplicity_ == 3) {
        naocc_ -= 1;
        navir_ -= 1;
        off = 2;
    }

    psi::SharedMatrix Caocc(new psi::Matrix("Caocc", nsopi[0], naocc_));
    psi::SharedMatrix Cavir(new psi::Matrix("Cavir", nsopi[0], navir_));
    psi::SharedMatrix Caact(new psi::Matrix("Caact", nsopi[0], off));

    for (int h = 0; h < nirrep; h++) {
        for (int mu = 0; mu < nsopi[h]; mu++) {
            for (int i = 0; i < naocc_; i++) {
                Caocc->set(h, mu, i, Ca->get(h, mu, abs_act_[i]));
            }
            for (int i = 0; i < navir_; ++i) {
                Cavir->set(h, mu, i, Ca->get(h, mu, abs_act_[i + naocc_ + off]));
            }
            for (int i = 0; i < off; ++i) {
                Caact->set(h, mu, i, Ca->get(h, mu, abs_act_[i + naocc_]));
            }
        }
    }

    std::shared_ptr<psi::BasisSet> primary = wfn_->basisset();

    std::shared_ptr<Localizer> loc_a = Localizer::build(local_type_, primary, Caocc);
    loc_a->localize();

    psi::SharedMatrix Laocc = loc_a->L();

    std::shared_ptr<psi::Localizer> loc_v = psi::Localizer::build(local_type_, primary, Cavir);
    loc_v->localize();

    psi::SharedMatrix Lvir = loc_v->L();

    std::shared_ptr<psi::Matrix> U = std::make_shared<psi::Matrix>("U",Ca->rowspi(), Ca->colspi()); 
    U->identity();

    psi::SharedMatrix Lact;
    psi::SharedMatrix Uact;
    if (multiplicity_ == 3) {
        std::shared_ptr<Localizer> loc_c = Localizer::build(local_type_, primary, Caact);
        loc_c->localize();
        Lact = loc_c->L();
        Uact = loc_c->U();
    }
    psi::SharedMatrix Uocc = loc_a->U();
    psi::SharedMatrix Uvir = loc_v->U();

    for (int h = 0; h < nirrep; ++h) {
        for (int i = 0; i < naocc_; ++i) {
            psi::SharedVector vec = Laocc->get_column(h, i);
            Ca->set_column(h, i + nfrz_ + nrst_, vec);
            Cb->set_column(h, i + nfrz_ + nrst_, vec);
            U->set_column(h, i + nfrz_ + nrst_, Uocc->get_column(h,i));
        }
        for (int i = 0; i < navir_; ++i) {
            psi::SharedVector vec = Lvir->get_column(h, i);
            Ca->set_column(h, i + nfrz_ + nrst_ + naocc_ + off, vec);
            Cb->set_column(h, i + nfrz_ + nrst_ + naocc_ + off, vec);
            U->set_column(h, i + nfrz_ + nrst_+ naocc_ + off, Uvir->get_column(h,i));
        }

        for (int i = 0; i < off; ++i) {
            psi::SharedVector vec = Lact->get_column(h, i);
            Ca->set_column(h, i + nfrz_ + nrst_ + naocc_, vec);
            Cb->set_column(h, i + nfrz_ + nrst_ + naocc_, vec);
            U->set_column(h, i + nfrz_ + nrst_+ naocc_ + off, Uact->get_column(h,i));
        }
    }

    double value = 0.0;
    for (int h = 0; h < nirrep; ++h) {
        for (int i = 0; i < Ca->rowdim(h); ++i) {
            for (int j = 0; j < Ca->coldim(h); ++j) {
                value = std::fabs(Ca->get(i, j) - Cb->get(i, j));
            }
        }
    }
    outfile->Printf("\n  ||Ca - Cb||_[1] = %1.5f", value);

    ints_->rotate_orbitals(U,U);
}

void LOCALIZE::full_localize() {

    // Build C matrices
    psi::SharedMatrix Ca = wfn_->Ca();
    psi::SharedMatrix Cb = wfn_->Cb();
    psi::Dimension nsopi = wfn_->nsopi();
    int nirrep = wfn_->nirrep();

    size_t nact = abs_act_.size();

    psi::SharedMatrix Caact(new psi::Matrix("Caact", nsopi[0], nact));
    for (int h = 0; h < nirrep; h++) {
        for (int mu = 0; mu < nsopi[h]; mu++) {
            for (size_t i = 0; i < nact; i++) {
                Caact->set(h, mu, i, Ca->get(h, mu, abs_act_[i]));
            }
        }
    }

    // Localize all active together
    std::shared_ptr<psi::BasisSet> primary = wfn_->basisset();

    std::shared_ptr<Localizer> loc_a = Localizer::build(local_type_, primary, Caact);
    loc_a->localize();

    psi::SharedMatrix Ua = loc_a->U();
    psi::SharedMatrix Laocc = loc_a->L();

    std::shared_ptr<psi::Matrix> U = std::make_shared<psi::Matrix>("U", Ca->colspi(), Ca->rowspi());
    U->identity();

    for (int h = 0; h < nirrep; ++h) {
        for (size_t i = 0; i < nact; ++i) {
            psi::SharedVector vec = Laocc->get_column(h, i);
            Ca->set_column(h, i + nfrz_ + nrst_, vec);
            Cb->set_column(h, i + nfrz_ + nrst_, vec);
            U->set_column(h, i + nfrz_ + nrst_, Ua->get_column(h,i));
        }
    }
    ints_->rotate_orbitals(U,U);

    U_ = std::make_shared<psi::Matrix>("U", nsopi[0], nact);
    U_->copy(Ua);
}

psi::SharedMatrix LOCALIZE::get_U() { return U_; }

LOCALIZE::~LOCALIZE() {}
}