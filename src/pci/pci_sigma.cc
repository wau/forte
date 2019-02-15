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

#include <cfloat>
#include "pci_sigma.h"

namespace forte {
#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#define omp_get_thread_num() 0
#endif

PCISigmaVector::PCISigmaVector(det_hashvec& dets_hashvec, std::vector<double>& ref_C,
                               double spawning_threshold)
    : SigmaVector(dets_hashvec.size()), dets_(dets_hashvec), ref_C_(ref_C),
      spawning_threshold_(spawning_threshold) {}

void PCISigmaVector::compute_sigma(psi::SharedVector sigma, psi::SharedVector b) {}

void PCISigmaVector::get_diagonal(psi::Vector& diag) {}

void PCISigmaVector::add_bad_roots(
    std::vector<std::vector<std::pair<size_t, double>>>& bad_states) {}

void PCISigmaVector::apply_tau_H_symm(double tau, double spawning_threshold, det_hashvec& ref_dets,
                                      std::vector<double>& ref_C, std::vector<double>& result_C,
                                      double S, size_t& overlap_size) {

    size_t ref_size = ref_dets.size();
    //    result_dets.clear();
    result_C.clear();
    //    det_hashvec result_dets(ref_dets);
    det_hashvec extra_dets;
    std::vector<double> extra_C;
    result_C.resize(ref_size, DBL_MIN);

    std::vector<std::vector<std::pair<Determinant, double>>> thread_det_C_vecs(num_threads_);
    num_off_diag_elem_ = 0;

#pragma omp parallel for
    for (size_t I = 0; I < ref_size; ++I) {
        std::pair<double, double> max_coupling;
        size_t current_rank = omp_get_thread_num();
#pragma omp critical(dets_coupling)
        { max_coupling = dets_max_couplings_[ref_dets[I]]; }
        if (max_coupling.first == 0.0 or max_coupling.second == 0.0) {
            thread_det_C_vecs[current_rank].clear();
            apply_tau_H_symm_det_dynamic_HBCI_2(tau, spawning_threshold, ref_dets, ref_C, I,
                                                ref_C[I], result_C, thread_det_C_vecs[current_rank],
                                                max_coupling);
#pragma omp critical(merge_extra)
            {
                merge(extra_dets, extra_C, thread_det_C_vecs[current_rank],
                      std::function<double(double, double)>(std::plus<double>()), 0.0, false);
            }
#pragma omp critical(dets_coupling)
            { dets_max_couplings_[ref_dets[I]] = max_coupling; }
        } else {
            thread_det_C_vecs[current_rank].clear();
            apply_tau_H_symm_det_dynamic_HBCI_2(tau, spawning_threshold, ref_dets, ref_C, I,
                                                ref_C[I], result_C, thread_det_C_vecs[current_rank],
                                                max_coupling);
#pragma omp critical(merge_extra)
            {
                merge(extra_dets, extra_C, thread_det_C_vecs[current_rank],
                      std::function<double(double, double)>(std::plus<double>()), 0.0, false);
            }
        }
    }

    std::vector<size_t> removing_indices;
    for (size_t I = 0; I < ref_size; ++I) {
        if (result_C[I] == DBL_MIN) {
            removing_indices.push_back(I);
        }
    }
    ref_dets.erase_by_index(removing_indices);
    std::reverse(removing_indices.begin(), removing_indices.end());
    for (size_t I : removing_indices) {
        result_C.erase(result_C.begin() + I);
        ref_C.erase(ref_C.begin() + I);
    }
    overlap_size = ref_dets.size();
    ref_dets.merge(extra_dets);
    result_C.insert(result_C.end(), extra_C.begin(), extra_C.end());

#pragma omp parallel for
    for (size_t I = 0; I < overlap_size; ++I) {
        // Diagonal contribution
        // parallel_timer_on("EWCI:diagonal", omp_get_thread_num());
        double det_energy = as_ints_->energy(ref_dets[I]) + as_ints_->scalar_energy();
        // parallel_timer_off("EWCI:diagonal", omp_get_thread_num());
        // Diagonal contributions
        result_C[I] += tau * (det_energy - S) * ref_C[I];
    }

    //    if (approx_E_flag_) {
    //        timer_on("EWCI:<E>a");
    //        double CHC_energy = 0.0;
    //#pragma omp parallel for reduction(+ : CHC_energy)
    //        for (size_t I = 0; I < overlap_size; ++I) {
    //            CHC_energy += ref_C[I] * result_C[I];
    //        }
    //        CHC_energy = CHC_energy / tau + S + nuclear_repulsion_energy_;
    //        timer_off("EWCI:<E>a");
    //        double CHC_energy_gradient =
    //            (CHC_energy - approx_energy_) / (time_step_ * energy_estimate_freq_);
    //        old_approx_energy_ = approx_energy_;
    //        approx_energy_ = CHC_energy;
    //        approx_E_flag_ = false;
    //        approx_E_tau_ = tau;
    //        approx_E_S_ = S;
    //        if (cycle_ != 0)
    //            outfile->Printf(" %20.12f %10.3e", approx_energy_, CHC_energy_gradient);
    //    }
}

void PCISigmaVector::apply_tau_H_symm_det_dynamic_HBCI_2(
    double tau, double spawning_threshold, const det_hashvec& dets_hashvec,
    const std::vector<double>& pre_C, size_t I, double CI, std::vector<double>& result_C,
    std::vector<std::pair<Determinant, double>>& new_det_C_vec,
    std::pair<double, double>& max_coupling) {

    const Determinant& detI = dets_hashvec[I];
    size_t pre_C_size = pre_C.size();

    bool do_singles_1 = max_coupling.first == 0.0 and
                        std::fabs(dets_single_max_coupling_ * CI) >= spawning_threshold;
    bool do_singles = std::fabs(max_coupling.first * CI) >= spawning_threshold;
    bool do_doubles_1 = max_coupling.second == 0.0 and
                        std::fabs(dets_double_max_coupling_ * CI) >= spawning_threshold;
    bool do_doubles = std::fabs(max_coupling.second * CI) >= spawning_threshold;

    // Diagonal contributions
    // parallel_timer_on("EWCI:diagonal", omp_get_thread_num());
    bool diagonal_flag = false;
    double diagonal_contribution = 0.0;
    // parallel_timer_off("EWCI:diagonal", omp_get_thread_num());

    Determinant detJ(detI);
    if (do_singles) {
        // parallel_timer_on("EWCI:singles", omp_get_thread_num());
        // Generate alpha excitations
        for (size_t x = 0; x < a_couplings_size_; ++x) {
            double HJI_max = std::get<1>(a_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(a_couplings_[x]);
            if (detI.get_alfa_bit(i)) {
                std::vector<std::tuple<int, double>>& sub_couplings = std::get<2>(a_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a;
                    double HJI_bound;
                    std::tie(a, HJI_bound) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI_bound, CI, spawning_threshold)) {
                        break;
                    }
                    if (!detI.get_alfa_bit(a)) {
                        Determinant detJ(detI);

                        double HJI = as_ints_->slater_rules_single_alpha_abs(detJ, i, a);

                        if (prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                            HJI *= detJ.single_excitation_a(i, a);

                            size_t index = dets_hashvec.find(detJ);
                            if (index > I) {
                                if (index >= pre_C_size) {
                                    if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                        new_det_C_vec.push_back(
                                            std::make_pair(detJ, tau * HJI * CI));
                                        diagonal_flag = true;
#pragma omp atomic
                                        num_off_diag_elem_ += 2;
                                    }
                                } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                              spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_flag = true;
                                    diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            }

                            detJ.set_alfa_bit(i, true);
                            detJ.set_alfa_bit(a, false);
                        }
                    }
                }
            }
        }
        // Generate beta excitations
        for (size_t x = 0; x < b_couplings_size_; ++x) {
            double HJI_max = std::get<1>(b_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(b_couplings_[x]);
            if (detI.get_beta_bit(i)) {
                std::vector<std::tuple<int, double>>& sub_couplings = std::get<2>(b_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a;
                    double HJI_bound;
                    std::tie(a, HJI_bound) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI_bound, CI, spawning_threshold)) {
                        break;
                    }
                    if (!detI.get_beta_bit(a)) {
                        Determinant detJ(detI);

                        double HJI = as_ints_->slater_rules_single_beta_abs(detJ, i, a);
                        if (prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                            HJI *= detJ.single_excitation_b(i, a);

                            size_t index = dets_hashvec.find(detJ);
                            if (index > I) {
                                if (index >= pre_C_size) {
                                    if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                        new_det_C_vec.push_back(
                                            std::make_pair(detJ, tau * HJI * CI));
                                        diagonal_flag = true;
#pragma omp atomic
                                        num_off_diag_elem_ += 2;
                                    }
                                } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                              spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_flag = true;
                                    diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            }

                            detJ.set_beta_bit(i, true);
                            detJ.set_beta_bit(a, false);
                        }
                    }
                }
            }
        }
        // parallel_timer_off("EWCI:singles", omp_get_thread_num());
    } else if (do_singles_1) {
        // parallel_timer_on("EWCI:singles", omp_get_thread_num());
        // Generate alpha excitations
        for (size_t x = 0; x < a_couplings_size_; ++x) {
            double HJI_max = std::get<1>(a_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(a_couplings_[x]);
            if (detI.get_alfa_bit(i)) {
                std::vector<std::tuple<int, double>>& sub_couplings = std::get<2>(a_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a;
                    double HJI_bound;
                    std::tie(a, HJI_bound) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI_bound, CI, spawning_threshold)) {
                        break;
                    }
                    if (!detI.get_alfa_bit(a)) {
                        Determinant detJ(detI);

                        double HJI = as_ints_->slater_rules_single_alpha_abs(detJ, i, a);

                        max_coupling.first = std::max(max_coupling.first, std::fabs(HJI));
                        if (prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                            HJI *= detJ.single_excitation_a(i, a);

                            size_t index = dets_hashvec.find(detJ);
                            if (index > I) {
                                if (index >= pre_C_size) {
                                    if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                        new_det_C_vec.push_back(
                                            std::make_pair(detJ, tau * HJI * CI));
                                        diagonal_flag = true;
#pragma omp atomic
                                        num_off_diag_elem_ += 2;
                                    }
                                } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                              spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_flag = true;
                                    diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            }

                            detJ.set_alfa_bit(i, true);
                            detJ.set_alfa_bit(a, false);
                        }
                    }
                }
            }
        }
        // Generate beta excitations
        for (size_t x = 0; x < b_couplings_size_; ++x) {
            double HJI_max = std::get<1>(b_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(b_couplings_[x]);
            if (detI.get_beta_bit(i)) {
                std::vector<std::tuple<int, double>>& sub_couplings = std::get<2>(b_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a;
                    double HJI_bound;
                    std::tie(a, HJI_bound) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI_bound, CI, spawning_threshold)) {
                        break;
                    }
                    if (!detI.get_beta_bit(a)) {
                        Determinant detJ(detI);

                        double HJI = as_ints_->slater_rules_single_beta_abs(detJ, i, a);

                        max_coupling.first = std::max(max_coupling.first, std::fabs(HJI));
                        if (prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                            HJI *= detJ.single_excitation_b(i, a);

                            size_t index = dets_hashvec.find(detJ);
                            if (index > I) {
                                if (index >= pre_C_size) {
                                    if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                        new_det_C_vec.push_back(
                                            std::make_pair(detJ, tau * HJI * CI));
                                        diagonal_flag = true;
#pragma omp atomic
                                        num_off_diag_elem_ += 2;
                                    }
                                } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                              spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_flag = true;
                                    diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            }

                            detJ.set_beta_bit(i, true);
                            detJ.set_beta_bit(a, false);
                        }
                    }
                }
            }
        }
        // parallel_timer_off("EWCI:singles", omp_get_thread_num());
    }

    if (do_doubles) {
        // parallel_timer_on("EWCI:doubles", omp_get_thread_num());
        // Generate alpha-alpha excitations
        for (size_t x = 0; x < aa_couplings_size_; ++x) {
            double HJI_max = std::get<2>(aa_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(aa_couplings_[x]);
            int j = std::get<1>(aa_couplings_[x]);
            if (detI.get_alfa_bit(i) and detI.get_alfa_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(aa_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_alfa_bit(a) or detI.get_alfa_bit(b))) {
                        //                        Determinant detJ(detI);
                        HJI *= detJ.double_excitation_aa(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index >= pre_C_size) {
                                if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                    new_det_C_vec.push_back(std::make_pair(detJ, tau * HJI * CI));
                                    diagonal_flag = true;
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                          spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_flag = true;
                                diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                num_off_diag_elem_ += 2;
                            }
                        }

                        detJ.set_alfa_bit(i, true);
                        detJ.set_alfa_bit(j, true);
                        detJ.set_alfa_bit(a, false);
                        detJ.set_alfa_bit(b, false);
                    }
                }
            }
        }
        // Generate alpha-beta excitations
        for (size_t x = 0; x < ab_couplings_size_; ++x) {
            double HJI_max = std::get<2>(ab_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(ab_couplings_[x]);
            int j = std::get<1>(ab_couplings_[x]);
            if (detI.get_alfa_bit(i) and detI.get_beta_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(ab_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_alfa_bit(a) or detI.get_beta_bit(b))) {
                        //                        Determinant detJ(detI);
                        HJI *= detJ.double_excitation_ab(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index >= pre_C_size) {
                                if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                    new_det_C_vec.push_back(std::make_pair(detJ, tau * HJI * CI));
                                    diagonal_flag = true;
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                          spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_flag = true;
                                diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                num_off_diag_elem_ += 2;
                            }
                        }

                        detJ.set_alfa_bit(i, true);
                        detJ.set_beta_bit(j, true);
                        detJ.set_alfa_bit(a, false);
                        detJ.set_beta_bit(b, false);
                    }
                }
            }
        }
        // Generate beta-beta excitations
        for (size_t x = 0; x < bb_couplings_size_; ++x) {
            double HJI_max = std::get<2>(bb_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(bb_couplings_[x]);
            int j = std::get<1>(bb_couplings_[x]);
            if (detI.get_beta_bit(i) and detI.get_beta_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(bb_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_beta_bit(a) or detI.get_beta_bit(b))) {
                        //                        Determinant detJ(detI);
                        HJI *= detJ.double_excitation_bb(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index >= pre_C_size) {
                                if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                    new_det_C_vec.push_back(std::make_pair(detJ, tau * HJI * CI));
                                    diagonal_flag = true;
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                          spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_flag = true;
                                diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                num_off_diag_elem_ += 2;
                            }
                        }

                        detJ.set_beta_bit(i, true);
                        detJ.set_beta_bit(j, true);
                        detJ.set_beta_bit(a, false);
                        detJ.set_beta_bit(b, false);
                    }
                }
            }
        }
        // parallel_timer_off("EWCI:doubles", omp_get_thread_num());
    } else if (do_doubles_1) {
        // parallel_timer_on("EWCI:doubles", omp_get_thread_num());
        // Generate alpha-alpha excitations
        for (size_t x = 0; x < aa_couplings_size_; ++x) {
            double HJI_max = std::get<2>(aa_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(aa_couplings_[x]);
            int j = std::get<1>(aa_couplings_[x]);
            if (detI.get_alfa_bit(i) and detI.get_alfa_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(aa_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_alfa_bit(a) or detI.get_alfa_bit(b))) {
                        max_coupling.second = std::max(max_coupling.second, std::fabs(HJI));
                        //                        Determinant detJ(detI);
                        HJI *= detJ.double_excitation_aa(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index >= pre_C_size) {
                                if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                    new_det_C_vec.push_back(std::make_pair(detJ, tau * HJI * CI));
                                    diagonal_flag = true;
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                          spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_flag = true;
                                diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                num_off_diag_elem_ += 2;
                            }
                        }

                        detJ.set_alfa_bit(i, true);
                        detJ.set_alfa_bit(j, true);
                        detJ.set_alfa_bit(a, false);
                        detJ.set_alfa_bit(b, false);
                    }
                }
            }
        }
        // Generate alpha-beta excitations
        for (size_t x = 0; x < ab_couplings_size_; ++x) {
            double HJI_max = std::get<2>(ab_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(ab_couplings_[x]);
            int j = std::get<1>(ab_couplings_[x]);
            if (detI.get_alfa_bit(i) and detI.get_beta_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(ab_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_alfa_bit(a) or detI.get_beta_bit(b))) {
                        max_coupling.second = std::max(max_coupling.second, std::fabs(HJI));
                        //                        Determinant detJ(detI);
                        HJI *= detJ.double_excitation_ab(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index >= pre_C_size) {
                                if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                    new_det_C_vec.push_back(std::make_pair(detJ, tau * HJI * CI));
                                    diagonal_flag = true;
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                          spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_flag = true;
                                diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                num_off_diag_elem_ += 2;
                            }
                        }

                        detJ.set_alfa_bit(i, true);
                        detJ.set_beta_bit(j, true);
                        detJ.set_alfa_bit(a, false);
                        detJ.set_beta_bit(b, false);
                    }
                }
            }
        }
        // Generate beta-beta excitations
        for (size_t x = 0; x < bb_couplings_size_; ++x) {
            double HJI_max = std::get<2>(bb_couplings_[x]);
            if (std::fabs(HJI_max * CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(bb_couplings_[x]);
            int j = std::get<1>(bb_couplings_[x]);
            if (detI.get_beta_bit(i) and detI.get_beta_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(bb_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_beta_bit(a) or detI.get_beta_bit(b))) {
                        max_coupling.second = std::max(max_coupling.second, std::fabs(HJI));
                        //                        Determinant detJ(detI);
                        HJI *= detJ.double_excitation_bb(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index >= pre_C_size) {
                                if (important_H_CI_CJ_(HJI, CI, 0.0, spawning_threshold)) {
                                    new_det_C_vec.push_back(std::make_pair(detJ, tau * HJI * CI));
                                    diagonal_flag = true;
#pragma omp atomic
                                    num_off_diag_elem_ += 2;
                                }
                            } else if (important_H_CI_CJ_(HJI, CI, pre_C[index],
                                                          spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_flag = true;
                                diagonal_contribution += tau * HJI * pre_C[index];
#pragma omp atomic
                                num_off_diag_elem_ += 2;
                            }
                        }

                        detJ.set_beta_bit(i, true);
                        detJ.set_beta_bit(j, true);
                        detJ.set_beta_bit(a, false);
                        detJ.set_beta_bit(b, false);
                    }
                }
            }
        }
        // parallel_timer_off("EWCI:doubles", omp_get_thread_num());
    }
    if (diagonal_flag) {
        if (std::fabs(diagonal_contribution) > DBL_MIN) {
#pragma omp atomic
            result_C[I] += diagonal_contribution;
        } else {
#pragma omp atomic
            result_C[I] -= DBL_MIN;
        }
    }
}

void PCISigmaVector::apply_tau_H_ref_C_symm(double tau, double spawning_threshold,
                                            const det_hashvec& result_dets,
                                            const std::vector<double>& ref_C,
                                            const std::vector<double>& pre_C,
                                            std::vector<double>& result_C,
                                            const size_t overlap_size, double S) {

    const size_t result_size = result_dets.size();
    result_C.clear();
    result_C.resize(result_size, 0.0);

#pragma omp parallel for
    for (size_t I = 0; I < overlap_size; ++I) {
        std::pair<double, double> max_coupling;
        max_coupling = dets_max_couplings_[result_dets[I]];
        apply_tau_H_ref_C_symm_det_dynamic_HBCI_2(tau, spawning_threshold, result_dets, pre_C,
                                                  ref_C, I, pre_C[I], ref_C[I], overlap_size,
                                                  result_C, max_coupling);
    }
#pragma omp parallel for
    for (size_t I = 0; I < result_size; ++I) {
        // Diagonal contribution
        // parallel_timer_on("EWCI:diagonal", omp_get_thread_num());
        double det_energy = as_ints_->energy(result_dets[I]) + as_ints_->scalar_energy();
        // parallel_timer_off("EWCI:diagonal", omp_get_thread_num());
        // Diagonal contributions
        result_C[I] += tau * (det_energy - S) * pre_C[I];
    }
}

void PCISigmaVector::apply_tau_H_ref_C_symm_det_dynamic_HBCI_2(
    double tau, double spawning_threshold, const det_hashvec& dets_hashvec,
    const std::vector<double>& pre_C, const std::vector<double>& ref_C, size_t I, double CI,
    double ref_CI, const size_t overlap_size, std::vector<double>& result_C,
    const std::pair<double, double>& max_coupling) {

    const Determinant& detI = dets_hashvec[I];

    bool do_singles = std::fabs(max_coupling.first * ref_CI) >= spawning_threshold;
    bool do_doubles = std::fabs(max_coupling.second * ref_CI) >= spawning_threshold;

    // Diagonal contributions
    // parallel_timer_on("EWCI:diagonal", omp_get_thread_num());
    double diagonal_contribution = 0.0;
    // parallel_timer_off("EWCI:diagonal", omp_get_thread_num());

    Determinant detJ(detI);
    if (do_singles) {
        // parallel_timer_on("EWCI:singles", omp_get_thread_num());
        // Generate alpha excitations
        for (size_t x = 0; x < a_couplings_size_; ++x) {
            double HJI_max = std::get<1>(a_couplings_[x]);
            if (std::fabs(HJI_max * ref_CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(a_couplings_[x]);
            if (detI.get_alfa_bit(i)) {
                std::vector<std::tuple<int, double>>& sub_couplings = std::get<2>(a_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a;
                    double HJI_bound;
                    std::tie(a, HJI_bound) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI_bound, ref_CI, spawning_threshold)) {
                        break;
                    }
                    if (!detI.get_alfa_bit(a)) {
                        Determinant detJ(detI);

                        double HJI = as_ints_->slater_rules_single_alpha_abs(detJ, i, a);

                        if (prescreen_H_CI_(HJI, ref_CI, spawning_threshold)) {
                            HJI *= detJ.single_excitation_a(i, a);

                            size_t index = dets_hashvec.find(detJ);
                            if (index > I) {
                                if (index < overlap_size) {
                                    if (important_H_CI_CJ_(HJI, ref_CI, ref_C[index],
                                                           spawning_threshold)) {
#pragma omp atomic
                                        result_C[index] += tau * HJI * CI;
                                        diagonal_contribution += tau * HJI * pre_C[index];
                                    }
                                } else if (important_H_CI_CJ_(HJI, ref_CI, 0.0,
                                                              spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_contribution += tau * HJI * pre_C[index];
                                }
                            }
                            detJ.set_alfa_bit(i, true);
                            detJ.set_alfa_bit(a, false);
                        }
                    }
                }
            }
        }
        // Generate beta excitations
        for (size_t x = 0; x < b_couplings_size_; ++x) {
            double HJI_max = std::get<1>(b_couplings_[x]);
            if (std::fabs(HJI_max * ref_CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(b_couplings_[x]);
            if (detI.get_beta_bit(i)) {
                std::vector<std::tuple<int, double>>& sub_couplings = std::get<2>(b_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a;
                    double HJI_bound;
                    std::tie(a, HJI_bound) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI_bound, ref_CI, spawning_threshold)) {
                        break;
                    }
                    if (!detI.get_beta_bit(a)) {
                        Determinant detJ(detI);

                        double HJI = as_ints_->slater_rules_single_beta_abs(detJ, i, a);

                        if (prescreen_H_CI_(HJI, ref_CI, spawning_threshold)) {
                            HJI *= detJ.single_excitation_b(i, a);

                            size_t index = dets_hashvec.find(detJ);
                            if (index > I) {
                                if (index < overlap_size) {
                                    if (important_H_CI_CJ_(HJI, ref_CI, ref_C[index],
                                                           spawning_threshold)) {
#pragma omp atomic
                                        result_C[index] += tau * HJI * CI;
                                        diagonal_contribution += tau * HJI * pre_C[index];
                                    }
                                } else if (important_H_CI_CJ_(HJI, ref_CI, 0.0,
                                                              spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_contribution += tau * HJI * pre_C[index];
                                }
                            }
                            detJ.set_beta_bit(i, true);
                            detJ.set_beta_bit(a, false);
                        }
                    }
                }
            }
        }
        // parallel_timer_off("EWCI:singles", omp_get_thread_num());
    }

    if (do_doubles) {
        // parallel_timer_on("EWCI:doubles", omp_get_thread_num());
        // Generate alpha-alpha excitations
        for (size_t x = 0; x < aa_couplings_size_; ++x) {
            double HJI_max = std::get<2>(aa_couplings_[x]);
            if (std::fabs(HJI_max * ref_CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(aa_couplings_[x]);
            int j = std::get<1>(aa_couplings_[x]);
            if (detI.get_alfa_bit(i) and detI.get_alfa_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(aa_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, ref_CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_alfa_bit(a) or detI.get_alfa_bit(b))) {
                        HJI *= detJ.double_excitation_aa(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index < overlap_size) {
                                if (important_H_CI_CJ_(HJI, ref_CI, ref_C[index],
                                                       spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_contribution += tau * HJI * pre_C[index];
                                }
                            } else if (important_H_CI_CJ_(HJI, ref_CI, 0.0, spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_contribution += tau * HJI * pre_C[index];
                            }
                        }
                        detJ.set_alfa_bit(i, true);
                        detJ.set_alfa_bit(j, true);
                        detJ.set_alfa_bit(a, false);
                        detJ.set_alfa_bit(b, false);
                    }
                }
            }
        }
        // Generate alpha-beta excitations
        for (size_t x = 0; x < ab_couplings_size_; ++x) {
            double HJI_max = std::get<2>(ab_couplings_[x]);
            if (std::fabs(HJI_max * ref_CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(ab_couplings_[x]);
            int j = std::get<1>(ab_couplings_[x]);
            if (detI.get_alfa_bit(i) and detI.get_beta_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(ab_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, ref_CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_alfa_bit(a) or detI.get_beta_bit(b))) {
                        HJI *= detJ.double_excitation_ab(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index < overlap_size) {
                                if (important_H_CI_CJ_(HJI, ref_CI, ref_C[index],
                                                       spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_contribution += tau * HJI * pre_C[index];
                                }
                            } else if (important_H_CI_CJ_(HJI, ref_CI, 0.0, spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_contribution += tau * HJI * pre_C[index];
                            }
                        }
                        detJ.set_alfa_bit(i, true);
                        detJ.set_beta_bit(j, true);
                        detJ.set_alfa_bit(a, false);
                        detJ.set_beta_bit(b, false);
                    }
                }
            }
        }
        // Generate beta-beta excitations
        for (size_t x = 0; x < bb_couplings_size_; ++x) {
            double HJI_max = std::get<2>(bb_couplings_[x]);
            if (std::fabs(HJI_max * ref_CI) < spawning_threshold) {
                break;
            }
            int i = std::get<0>(bb_couplings_[x]);
            int j = std::get<1>(bb_couplings_[x]);
            if (detI.get_beta_bit(i) and detI.get_beta_bit(j)) {
                std::vector<std::tuple<int, int, double>>& sub_couplings =
                    std::get<3>(bb_couplings_[x]);
                size_t sub_couplings_size = sub_couplings.size();
                for (size_t y = 0; y < sub_couplings_size; ++y) {
                    int a, b;
                    double HJI;
                    std::tie(a, b, HJI) = sub_couplings[y];
                    if (!prescreen_H_CI_(HJI, ref_CI, spawning_threshold)) {
                        break;
                    }
                    if (!(detI.get_beta_bit(a) or detI.get_beta_bit(b))) {
                        HJI *= detJ.double_excitation_bb(i, j, a, b);

                        size_t index = dets_hashvec.find(detJ);
                        if (index > I) {
                            if (index < overlap_size) {
                                if (important_H_CI_CJ_(HJI, ref_CI, ref_C[index],
                                                       spawning_threshold)) {
#pragma omp atomic
                                    result_C[index] += tau * HJI * CI;
                                    diagonal_contribution += tau * HJI * pre_C[index];
                                }
                            } else if (important_H_CI_CJ_(HJI, ref_CI, 0.0, spawning_threshold)) {
#pragma omp atomic
                                result_C[index] += tau * HJI * CI;
                                diagonal_contribution += tau * HJI * pre_C[index];
                            }
                        }
                        detJ.set_beta_bit(i, true);
                        detJ.set_beta_bit(j, true);
                        detJ.set_beta_bit(a, false);
                        detJ.set_beta_bit(b, false);
                    }
                }
            }
        }
        // parallel_timer_off("EWCI:doubles", omp_get_thread_num());
    }
#pragma omp atomic
    result_C[I] += diagonal_contribution;
}
}