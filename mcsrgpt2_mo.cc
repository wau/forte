#include <libqt/qt.h>
#include <string>
#include <numeric>
#include <algorithm>
#include <ctype.h>
#include "tensor_basic.h"
#include "tensor_blocked.h"
#include "mcsrgpt2_mo.h"

#define Delta(i,j) ((i==j) ? 1 : 0)

using namespace std;

namespace psi{ namespace main{

MCSRGPT2_MO::MCSRGPT2_MO(Options &options) : FCI_MO(options)
{
    fprintf(outfile,"\n");
    fprintf(outfile,"\n  **************************************************");
    fprintf(outfile,"\n  *        Similarity Renormalization Group        *");
    fprintf(outfile,"\n  *       Second-Order Perturbative Analysis       *");
    fprintf(outfile,"\n  *                                                *");
    fprintf(outfile,"\n  *                 by Chenyang Li                 *");
    fprintf(outfile,"\n  **************************************************");
    fprintf(outfile,"\n");

    startup(options);

    compute_energy();

}

MCSRGPT2_MO::~MCSRGPT2_MO()
{
    cleanup();
}

void MCSRGPT2_MO::cleanup(){
    delete integral_;
}

void MCSRGPT2_MO::startup(Options &options){

    // DSRG Parameters
    s_ = options.get_double("S_PARAMETER");
    if(s_ < 0){
        fprintf(outfile, "\n  S parameter for DSRG must >= 0!");
        exit(1);
    }
    taylor_threshold_ = options.get_int("TAYLOR_THRESHOLD");
    if(taylor_threshold_ <= 0){
        fprintf(outfile, "\n  Threshold for Taylor expansion must be an integer greater than 0!");
        exit(1);
    }
    int e_conv = options.get_int("E_CONV");
    taylor_order_ = int(0.5 * (e_conv / taylor_threshold_ + 1)) + 1;

    fprintf(outfile, "\n  Subspace Indices:");
    print_idx("Core", idx_c_);
    print_idx("Active", idx_a_);
    print_idx("Virtual", idx_v_);
    print_idx("Hole", idx_h_);
    print_idx("Particle", idx_p_);


    // Compute Reference Energy
    compute_ref();

    // Form T Amplitudes
    T2aa_ = d4(nh_, d3(nh_, d2(npt_, d1(npt_))));
    T2ab_ = d4(nh_, d3(nh_, d2(npt_, d1(npt_))));
    T2bb_ = d4(nh_, d3(nh_, d2(npt_, d1(npt_))));
    Form_T2(T2aa_,T2ab_,T2bb_);

    T1a_ = d2(nh_, d1(npt_));
    T1b_ = d2(nh_, d1(npt_));
    Form_T1(T1a_,T1b_);

    // Check T Amplitudes
    int ntamp = options.get_int("NTAMP");
    T2Naa_ = 0.0, T2Nab_ = 0.0, T2Nbb_ = 0.0;
    T2Maxaa_ = vector<tuple<double, size_t, size_t, size_t, size_t>> (ntamp, make_tuple(0.0, 0, 0, 0, 0));
    T2Maxab_ = vector<tuple<double, size_t, size_t, size_t, size_t>> (ntamp, make_tuple(0.0, 0, 0, 0, 0));
    T2Maxbb_ = vector<tuple<double, size_t, size_t, size_t, size_t>> (ntamp, make_tuple(0.0, 0, 0, 0, 0));
    Check_T2(T2aa_,T2Naa_,T2Maxaa_,ntamp,0);
    Check_T2(T2ab_,T2Nab_,T2Maxab_,ntamp,1);
    Check_T2(T2bb_,T2Nbb_,T2Maxbb_,ntamp,2);

    T1Na_ = 0.0, T1Nb_ = 0.0;
    T1Maxa_ = vector<tuple<double, size_t, size_t>> (ntamp, make_tuple(0.0, 0, 0));
    T1Maxb_ = vector<tuple<double, size_t, size_t>> (ntamp, make_tuple(0.0, 0, 0));
    Check_T1(T1a_,T1Na_,T1Maxa_,ntamp,0);
    Check_T1(T1b_,T1Nb_,T1Maxb_,ntamp,1);

    bool dsrgpt = options.get_bool("DSRGPT");

    // Effective Fock Matrix
    Fa_dsrg_ = d2(nmo_, d1(nmo_));
    Fb_dsrg_ = d2(nmo_, d1(nmo_));
    Form_Fock_DSRG(Fa_dsrg_,Fb_dsrg_,dsrgpt);

    // Effective Two Electron Integrals
    vaa_dsrg_ = d4(nmo_, d3(nmo_, d2(nmo_, d1(nmo_))));
    vab_dsrg_ = d4(nmo_, d3(nmo_, d2(nmo_, d1(nmo_))));
    vbb_dsrg_ = d4(nmo_, d3(nmo_, d2(nmo_, d1(nmo_))));
    Form_APTEI_DSRG(vaa_dsrg_,vab_dsrg_,vbb_dsrg_,dsrgpt);
}

void MCSRGPT2_MO::Form_Fock_DSRG(d2 &A, d2 &B, const bool &dsrgpt){
    timer_on("Fock_DSRG");
    for(size_t p=0; p<nmo_; ++p){
        for(size_t q=0; q<nmo_; ++q){            
            A[p][q] = Fa_[p][q];
            B[p][q] = Fb_[p][q];
        }
    }
    if(dsrgpt){
        for(size_t i=0; i<nh_; ++i){
            size_t ni = idx_h_[i];
            for(size_t a=0; a<npt_; ++a){
                size_t na = idx_p_[a];
                double value_a = 0.0, value_b = 0.0;
                for(size_t u=0; u<na_; ++u){
                    size_t nu = idx_a_[u];
                    for(size_t x=0; x<na_; ++x){
                        size_t nx = idx_a_[x];
                        value_a += (Fa_[nx][nx] - Fa_[nu][nu]) * T2aa_[i][u][a][x] * Da_[nx][nu];
                        value_a += (Fb_[nx][nx] - Fb_[nu][nu]) * T2ab_[i][u][a][x] * Db_[nx][nu];
                        value_b += (Fa_[nx][nx] - Fa_[nu][nu]) * T2ab_[u][i][x][a] * Da_[nx][nu];
                        value_b += (Fb_[nx][nx] - Fb_[nu][nu]) * T2bb_[i][u][a][x] * Db_[nx][nu];
                    }
                }
                value_a += Fa_[na][ni];
                value_b += Fb_[na][ni];
                double da = Fa_[ni][ni] - Fa_[na][na];
                double db = Fb_[ni][ni] - Fb_[na][na];
                A[ni][na] += value_a * exp(-s_ * da * da);
                B[ni][na] += value_b * exp(-s_ * db * db);
                A[na][ni] += value_a * exp(-s_ * da * da);
                B[na][ni] += value_b * exp(-s_ * db * db);
            }
        }
    }
    timer_off("Fock_DSRG");
}

void MCSRGPT2_MO::Form_APTEI_DSRG(d4 &AA, d4 &AB, d4 &BB, const bool &dsrgpt){
    timer_on("APTEI_DSRG");
    for(size_t p=0; p<nmo_; ++p){
        for(size_t q=0; q<nmo_; ++q){
            for(size_t r=0; r<nmo_; ++r){
                for(size_t s=0; s<nmo_; ++s){
                    AA[p][q][r][s] = integral_->aptei_aa(p,q,r,s);
                    AB[p][q][r][s] = integral_->aptei_ab(p,q,r,s);
                    BB[p][q][r][s] = integral_->aptei_bb(p,q,r,s);
                }
            }
        }
    }
    if(dsrgpt){
        for(size_t i=0; i<nh_; ++i){
            size_t ni = idx_h_[i];
            for(size_t j=0; j<nh_; ++j){
                size_t nj = idx_h_[j];
                for(size_t a=0; a<npt_; ++a){
                    size_t na = idx_p_[a];
                    for(size_t b=0; b<npt_; ++b){
                        size_t nb = idx_p_[b];
                        double daa = Fa_[ni][ni] + Fa_[nj][nj] - Fa_[na][na] - Fa_[nb][nb];
                        double dab = Fa_[ni][ni] + Fb_[nj][nj] - Fa_[na][na] - Fb_[nb][nb];
                        double dbb = Fb_[ni][ni] + Fb_[nj][nj] - Fb_[na][na] - Fb_[nb][nb];
                        AA[ni][nj][na][nb] *= (1 + exp(-s_ * daa * daa));
                        AB[ni][nj][na][nb] *= (1 + exp(-s_ * dab * dab));
                        BB[ni][nj][na][nb] *= (1 + exp(-s_ * dbb * dbb));
                        AA[na][nb][ni][nj] *= (1 + exp(-s_ * daa * daa));
                        AB[na][nb][ni][nj] *= (1 + exp(-s_ * dab * dab));
                        BB[na][nb][ni][nj] *= (1 + exp(-s_ * dbb * dbb));
                    }
                }
            }
        }
    }
    timer_off("APTEI_DSRG");
}

void MCSRGPT2_MO::compute_ref(){
    timer_on("Compute Ref");
    Eref_ = 0.0;
    for(size_t p=0; p<nh_; ++p){
        size_t np = idx_h_[p];
        for(size_t q=0; q<nh_; ++q){
            size_t nq = idx_h_[q];
            Eref_ += (integral_->oei_a(nq,np) + Fa_[nq][np]) * Da_[np][nq];
            Eref_ += (integral_->oei_b(nq,np) + Fb_[nq][np]) * Db_[np][nq];
        }
    }
    Eref_ *= 0.5;
    for(size_t p=0; p<na_; ++p){
        size_t np = idx_a_[p];
        for(size_t q=0; q<na_; ++q){
            size_t nq = idx_a_[q];
            for(size_t r=0; r<na_; ++r){
                size_t nr = idx_a_[r];
                for(size_t s=0; s<na_; ++s){
                    size_t ns = idx_a_[s];
                    Eref_ += 0.25 * integral_->aptei_aa(np,nq,nr,ns) * L2aa_[p][q][r][s];
                    Eref_ += 0.25 * integral_->aptei_bb(np,nq,nr,ns) * L2bb_[p][q][r][s];
                    Eref_ += integral_->aptei_ab(np,nq,nr,ns) * L2ab_[p][q][r][s];
                }
            }
        }
    }
    Eref_ += e_nuc_;
//    fprintf(outfile, "\n    E0 (cumulant) %15c = %22.15f", ' ', Eref_);
    timer_off("Compute Ref");
}

void MCSRGPT2_MO::Form_T2(d4 &AA, d4 &AB, d4 &BB){
    timer_on("Form T2");
    for(size_t i=0; i<nh_; ++i){
        size_t ni = idx_h_[i];
        for(size_t j=0; j<nh_; ++j){
            size_t nj = idx_h_[j];
            for(size_t a=0; a<npt_; ++a){
                size_t na = idx_p_[a];
                for(size_t b=0; b<npt_; ++b){
                    size_t nb = idx_p_[b];

                    double Daa = Fa_[ni][ni] + Fa_[nj][nj] - Fa_[na][na] - Fa_[nb][nb];
                    double Dab = Fa_[ni][ni] + Fb_[nj][nj] - Fa_[na][na] - Fb_[nb][nb];
                    double Dbb = Fb_[ni][ni] + Fb_[nj][nj] - Fb_[na][na] - Fb_[nb][nb];

                    double scalar_aa = integral_->aptei_aa(na,nb,ni,nj);
                    double scalar_ab = integral_->aptei_ab(na,nb,ni,nj);
                    double scalar_bb = integral_->aptei_bb(na,nb,ni,nj);

                    double Zaa = sqrt(s_) * Daa;
                    double Zab = sqrt(s_) * Dab;
                    double Zbb = sqrt(s_) * Dbb;

                    if(fabs(Zaa) < pow(0.1,taylor_threshold_)){
                        AA[i][j][a][b] = Taylor_Exp(Zaa,taylor_order_) * sqrt(s_) * scalar_aa;
                    }else{
                        AA[i][j][a][b] = (1 - exp(-1.0 * pow(Zaa, 2.0))) / Zaa * sqrt(s_) * scalar_aa;
                    }

                    if(fabs(Zab) < pow(0.1,taylor_threshold_)){
                        AB[i][j][a][b] = Taylor_Exp(Zab,taylor_order_) * sqrt(s_) * scalar_ab;
                    }else{
                        AB[i][j][a][b] = (1 - exp(-1.0 * pow(Zab, 2.0))) / Zab * sqrt(s_) * scalar_ab;
                    }

                    if(fabs(Zbb) < pow(0.1,taylor_threshold_)){
                        BB[i][j][a][b] = Taylor_Exp(Zbb,taylor_order_) * sqrt(s_) * scalar_bb;
                    }else{
                        BB[i][j][a][b] = (1 - exp(-1.0 * pow(Zbb, 2.0))) / Zbb * sqrt(s_) * scalar_bb;
                    }

                }
            }
        }
    }

    // Zero Internal Excitations
    for(size_t i=0; i<na_; ++i){
        for(size_t j=0; j<na_; ++j){
            for(size_t k=0; k<na_; ++k){
                for(size_t l=0; l<na_; ++l){
                    AA[i][j][k][l] = 0.0;
                    AB[i][j][k][l] = 0.0;
                    BB[i][j][k][l] = 0.0;
                }
            }
        }
    }
    timer_off("Form T2");
}

void MCSRGPT2_MO::Form_T1(d2 &A, d2 &B){
    timer_on("Form T1");
    for(size_t i=0; i<nh_; ++i){
        size_t ni = idx_h_[i];
        for(size_t a=0; a<npt_; ++a){
            size_t na = idx_p_[a];

            double scalar_a = Fa_[ni][na];
            double scalar_b = Fb_[ni][na];

            for(size_t u=0; u<na_; ++u){
                size_t nu = idx_a_[u];
                for(size_t x=0; x<na_; ++x){
                    size_t nx = idx_a_[x];

                    scalar_a += (Fa_[nx][nx] - Fa_[nu][nu]) * T2aa_[i][u][a][x] * Da_[nx][nu];
                    scalar_a += (Fb_[nx][nx] - Fb_[nu][nu]) * T2ab_[i][u][a][x] * Db_[nx][nu];
                    scalar_b += (Fa_[nx][nx] - Fa_[nu][nu]) * T2ab_[u][i][x][a] * Da_[nx][nu];
                    scalar_b += (Fb_[nx][nx] - Fb_[nu][nu]) * T2bb_[i][u][a][x] * Db_[nx][nu];
                }
            }

            double delta_a = Fa_[ni][ni] - Fa_[na][na];
            double delta_b = Fb_[ni][ni] - Fb_[na][na];

            double Za = sqrt(s_) * delta_a;
            double Zb = sqrt(s_) * delta_b;

            A[i][a] = fabs(Za) < pow(0.1,taylor_threshold_) ? Taylor_Exp(Za,taylor_order_) : ((1 - exp(-1.0 * pow(Za, 2.0))) / Za);
            A[i][a] *= (sqrt(s_) * scalar_a);

            B[i][a] = fabs(Zb) < pow(0.1,taylor_threshold_) ? Taylor_Exp(Zb,taylor_order_) : ((1 - exp(-1.0 * pow(Zb, 2.0))) / Zb);
            B[i][a] *= (sqrt(s_) * scalar_b);

        }
    }

    // Zero Internal Excitations
    for(size_t i=0; i<na_; ++i){
        for(size_t j=0; j<na_; ++j){
            A[i][j] = 0.0;
            B[i][j] = 0.0;
        }
    }
    timer_off("Form T1");
}

inline bool ReverseSortT2(const tuple<double, size_t, size_t, size_t, size_t> &lhs, const tuple<double, size_t, size_t, size_t, size_t> &rhs){
    return fabs(get<0>(rhs)) < fabs(get<0>(lhs));
}

void MCSRGPT2_MO::Check_T2(const d4 &M, double &Norm, vector<tuple<double, size_t, size_t, size_t, size_t>> &Max, const int &ntamp, const int &x){
    timer_on("Check T2");
    double value = 0.0;
    size_t count = 0;
    for(size_t i=0; i<nh_; ++i){
        size_t ni = idx_h_[i];
        for(size_t j=0; j<nh_; ++j){
            size_t nj = idx_h_[j];
            for(size_t a=0; a<npt_; ++a){
                size_t na = idx_p_[a];
                for(size_t b=0; b<npt_; ++b){
                    size_t nb = idx_p_[b];
                    value += pow(M[i][j][a][b], 2.0);
                    if(fabs(M[i][j][a][b]) > fabs(get<0>(Max[ntamp-1]))){
                        Max[ntamp-1] = make_tuple(M[i][j][a][b],ni,nj,na,nb);
                    }
                    sort(Max.begin(), Max.end(), ReverseSortT2);
                    if(fabs(M[i][j][a][b]) > pow(0.1,15)) ++count;
                }
            }
        }
    }
    Norm = sqrt(value);

    // Print
    vector<string> vec_string;
    vec_string.push_back("AA");
    vec_string.push_back("AB");
    vec_string.push_back("BB");
    fprintf(outfile, "\n");
    fprintf(outfile, "\n  ==> Largest T2 amplitudes for spin case %s: <==", vec_string[x].c_str());
    if(x == 0) fprintf(outfile, "\n");
    if(x == 1) fprintf(outfile, "\n   %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c", ' ', '_', ' ', '_', ' ', ' ', '_', ' ', '_', ' ', ' ', '_', ' ', '_', ' ');
    if(x == 2) fprintf(outfile, "\n   %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c", '_', '_', '_', '_', ' ', '_', '_', '_', '_', ' ', '_', '_', '_', '_', ' ');
    fprintf(outfile, "\n   %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c", 'i', 'j', 'a', 'b', ' ', 'i', 'j', 'a', 'b', ' ', 'i', 'j', 'a', 'b', ' ');
    fprintf(outfile, "\n  --------------------------------------------------------------------------------");
    for(vector<tuple<double, size_t, size_t, size_t, size_t>>::size_type n = 0; n != Max.size(); ++n){
        if(n%3 == 0) fprintf(outfile, "\n  ");
        fprintf(outfile, "[%3zu %3zu %3zu %3zu] %8.5f ", get<1>(Max[n]), get<2>(Max[n]), get<3>(Max[n]), get<4>(Max[n]), get<0>(Max[n]));
    }
    fprintf(outfile, "\n  --------------------------------------------------------------------------------");
    fprintf(outfile, "\n  Norm of T2%s vector: (nonzero elements: %12zu) %25.15lf.", vec_string[x].c_str(), count, Norm);
    fprintf(outfile, "\n  --------------------------------------------------------------------------------");
    timer_off("Check T2");
}

inline bool ReverseSortT1(const tuple<double, size_t, size_t> &lhs, const tuple<double, size_t, size_t> &rhs){
    return fabs(get<0>(rhs)) < fabs(get<0>(lhs));
}

void MCSRGPT2_MO::Check_T1(const d2 &M, double &Norm, vector<tuple<double, size_t, size_t>> &Max, const int &ntamp, const int &x){
    timer_on("Check T1");
    double value = 0.0;
    size_t count = 0;
    for(size_t i=0; i<nh_; ++i){
        size_t ni = idx_h_[i];
        for(size_t a=0; a<npt_; ++a){
            size_t na = idx_p_[a];
            value += pow(M[i][a], 2.0);
            if(fabs(M[i][a]) > fabs(get<0>(Max[ntamp-1]))){
                Max[ntamp-1] = make_tuple(M[i][a],ni,na);
            }
            sort(Max.begin(), Max.end(), ReverseSortT1);
            if(fabs(M[i][a]) > pow(0.1,15))  ++count;
        }
    }
    Norm = sqrt(value);

    // Print
    vector<string> vec_string;
    vec_string.push_back("A");
    vec_string.push_back("B");
    fprintf(outfile, "\n");
    fprintf(outfile, "\n  ==> Largest T1 amplitudes for spin case %s: <==", vec_string[x].c_str());
    if(x == 0) fprintf(outfile, "\n");
    else fprintf(outfile, "\n   %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c", '_', ' ', '_', ' ', ' ', '_', ' ', '_', ' ', ' ', '_', ' ', '_', ' ', ' ');
    fprintf(outfile, "\n   %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c  %3c %3c %3c %3c %9c", 'i', ' ', 'a', ' ', ' ', 'i', ' ', 'a', ' ', ' ', 'i', ' ', 'a', ' ', ' ');
    fprintf(outfile, "\n  --------------------------------------------------------------------------------");
    for(vector<tuple<double, size_t, size_t>>::size_type n = 0; n != Max.size(); ++n){
        if(n%3 == 0) fprintf(outfile, "\n  ");
        fprintf(outfile, "[%3zu %3c %3zu %3c] %8.5f ", get<1>(Max[n]), ' ', get<2>(Max[n]), ' ', get<0>(Max[n]));
    }
    fprintf(outfile, "\n  --------------------------------------------------------------------------------");
    fprintf(outfile, "\n  Norm of T1%s vector: (nonzero elements: %12zu) %26.15lf.", vec_string[x].c_str(), count, Norm);
    fprintf(outfile, "\n  --------------------------------------------------------------------------------");
    timer_off("Check T1");
}

void MCSRGPT2_MO::compute_energy(){
    timer_on("E_MCDSRGPT2");
    double T1max = get<0>(T1Maxa_[0]), T2max = get<0>(T2Maxaa_[0]);
    if(fabs(T1max) < fabs(get<0>(T1Maxb_[0]))) T1max = get<0>(T1Maxb_[0]);
    if(fabs(T2max) < fabs(get<0>(T2Maxab_[0]))) T2max = get<0>(T2Maxab_[0]);
    if(fabs(T2max) < fabs(get<0>(T2Maxbb_[0]))) T2max = get<0>(T2Maxbb_[0]);
    double T1norm = sqrt(pow(T1Na_,2) + pow(T1Nb_,2));
    double T2norm = sqrt(pow(T2Naa_,2) + 4 * pow(T2Nab_,2) + pow(T2Nbb_,2));

    double E2 = 0.0;
    double E5_1 = 0.0;
    double E5_2 = 0.0;
    double E6_1 = 0.0;
    double E6_2 = 0.0;
    double E7 = 0.0;
    double E8_1 = 0.0;
    double E8_2 = 0.0;
    double E8_3 = 0.0;
    double E10_1 = 0.0;
    double E10_2 = 0.0;

    E_FT1(E2);
    E_VT1_FT2(E6_1,E6_2,E5_1,E5_2);
    E_VT2_2(E7);
    E_VT2_4PP(E8_1);
    E_VT2_4HH(E8_2);
    E_VT2_4PH(E8_3);
    E_VT2_6(E10_1,E10_2);

    double E5 = E5_1 + E5_2;
    double E6 = E6_1 + E6_2;
    double E8 = E8_1 + E8_2 + E8_3;
    double E10 = E10_1 + E10_2;
    double EVT2 = E7 + E8 + E10;

    Ecorr_ = E2 + E5 + E6 + EVT2;
    Etotal_ = Eref_ + Ecorr_;

    // Print
    fprintf(outfile, "\n  ");
    fprintf(outfile, "\n  ==> MC-DSRG-PT2 Energy Summary <==");
    fprintf(outfile, "\n  ");
    fprintf(outfile, "\n    E0 (cumulant) %15c = %22.15f", ' ', Eref_);
    fprintf(outfile, "\n    E([F, T1]) %18c = %22.15lf", ' ', E2);
    fprintf(outfile, "\n    E([V, T1]) %18c = %22.15lf", ' ', E5);
    fprintf(outfile, "\n    E([V, T1]: V) %15c = %22.15lf", ' ', E5_1);
    fprintf(outfile, "\n    E([V, T1]: C) %15c = %22.15lf", ' ', E5_2);
    fprintf(outfile, "\n    E([F, T2]) %18c = %22.15lf", ' ', E6);
    fprintf(outfile, "\n    E([F, T2]: V) %15c = %22.15lf", ' ', E6_1);
    fprintf(outfile, "\n    E([F, T2]: C) %15c = %22.15lf", ' ', E6_2);
    fprintf(outfile, "\n    E([V, T2] C_2^4) %12c = %22.15lf", ' ', E7);
    fprintf(outfile, "\n    E([V, T2] C_2^2 * C_4) %6c = %22.15lf", ' ', E8);
    fprintf(outfile, "\n    E([V, T2] C_2^2 * C_4: PP) %2c = %22.15lf", ' ', E8_1);
    fprintf(outfile, "\n    E([V, T2] C_2^2 * C_4: HH) %2c = %22.15lf", ' ', E8_2);
    fprintf(outfile, "\n    E([V, T2] C_2^2 * C_4: PH) %2c = %22.15lf", ' ', E8_3);
    fprintf(outfile, "\n    E([V, T2] C_2 * C_6) %8c = %22.15lf", ' ', E10);
    fprintf(outfile, "\n    E([V, T2] C_2 * C_6: H) %5c = %22.15lf", ' ', E10_1);
    fprintf(outfile, "\n    E([V, T2] C_2 * C_6: P) %5c = %22.15lf", ' ', E10_2);
    fprintf(outfile, "\n    E([V, T2]) %18c = %22.15lf", ' ', EVT2);
    fprintf(outfile, "\n    E(SRGPT2) %19c = %22.15lf", ' ', Ecorr_);
    fprintf(outfile, "\n  * E(Total) %20c = %22.15lf", ' ', Etotal_);
    fprintf(outfile, "\n    max(T1) %21c = %22.15lf", ' ', T1max);
    fprintf(outfile, "\n    max(T2) %21c = %22.15lf", ' ', T2max);
    fprintf(outfile, "\n    ||T1|| %22c = %22.15lf", ' ', T1norm);
    fprintf(outfile, "\n    ||T2|| %22c = %22.15lf", ' ', T2norm);
    fprintf(outfile, "\n    ");
    timer_off("E_MCDSRGPT2");
}

void MCSRGPT2_MO::E_FT1(double &E){
    timer_on("[F, T1]");
    E = 0.0;
    for(size_t i=0; i<nh_; ++i){
        size_t ni = idx_h_[i];
        for(size_t j=0; j<nh_; ++j){
            size_t nj = idx_h_[j];
            for(size_t a=0; a<npt_; ++a){
                size_t na = idx_p_[a];
                for(size_t b=0; b<npt_; ++b){
                    size_t nb = idx_p_[b];
                    E += Fa_dsrg_[nb][nj] * T1a_[i][a] * Da_[nj][ni] * (Delta(na,nb) - Da_[na][nb]);
                    E += Fb_dsrg_[nb][nj] * T1b_[i][a] * Db_[nj][ni] * (Delta(na,nb) - Db_[na][nb]);
                }
            }
        }
    }
    timer_off("[F, T1]");
}

void MCSRGPT2_MO::E_VT1_FT2(double &EF1, double &EF2, double &EV1, double &EV2){
    timer_on("[F, T2] & [V, T1]");
    EF1 = 0.0;
    EF2 = 0.0;
    EV1 = 0.0;
    EV2 = 0.0;
    for(size_t u=0; u<na_; ++u){
        size_t nu = idx_a_[u];
        for(size_t v=0; v<na_; ++v){
            size_t nv = idx_a_[v];
            for(size_t x=0; x<na_; ++x){
                size_t nx = idx_a_[x];
                for(size_t y=0; y<na_; ++y){
                    size_t ny = idx_a_[y];

                    for(size_t e=0; e<nv_; ++e){
                        size_t ne = idx_v_[e];
                        size_t te = e + na_;
                        EV1 += vaa_dsrg_[nx][ny][ne][nv] * T1a_[u][te] * L2aa_[x][y][u][v];
                        EV1 += vbb_dsrg_[nx][ny][ne][nv] * T1b_[u][te] * L2bb_[x][y][u][v];
                        EV1 += 2 * vab_dsrg_[nx][ny][ne][nv] * T1a_[u][te] * L2ab_[x][y][u][v];
                        EV1 += 2 * vab_dsrg_[ny][nx][nv][ne] * T1b_[u][te] * L2ab_[y][x][v][u];

                        EF1 += Fa_dsrg_[ne][nx] * T2aa_[u][v][te][y] * L2aa_[x][y][u][v];
                        EF1 += Fb_dsrg_[ne][nx] * T2bb_[u][v][te][y] * L2bb_[x][y][u][v];
                        EF1 += 2 * Fa_dsrg_[ne][nx] * T2ab_[u][v][te][y] * L2ab_[x][y][u][v];
                        EF1 += 2 * Fb_dsrg_[ne][nx] * T2ab_[v][u][y][te] * L2ab_[y][x][v][u];
                    }

                    for(size_t m=0; m<nc_; ++m){
                        size_t nm = idx_c_[m];
                        size_t tm = m + na_;
                        EV2 -= vaa_dsrg_[nm][ny][nu][nv] * T1a_[tm][x] * L2aa_[x][y][u][v];
                        EV2 -= vbb_dsrg_[nm][ny][nu][nv] * T1b_[tm][x] * L2bb_[x][y][u][v];
                        EV2 -= 2 * vab_dsrg_[nm][ny][nu][nv] * T1a_[tm][x] * L2ab_[x][y][u][v];
                        EV2 -= 2 * vab_dsrg_[ny][nm][nv][nu] * T1b_[tm][x] * L2ab_[y][x][v][u];

                        EF2 -= Fa_dsrg_[nv][nm] * T2aa_[u][tm][x][y] * L2aa_[x][y][u][v];
                        EF2 -= Fb_dsrg_[nv][nm] * T2bb_[u][tm][x][y] * L2bb_[x][y][u][v];
                        EF2 -= 2 * Fa_dsrg_[nv][nm] * T2ab_[tm][u][y][x] * L2ab_[y][x][v][u];
                        EF2 -= 2 * Fb_dsrg_[nv][nm] * T2ab_[u][tm][x][y] * L2ab_[x][y][u][v];
                    }
                }
            }
        }
    }
    EV1 *= 0.5;
    EV2 *= 0.5;
    EF1 *= 0.5;
    EF2 *= 0.5;
    timer_off("[F, T2] & [V, T1]");
}

void MCSRGPT2_MO::E_VT2_2(double &E){
    timer_on("[V, T2] C_2^4");
    E = 0.0;
    d4 C1aa (npt_, d3(npt_, d2(nh_, d1(nh_))));
    d4 C1ab (npt_, d3(npt_, d2(nh_, d1(nh_))));
    d4 C1bb (npt_, d3(npt_, d2(nh_, d1(nh_))));
    for(size_t k=0; k<nh_; ++k){
        size_t nk = idx_h_[k];
        for(size_t l=0; l<nh_; ++l){
            size_t nl = idx_h_[l];
            for(size_t a=0; a<npt_; ++a){
                size_t na = idx_p_[a];
                for(size_t d=0; d<npt_; ++d){
                    size_t nd = idx_p_[d];
                    for(size_t c=0; c<npt_; ++c){
                        size_t nc = idx_p_[c];
                        C1aa[a][d][k][l] += vaa_dsrg_[nk][nl][nc][nd] * (Delta(na,nc) - Da_[na][nc]);
                        C1ab[a][d][k][l] += vab_dsrg_[nk][nl][nc][nd] * (Delta(na,nc) - Da_[na][nc]);
                        C1bb[a][d][k][l] += vbb_dsrg_[nk][nl][nc][nd] * (Delta(na,nc) - Db_[na][nc]);
                    }
                }
            }
        }
    }
    d4 C2aa (npt_, d3(npt_, d2(nh_, d1(nh_))));
    d4 C2ab (npt_, d3(npt_, d2(nh_, d1(nh_))));
    d4 C2bb (npt_, d3(npt_, d2(nh_, d1(nh_))));
    for(size_t k=0; k<nh_; ++k){
        for(size_t l=0; l<nh_; ++l){
            for(size_t a=0; a<npt_; ++a){
                for(size_t b=0; b<npt_; ++b){
                    size_t nb = idx_p_[b];
                    for(size_t d=0; d<npt_; ++d){
                        size_t nd = idx_p_[d];
                        C2aa[a][b][k][l] += C1aa[a][d][k][l] * (Delta(nb,nd) - Da_[nb][nd]);
                        C2ab[a][b][k][l] += C1ab[a][d][k][l] * (Delta(nb,nd) - Db_[nb][nd]);
                        C2bb[a][b][k][l] += C1bb[a][d][k][l] * (Delta(nb,nd) - Db_[nb][nd]);
                    }
                }
            }
        }
    }
    C1aa = d4(npt_, d3(npt_, d2(nh_, d1(nh_))));
    C1ab = d4(npt_, d3(npt_, d2(nh_, d1(nh_))));
    C1bb = d4(npt_, d3(npt_, d2(nh_, d1(nh_))));
    for(size_t b=0; b<npt_; ++b){
        for(size_t l=0; l<nh_; ++l){
            for(size_t a=0; a<npt_; ++a){
                for(size_t i=0; i<nh_; ++i){
                    size_t ni = idx_h_[i];
                    for(size_t k=0; k<nh_; ++k){
                        size_t nk = idx_h_[k];
                        C1aa[a][b][i][l] += C2aa[a][b][k][l] * Da_[nk][ni];
                        C1ab[a][b][i][l] += C2ab[a][b][k][l] * Da_[nk][ni];
                        C1bb[a][b][i][l] += C2bb[a][b][k][l] * Db_[nk][ni];
                    }
                }
            }
        }
    }
    C2aa = d4(npt_, d3(npt_, d2(nh_, d1(nh_))));
    C2ab = d4(npt_, d3(npt_, d2(nh_, d1(nh_))));
    C2bb = d4(npt_, d3(npt_, d2(nh_, d1(nh_))));
    for(size_t a=0; a<npt_; ++a){
        for(size_t b=0; b<npt_; ++b){
            for(size_t i=0; i<nh_; ++i){
                for(size_t j=0; j<nh_; ++j){
                    size_t nj = idx_h_[j];
                    for(size_t l=0; l<nh_; ++l){
                        size_t nl = idx_h_[l];
                        C2aa[a][b][i][j] += C1aa[a][b][i][l] * Da_[nl][nj];
                        C2ab[a][b][i][j] += C1ab[a][b][i][l] * Db_[nl][nj];
                        C2bb[a][b][i][j] += C1bb[a][b][i][l] * Db_[nl][nj];
                    }
                }
            }
        }
    }
    for(size_t i=0; i<nh_; ++i){
        for(size_t j=0; j<nh_; ++j){
            for(size_t a=0; a<npt_; ++a){
                for(size_t b=0; b<npt_; ++b){
                    E += T2aa_[i][j][a][b] * C2aa[a][b][i][j];
                    E += 4 * T2ab_[i][j][a][b] * C2ab[a][b][i][j];
                    E += T2bb_[i][j][a][b] * C2bb[a][b][i][j];
                }
            }
        }
    }
    E *= 0.25;
    timer_off("[V, T2] C_2^4");
}

void MCSRGPT2_MO::E_VT2_4PP(double &E){
    timer_on("[V, T2] C_4 * C_2^2: PP");
    E = 0.0;
    d4 C1aa (npt_, d3(npt_, d2(na_, d1(na_))));
    d4 C1ab (npt_, d3(npt_, d2(na_, d1(na_))));
    d4 C1bb (npt_, d3(npt_, d2(na_, d1(na_))));
    for(size_t x=0; x<na_; ++x){
        size_t nx = idx_a_[x];
        for(size_t y=0; y<na_; ++y){
            size_t ny = idx_a_[y];
            for(size_t d=0; d<npt_; ++d){
                size_t nd = idx_p_[d];
                for(size_t a=0; a<npt_; ++a){
                    size_t na = idx_p_[a];
                    for(size_t c=0; c<npt_; ++c){
                        size_t nc = idx_p_[c];
                        C1aa[a][d][x][y] += vaa_dsrg_[nx][ny][nc][nd] * (Delta(na,nc) - Da_[na][nc]);
                        C1ab[a][d][x][y] += vab_dsrg_[nx][ny][nc][nd] * (Delta(na,nc) - Da_[na][nc]);
                        C1bb[a][d][x][y] += vbb_dsrg_[nx][ny][nc][nd] * (Delta(na,nc) - Db_[na][nc]);
                    }
                }
            }
        }
    }
    d4 C2aa (npt_, d3(npt_, d2(na_, d1(na_))));
    d4 C2ab (npt_, d3(npt_, d2(na_, d1(na_))));
    d4 C2bb (npt_, d3(npt_, d2(na_, d1(na_))));
    for(size_t x=0; x<na_; ++x){
        for(size_t y=0; y<na_; ++y){
            for(size_t a=0; a<npt_; ++a){
                for(size_t b=0; b<npt_; ++b){
                    size_t nb = idx_p_[b];
                    for(size_t d=0; d<npt_; ++d){
                        size_t nd = idx_p_[d];
                        C2aa[a][b][x][y] += C1aa[a][d][x][y] * (Delta(nb,nd) - Da_[nb][nd]);
                        C2ab[a][b][x][y] += C1ab[a][d][x][y] * (Delta(nb,nd) - Db_[nb][nd]);
                        C2bb[a][b][x][y] += C1bb[a][d][x][y] * (Delta(nb,nd) - Db_[nb][nd]);
                    }
                }
            }
        }
    }
    C1aa = d4(npt_, d3(npt_, d2(na_, d1(na_))));
    C1ab = d4(npt_, d3(npt_, d2(na_, d1(na_))));
    C1bb = d4(npt_, d3(npt_, d2(na_, d1(na_))));
    for(size_t u=0; u<na_; ++u){
        for(size_t v=0; v<na_; ++v){
            for(size_t a=0; a<npt_; ++a){
                for(size_t b=0; b<npt_; ++b){
                    for(size_t x=0; x<na_; ++x){
                        for(size_t y=0; y<na_; ++y){
                            C1aa[a][b][u][v] += C2aa[a][b][x][y] * L2aa_[x][y][u][v];
                            C1bb[a][b][u][v] += C2bb[a][b][x][y] * L2bb_[x][y][u][v];
                            C1ab[a][b][u][v] += C2ab[a][b][x][y] * L2ab_[x][y][u][v];
                        }
                    }
                }
            }
        }
    }
    for(size_t u=0; u<na_; ++u){
        for(size_t v=0; v<na_; ++v){
            for(size_t a=0; a<npt_; ++a){
                for(size_t b=0; b<npt_; ++b){
                    E += C1aa[a][b][u][v] * T2aa_[u][v][a][b];
                    E += C1bb[a][b][u][v] * T2bb_[u][v][a][b];
                    E += 8 * C1ab[a][b][u][v] * T2ab_[u][v][a][b];
                }
            }
        }
    }
    E *= 0.125;
    timer_off("[V, T2] C_4 * C_2^2: PP");
}

void MCSRGPT2_MO::E_VT2_4HH(double &E){
    timer_on("[V, T2] C_4 * C_2^2: HH");
    E = 0.0;
    d4 C1aa (na_, d3(na_, d2(nh_, d1(nh_))));
    d4 C1ab (na_, d3(na_, d2(nh_, d1(nh_))));
    d4 C1bb (na_, d3(na_, d2(nh_, d1(nh_))));
    for(size_t u=0; u<na_; ++u){
        size_t nu = idx_a_[u];
        for(size_t v=0; v<na_; ++v){
            size_t nv = idx_a_[v];
            for(size_t l=0; l<nh_; ++l){
                size_t nl = idx_h_[l];
                for(size_t k=0; k<nh_; ++k){
                    size_t nk = idx_h_[k];
                    for(size_t i=0; i<nh_; ++i){
                        size_t ni = idx_h_[i];
                        C1aa[u][v][i][l] += vaa_dsrg_[nk][nl][nu][nv] * Da_[nk][ni];
                        C1ab[u][v][i][l] += vab_dsrg_[nk][nl][nu][nv] * Da_[nk][ni];
                        C1bb[u][v][i][l] += vbb_dsrg_[nk][nl][nu][nv] * Db_[nk][ni];
                    }
                }
            }
        }
    }
    d4 C2aa (na_, d3(na_, d2(nh_, d1(nh_))));
    d4 C2ab (na_, d3(na_, d2(nh_, d1(nh_))));
    d4 C2bb (na_, d3(na_, d2(nh_, d1(nh_))));
    for(size_t u=0; u<na_; ++u){
        for(size_t v=0; v<na_; ++v){
            for(size_t i=0; i<nh_; ++i){
                for(size_t l=0; l<nh_; ++l){
                    size_t nl = idx_h_[l];
                    for(size_t j=0; j<nh_; ++j){
                        size_t nj = idx_h_[j];
                        C2aa[u][v][i][j] += C1aa[u][v][i][l] * Da_[nl][nj];
                        C2ab[u][v][i][j] += C1ab[u][v][i][l] * Db_[nl][nj];
                        C2bb[u][v][i][j] += C1bb[u][v][i][l] * Db_[nl][nj];
                    }
                }
            }
        }
    }
    C1aa = d4(na_, d3(na_, d2(nh_, d1(nh_))));
    C1ab = d4(na_, d3(na_, d2(nh_, d1(nh_))));
    C1bb = d4(na_, d3(na_, d2(nh_, d1(nh_))));
    for(size_t x=0; x<na_; ++x){
        for(size_t y=0; y<na_; ++y){
            for(size_t i=0; i<nh_; ++i){
                for(size_t j=0; j<nh_; ++j){
                    for(size_t u=0; u<na_; ++u){
                        for(size_t v=0; v<na_; ++v){
                            C1aa[x][y][i][j] += C2aa[u][v][i][j] * L2aa_[x][y][u][v];
                            C1ab[x][y][i][j] += C2ab[u][v][i][j] * L2ab_[x][y][u][v];
                            C1bb[x][y][i][j] += C2bb[u][v][i][j] * L2bb_[x][y][u][v];
                        }
                    }
                }
            }
        }
    }
    for(size_t x=0; x<na_; ++x){
        for(size_t y=0; y<na_; ++y){
            for(size_t i=0; i<nh_; ++i){
                for(size_t j=0; j<nh_; ++j){
                    E += C1aa[x][y][i][j] * T2aa_[i][j][x][y];
                    E += 8 * C1ab[x][y][i][j] * T2ab_[i][j][x][y];
                    E += C1bb[x][y][i][j] * T2bb_[i][j][x][y];
                }
            }
        }
    }
    E *= 0.125;
    timer_off("[V, T2] C_4 * C_2^2: HH");
}

void MCSRGPT2_MO::E_VT2_4PH(double &E){
    timer_on("[V, T2] C_4 * C_2^2: PH");
    E = 0.0;
    d4 C11 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C12 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C13 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C14 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C19 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C110 (na_, d3(npt_, d2(nh_, d1(na_))));
    for(size_t x=0; x<na_; ++x){
        size_t nx = idx_a_[x];
        for(size_t v=0; v<na_; ++v){
            size_t nv = idx_a_[v];
            for(size_t j=0; j<nh_; ++j){
                size_t nj = idx_h_[j];
                for(size_t a=0; a<npt_; ++a){
                    size_t na = idx_p_[a];
                    for(size_t b=0; b<npt_; ++b){
                        size_t nb = idx_p_[b];
                        C11[v][a][j][x] += vaa_dsrg_[nj][nx][nv][nb] * (Delta(na,nb) - Da_[na][nb]);
                        C12[v][a][j][x] -= vab_dsrg_[nx][nj][nv][nb] * (Delta(na,nb) - Db_[na][nb]);
                        C13[v][a][j][x] += vbb_dsrg_[nj][nx][nv][nb] * (Delta(na,nb) - Db_[na][nb]);
                        C14[v][a][j][x] -= vab_dsrg_[nj][nx][nb][nv] * (Delta(na,nb) - Da_[na][nb]);
                        C19[v][a][j][x] += vab_dsrg_[nx][nj][nb][nv] * (Delta(na,nb) - Da_[na][nb]);
                        C110[v][a][j][x] += vab_dsrg_[nj][nx][nv][nb] * (Delta(na,nb) - Db_[na][nb]);
                    }
                }
            }
        }
    }
    d4 C21 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C22 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C23 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C24 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C29 (na_, d3(npt_, d2(nh_, d1(na_))));
    d4 C210 (na_, d3(npt_, d2(nh_, d1(na_))));
    for(size_t x=0; x<na_; ++x){
        for(size_t v=0; v<na_; ++v){
            for(size_t a=0; a<npt_; ++a){
                for(size_t i=0; i<nh_; ++i){
                    size_t ni = idx_h_[i];
                    for(size_t j=0; j<nh_; ++j){
                        size_t nj = idx_h_[j];
                        C21[v][a][i][x] += C11[v][a][j][x] * Da_[nj][ni];
                        C22[v][a][i][x] += C12[v][a][j][x] * Db_[nj][ni];
                        C23[v][a][i][x] += C13[v][a][j][x] * Db_[nj][ni];
                        C24[v][a][i][x] += C14[v][a][j][x] * Da_[nj][ni];
                        C29[v][a][i][x] += C19[v][a][j][x] * Db_[nj][ni];
                        C210[v][a][i][x] += C110[v][a][j][x] * Da_[nj][ni];
                    }
                }
            }
        }
    }
    d4 C31 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C32 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C33 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C34 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C35 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C36 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C37 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C38 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C39 (na_, d3(na_, d2(na_, d1(na_))));
    d4 C310 (na_, d3(na_, d2(na_, d1(na_))));
    for(size_t u=0; u<na_; ++u){
        for(size_t y=0; y<na_; ++y){
            for(size_t x=0; x<na_; ++x){
                for(size_t v=0; v<na_; ++v){
                    for(size_t i=0; i<nh_; ++i){
                        for(size_t a=0; a<npt_; ++a){
                            C31[u][v][x][y] += C21[v][a][i][x] * T2aa_[i][u][a][y];
                            C32[u][v][x][y] += C22[v][a][i][x] * T2ab_[u][i][y][a];
                            C33[u][v][x][y] += C23[v][a][i][x] * T2bb_[i][u][a][y];
                            C34[u][v][x][y] += C24[v][a][i][x] * T2ab_[i][u][a][y];
                            C35[u][v][x][y] += C21[v][a][i][x] * T2ab_[i][u][a][y];
                            C36[u][v][x][y] += C22[v][a][i][x] * T2bb_[i][u][a][y];
                            C37[u][v][x][y] += C24[v][a][i][x] * T2aa_[i][u][a][y];
                            C38[u][v][x][y] += C23[v][a][i][x] * T2ab_[u][i][y][a];
                            C39[u][v][x][y] -= C29[v][a][i][x] * T2ab_[u][i][a][y];
                            C310[u][v][x][y] -= C210[v][a][i][x] * T2ab_[i][u][y][a];
                        }
                    }
                }
            }
        }
    }
    for(size_t u=0; u<na_; ++u){
        for(size_t v=0; v<na_; ++v){
            for(size_t x=0; x<na_; ++x){
                for(size_t y=0; y<na_; ++y){
                    E += C31[u][v][x][y] * L2aa_[x][y][u][v];
                    E += C32[u][v][x][y] * L2aa_[x][y][u][v];
                    E += C33[u][v][x][y] * L2bb_[x][y][u][v];
                    E += C34[u][v][x][y] * L2bb_[x][y][u][v];
                    E -= C35[u][v][x][y] * L2ab_[x][y][v][u];
                    E -= C36[u][v][x][y] * L2ab_[x][y][v][u];
                    E -= C37[u][v][x][y] * L2ab_[y][x][u][v];
                    E -= C38[u][v][x][y] * L2ab_[y][x][u][v];
                    E += C39[u][v][x][y] * L2ab_[x][y][u][v];
                    E += C310[u][v][x][y] * L2ab_[y][x][v][u];
                }
            }
        }
    }
    timer_off("[V, T2] C_4 * C_2^2: PH");
}

void MCSRGPT2_MO::E_VT2_6(double &E1, double &E2){
    timer_on("[V, T2] C_6 * C_2");
    E1 = 0.0;
    E2 = 0.0;
    for(size_t u=0; u<na_; ++u){
        size_t nu = idx_a_[u];
        for(size_t v=0; v<na_; ++v){
            size_t nv = idx_a_[v];
            for(size_t w=0; w<na_; ++w){
                size_t nw = idx_a_[w];
                for(size_t x=0; x<na_; ++x){
                    size_t nx = idx_a_[x];
                    for(size_t y=0; y<na_; ++y){
                        size_t ny = idx_a_[y];
                        for(size_t z=0; z<na_; ++z){
                            size_t nz = idx_a_[z];
                            for(size_t i=0; i<nh_; ++i){
                                size_t ni = idx_h_[i];
                                // L3aaa & L3bbb
                                E1 += vaa_dsrg_[ni][nz][nu][nv] * T2aa_[i][w][x][y] * L3aaa_[x][y][z][u][v][w];
                                E1 += vbb_dsrg_[ni][nz][nu][nv] * T2bb_[i][w][x][y] * L3bbb_[x][y][z][u][v][w];
                                // L3aab
                                E1 -= 2 * L3aab_[x][z][y][u][v][w] * T2ab_[i][w][x][y] * vaa_dsrg_[ni][nz][nu][nv];
                                // L3aba & L3baa
                                E1 -= 2 * L3aab_[x][y][z][u][w][v] * T2aa_[i][w][x][y] * vab_dsrg_[ni][nz][nu][nv];
                                E1 += 4 * L3aab_[x][z][y][u][w][v] * T2ab_[w][i][x][y] * vab_dsrg_[nz][ni][nu][nv];
                                // L3abb & L3bab
                                E1 += 4 * L3abb_[x][y][z][u][v][w] * T2ab_[i][w][x][y] * vab_dsrg_[ni][nz][nu][nv];
                                E1 -= 2 * L3abb_[z][x][y][u][v][w] * T2bb_[i][w][x][y] * vab_dsrg_[nz][ni][nu][nv];
                                // L3bba
                                E1 -= 2 * L3abb_[x][y][z][w][u][v] * T2ab_[w][i][x][y] * vbb_dsrg_[ni][nz][nu][nv];
                            }
                            for(size_t a=0; a<npt_; ++a){
                                size_t na = idx_p_[a];
                                // L3aaa & L3bbb
                                E2 += vaa_dsrg_[nx][ny][nw][na] * T2aa_[u][v][a][z] * L3aaa_[x][y][z][u][v][w];
                                E2 += vbb_dsrg_[nx][ny][nw][na] * T2bb_[u][v][a][z] * L3bbb_[x][y][z][u][v][w];
                                // L3aab
                                E2 += 2 * L3aab_[x][z][y][u][v][w] * T2aa_[u][v][a][z] * vab_dsrg_[nx][ny][na][nw];
                                // L3aba & L3baa
                                E2 -= 4 * L3aab_[x][z][y][u][w][v] * T2ab_[u][v][z][a] * vab_dsrg_[nx][ny][nw][na];
                                E2 -= 2 * L3aab_[x][y][z][u][w][v] * T2ab_[u][v][a][z] * vaa_dsrg_[nx][ny][nw][na];
                                // L3abb & L3bab
                                E2 -= 4 * L3abb_[x][y][z][u][v][w] * T2ab_[u][v][a][z] * vab_dsrg_[nx][ny][na][nw];
                                E2 -= 2 * L3abb_[z][x][y][u][v][w] * T2ab_[u][v][z][a] * vbb_dsrg_[nx][ny][nw][na];
                                // L3bba
                                E2 += 2 * L3abb_[x][y][z][w][u][v] * T2bb_[u][v][a][z] * vab_dsrg_[nx][ny][nw][na];
                            }
                        }
                    }
                }
            }
        }
    }
    E1 *= 0.25;
    E2 *= 0.25;
    timer_off("[V, T2] C_6 * C_2");
}

}}
