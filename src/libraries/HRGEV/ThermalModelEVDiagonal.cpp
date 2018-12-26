/*
 * Thermal-FIST package
 * 
 * Copyright (c) 2014-2018 Volodymyr Vovchenko
 *
 * GNU General Public License (GPLv3 or later)
 */
#include "HRGEV/ThermalModelEVDiagonal.h"

#ifdef USE_OPENMP
#include <omp.h>
#endif

#include <vector>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

#include "HRGBase/xMath.h"
#include "HRGEV/ExcludedVolumeHelper.h"

#include <Eigen/Dense>

using namespace Eigen;

using namespace std;

namespace thermalfist {

  ThermalModelEVDiagonal::ThermalModelEVDiagonal(ThermalParticleSystem *TPS_, const ThermalModelParameters& params, double RHad_, int mode) :
    ThermalModelBase(TPS_, params)
  {
    m_densitiesid.resize(m_TPS->Particles().size());
    m_v.resize(m_TPS->Particles().size());
    m_Volume = params.V;
    m_TAG = "ThermalModelEVDiagonal";

    m_Ensemble = GCE;
    m_InteractionModel = DiagonalEV;
  }


  ThermalModelEVDiagonal::~ThermalModelEVDiagonal(void)
  {
  }


  void ThermalModelEVDiagonal::SetRadius(double rad) {
    if (m_v.size() != m_TPS->Particles().size())
      m_v.resize(m_TPS->Particles().size());
    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      m_v[i] = CuteHRGHelper::vr(rad);
    }
  }

  void ThermalModelEVDiagonal::FillVirial(const std::vector<double>& ri)
  {
    if (ri.size() != m_TPS->Particles().size()) {
      printf("**WARNING** %s::FillVirial(const std::vector<double> & ri): size of ri does not match number of hadrons in the list", m_TAG.c_str());
      return;
    }
    m_v.resize(m_TPS->Particles().size());
    for (int i = 0; i < m_TPS->Particles().size(); ++i)
      m_v[i] = CuteHRGHelper::vr(ri[i]);
  }

  void ThermalModelEVDiagonal::FillVirialEV(const std::vector<double>& vi)
  {
    if (vi.size() != m_TPS->Particles().size()) {
      printf("**WARNING** %s::FillVirialEV(const std::vector<double> & vi): size of vi does not match number of hadrons in the list", m_TAG.c_str());
      return;
    }
    m_v = vi;
  }

  void ThermalModelEVDiagonal::ReadInteractionParameters(const std::string & filename)
  {
    m_v = std::vector<double>(m_TPS->Particles().size(), 0.);

    ifstream fin(filename);
    char cc[2000];
    while (!fin.eof()) {
      fin.getline(cc, 2000);
      string tmp = string(cc);
      vector<string> elems = CuteHRGHelper::split(tmp, '#');
      if (elems.size() < 1)
        continue;
      istringstream iss(elems[0]);
      int pdgid;
      double b;
      if (iss >> pdgid >> b) {
        int ind = m_TPS->PdgToId(pdgid);
        if (ind != -1)
          m_v[ind] = b;
      }
    }
    fin.close();
  }

  void ThermalModelEVDiagonal::WriteInteractionParameters(const std::string & filename)
  {
    ofstream fout(filename);
    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      fout << std::setw(15) << m_TPS->Particle(i).PdgId();
      fout << std::setw(15) << m_v[i];
      fout << std::endl;
    }
    fout.close();
  }

  double ThermalModelEVDiagonal::ExcludedVolume(int i) const
  {
    if (i < 0 || i >= m_v.size())
      return 0.;
    return m_v[i];
  }


  void ThermalModelEVDiagonal::SetParameters(const ThermalModelParameters& params) {
    m_Parameters = params;
    m_Calculated = false;
  }


  void ThermalModelEVDiagonal::ChangeTPS(ThermalParticleSystem *TPS_) {
    ThermalModelBase::ChangeTPS(TPS_);
    m_densitiesid.resize(m_TPS->Particles().size());
    //m_Calculated = false;
  }


  double ThermalModelEVDiagonal::DensityId(int i, double Pressure) {
    double ret = 0.;

    double dMu = -m_v[i] * Pressure;

    return m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::ParticleDensity, m_UseWidth, m_Chem[i], dMu);
  }

  double ThermalModelEVDiagonal::PressureId(int i, double Pressure) {
    double ret = 0.;

    double dMu = -m_v[i] * Pressure;

    return m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::Pressure, m_UseWidth, m_Chem[i], dMu);
  }

  double ThermalModelEVDiagonal::ScaledVarianceId(int i, double Pressure) {
    double ret = 0.;

    double dMu = -m_v[i] * Pressure;

    return m_TPS->Particles()[i].ScaledVariance(m_Parameters, m_UseWidth, m_Chem[i], dMu);
  }

  double ThermalModelEVDiagonal::Pressure(double P) {
    double ret = 0.;

#pragma omp parallel for reduction(+:ret) if(useOpenMP)
    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      double dMu = -m_v[i] * P;
      ret += m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::Pressure, m_UseWidth, m_Chem[i], dMu);
    }

    return ret;
  }

  void ThermalModelEVDiagonal::SolvePressure() {
    BroydenEquationsDEV eqs(this);
    BroydenJacobianDEV jac(this);
    Broyden broydn(&eqs, &jac);

    m_Pressure = Pressure(0.);
    double mnc = pow(m_Parameters.T, 4.) * pow(xMath::GeVtoifm(), 3.);
    eqs.SetMnc(mnc);
    jac.SetMnc(mnc);
    double x0 = log(m_Pressure / mnc);
    std::vector<double> x(1, x0);

    x = broydn.Solve(x, &BroydenSolutionCriteriumDEV(this, 1.0E-8));

    double current_precision = Broyden::TOL;

    double PressureNew = mnc * exp(x[0]);

    // UPDATE: Currently not used
    //// If pressures are too small we may need additional iterations with higher accuracy
    //while (abs(PressureNew) < current_precision && current_precision > 1.e-50 && abs(PressureNew /m_Pressure) < 1.e-5) {
    //  current_precision *= 1.e-10;
    //  x = broydn.Solve(x, &Broyden::BroydenSolutionCriterium(current_precision));
    //  PressureNew = mnc * exp(x[0]);
    //}

    m_Pressure = PressureNew;

    if (broydn.Iterations() == broydn.MaxIterations())
      m_LastCalculationSuccessFlag = false;
    else m_LastCalculationSuccessFlag = true;

    m_MaxDiff = broydn.MaxDifference();
  }

  void ThermalModelEVDiagonal::CalculateDensities() {
    m_FluctuationsCalculated = false;

    SolvePressure();

    m_wnSum = 0.;
    m_Densityid = 0.;
    m_TotalDensity = 0.;
    m_Suppression = 0.;
    double densityid = 0., suppression = 0.;

    m_densitiesidnoshift = m_densitiesid;

#pragma omp parallel for reduction(+:densityid) reduction(+:suppression) if(useOpenMP)
    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      double dMu = -m_v[i] * m_Pressure;
      m_densitiesid[i] = m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::ParticleDensity, m_UseWidth, m_Chem[i], dMu);
      densityid += m_densitiesid[i];
      suppression += m_v[i] * m_densitiesid[i];

      m_densitiesidnoshift[i] = m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::ParticleDensity, m_UseWidth, m_Chem[i], 0.);
    }

    m_Densityid = densityid;
    m_Suppression = suppression;

    m_Suppression = 1. / (1. + m_Suppression);

    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      m_densities[i] = m_densitiesid[i] * m_Suppression;
      m_TotalDensity += m_densities[i];
      m_wnSum += m_densities[i] * m_TPS->Particles()[i].ScaledVariance(m_Parameters, m_UseWidth, m_Chem[i], -m_v[i] * m_Pressure);
    }

    CalculateFeeddown();

    m_Calculated = true;
    ValidateCalculation();
  }

  void ThermalModelEVDiagonal::CalculateTwoParticleCorrelations() {
    int NN = m_densities.size();
    vector<double> tN(NN), tW(NN);
    for (int i = 0; i < NN; ++i) tN[i] = DensityId(i, m_Pressure);
    for (int i = 0; i < NN; ++i) tW[i] = ScaledVarianceId(i, m_Pressure);

    m_PrimCorrel.resize(NN);
    for (int i = 0; i < NN; ++i) m_PrimCorrel[i].resize(NN);
    m_TotalCorrel = m_PrimCorrel;

    for (int i = 0; i < NN; ++i)
      for (int j = 0; j < NN; ++j) {
        m_PrimCorrel[i][j] = 0.;
        if (i == j) m_PrimCorrel[i][j] += m_densities[i] * tW[i];
        m_PrimCorrel[i][j] += -m_v[i] * m_densities[i] * m_densities[j] * tW[i];
        m_PrimCorrel[i][j] += -m_v[j] * m_densities[i] * m_densities[j] * tW[j];
        double tmp = 0.;
        for (int k = 0; k < m_densities.size(); ++k)
          tmp += m_v[k] * m_v[k] * tW[k] * m_densities[k];
        m_PrimCorrel[i][j] += m_densities[i] * m_densities[j] * tmp;
        m_PrimCorrel[i][j] /= m_Parameters.T;
      }

    for (int i = 0; i < NN; ++i) {
      m_wprim[i] = m_PrimCorrel[i][i];
      if (m_densities[i] > 0.) m_wprim[i] *= m_Parameters.T / m_densities[i];
      else m_wprim[i] = 1.;
    }

    CalculateSusceptibilityMatrix();
    CalculateTwoParticleFluctuationsDecays();
    CalculateProxySusceptibilityMatrix();
  }

  // TODO include correlations
  void ThermalModelEVDiagonal::CalculateFluctuations() {
    for (int i = 0; i < m_wprim.size(); ++i) {
      //m_wprim[i] = CalculateParticleScaledVariance(i);
      m_skewprim[i] = CalculateParticleSkewness(i);
      m_kurtprim[i] = CalculateParticleKurtosis(i);
    }
    CalculateTwoParticleCorrelations();
    m_FluctuationsCalculated = true;

    for (int i = 0; i < m_wtot.size(); ++i) {
      double tmp1 = 0., tmp2 = 0., tmp3 = 0., tmp4 = 0.;
      tmp2 = m_densities[i] * m_wprim[i];
      tmp3 = m_densities[i] * m_wprim[i] * m_skewprim[i];
      tmp4 = m_densities[i] * m_wprim[i] * m_kurtprim[i];
      for (int r = 0; r < m_TPS->Particles()[i].DecayContributions().size(); ++r) {
        tmp2 += m_densities[m_TPS->Particles()[i].DecayContributions()[r].second] *
          (m_wprim[m_TPS->Particles()[i].DecayContributions()[r].second] * m_TPS->Particles()[i].DecayContributions()[r].first * m_TPS->Particles()[i].DecayContributions()[r].first
            + m_TPS->Particles()[i].DecayContributionsSigmas()[r].first);

        int rr = m_TPS->Particles()[i].DecayContributions()[r].second;
        double ni = m_TPS->Particles()[i].DecayContributions()[r].first;
        tmp3 += m_densities[rr] * m_wprim[rr] * (m_skewprim[rr] * ni * ni * ni + 3. * ni * m_TPS->Particles()[i].DecayCumulants()[r].first[1]);
        tmp3 += m_densities[rr] * m_TPS->Particles()[i].DecayCumulants()[r].first[2];

        tmp4 += m_densities[rr] * m_wprim[rr] * (m_kurtprim[rr] * ni * ni * ni * ni
          + 6. * m_skewprim[rr] * ni * ni * m_TPS->Particles()[i].DecayCumulants()[r].first[1]
          + 3. * m_TPS->Particles()[i].DecayCumulants()[r].first[1] * m_TPS->Particles()[i].DecayCumulants()[r].first[1]
          + 4. * ni * m_TPS->Particles()[i].DecayCumulants()[r].first[2]);

        tmp4 += m_densities[rr] * m_TPS->Particles()[i].DecayCumulants()[r].first[3];
      }

      tmp1 = m_densitiestotal[i];

      //m_wtot[i] = tmp2 / tmp1;
      m_skewtot[i] = tmp3 / tmp2;
      m_kurttot[i] = tmp4 / tmp2;
    }
  }


  std::vector<double> ThermalModelEVDiagonal::CalculateChargeFluctuations(const std::vector<double>& chgs, int order)
  {
    vector<double> ret(order + 1, 0.);

    // chi1
    for (int i = 0; i < m_densities.size(); ++i)
      ret[0] += chgs[i] * m_densities[i];

    ret[0] /= pow(m_Parameters.T * xMath::GeVtoifm(), 3);

    if (order < 2) return ret;



    // Preparing matrix for system of linear equations
    int NN = m_densities.size();

    vector<double> MuStar(NN, 0.);
    for (int i = 0; i < NN; ++i) {
      MuStar[i] = m_Chem[i] + MuShift(i);
    }


    MatrixXd densMatrix(2 * NN, 2 * NN);
    VectorXd solVector(2 * NN), xVector(2 * NN);

    vector<double> DensitiesId(m_densities.size()), chi2id(m_densities.size());
    for (int i = 0; i < NN; ++i) {
      DensitiesId[i] = m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::ParticleDensity, m_UseWidth, MuStar[i], 0.);
      chi2id[i] = m_TPS->Particles()[i].chi(2, m_Parameters, m_UseWidth, MuStar[i], 0.);
    }

    for (int i = 0; i < NN; ++i)
      for (int j = 0; j < NN; ++j) {
        densMatrix(i, j) = m_v[j] * DensitiesId[i];
        if (i == j) densMatrix(i, j) += 1.;
      }

    for (int i = 0; i < NN; ++i)
      for (int j = 0; j < NN; ++j)
        densMatrix(i, NN + j) = 0.;

    for (int i = 0; i < NN; ++i) {
      densMatrix(i, NN + i) = 0.;
      for (int k = 0; k < NN; ++k) {
        densMatrix(i, NN + i) += m_v[k] * m_densities[k];
      }
      densMatrix(i, NN + i) = (densMatrix(i, NN + i) - 1.) * chi2id[i] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * m_Parameters.T;
    }

    for (int i = 0; i < NN; ++i)
      for (int j = 0; j < NN; ++j) {
        densMatrix(NN + i, NN + j) = m_v[i] * DensitiesId[j];
        if (i == j) densMatrix(NN + i, NN + j) += 1.;
      }


    PartialPivLU<MatrixXd> decomp(densMatrix);

    // chi2
    vector<double> dni(NN, 0.), dmus(NN, 0.);

    for (int i = 0; i < NN; ++i) {
      xVector[i] = 0.;
      xVector[NN + i] = chgs[i];
    }

    solVector = decomp.solve(xVector);

    for (int i = 0; i < NN; ++i) {
      dni[i] = solVector[i];
      dmus[i] = solVector[NN + i];
    }

    for (int i = 0; i < NN; ++i)
      ret[1] += chgs[i] * dni[i];

    ret[1] /= pow(m_Parameters.T, 2) * pow(xMath::GeVtoifm(), 3);

    if (order < 3) return ret;
    // chi3
    vector<double> d2ni(NN, 0.), d2mus(NN, 0.);

    vector<double> chi3id(m_densities.size());
    for (int i = 0; i < NN; ++i)
      chi3id[i] = m_TPS->Particles()[i].chi(3, m_Parameters, m_UseWidth, MuStar[i], 0.);

    for (int i = 0; i < NN; ++i) {
      xVector[i] = 0.;

      double tmp = 0.;
      for (int j = 0; j < NN; ++j) tmp += m_v[j] * dni[j];
      tmp = -2. * tmp * chi2id[i] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * m_Parameters.T * dmus[i];
      xVector[i] += tmp;

      tmp = 0.;
      for (int j = 0; j < NN; ++j) tmp += m_v[j] * m_densities[j];
      tmp = -(tmp - 1.) * chi3id[i] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * dmus[i] * dmus[i];
      xVector[i] += tmp;
    }
    for (int i = 0; i < NN; ++i) {
      xVector[NN + i] = 0.;

      double tmp = 0.;
      for (int j = 0; j < NN; ++j) tmp += -m_v[i] * dmus[j] * chi2id[j] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * m_Parameters.T * dmus[j];

      xVector[NN + i] = tmp;
    }

    solVector = decomp.solve(xVector);

    for (int i = 0; i < NN; ++i) {
      d2ni[i] = solVector[i];
      d2mus[i] = solVector[NN + i];
    }

    for (int i = 0; i < NN; ++i)
      ret[2] += chgs[i] * d2ni[i];

    ret[2] /= m_Parameters.T * pow(xMath::GeVtoifm(), 3);


    if (order < 4) return ret;

    // chi4
    vector<double> d3ni(NN, 0.), d3mus(NN, 0.);

    vector<double> chi4id(m_densities.size());
    for (int i = 0; i < NN; ++i)
      chi4id[i] = m_TPS->Particles()[i].chi(4, m_Parameters, m_UseWidth, MuStar[i], 0.);

    vector<double> dnis(NN, 0.);
    for (int i = 0; i < NN; ++i) {
      dnis[i] = chi2id[i] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * m_Parameters.T * dmus[i];
    }

    vector<double> d2nis(NN, 0.);
    for (int i = 0; i < NN; ++i) {
      d2nis[i] = chi3id[i] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * dmus[i] * dmus[i] +
        chi2id[i] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * m_Parameters.T * d2mus[i];
    }

    for (int i = 0; i < NN; ++i) {
      xVector[i] = 0.;

      double tmp = 0.;
      for (int j = 0; j < NN; ++j) tmp += m_v[j] * dni[j];
      tmp = -3. * tmp * d2nis[i];
      xVector[i] += tmp;

      tmp = 0.;
      for (int j = 0; j < NN; ++j) tmp += m_v[j] * d2ni[j];
      tmp = -3. * tmp * dnis[i];
      xVector[i] += tmp;

      double tmps = 0.;
      for (int j = 0; j < NN; ++j) tmps += m_v[j] * m_densities[j];

      tmp = -(tmps - 1.) * chi3id[i] * pow(xMath::GeVtoifm(), 3) * m_Parameters.T * d2mus[i] * 3. * dmus[i];
      xVector[i] += tmp;

      tmp = -(tmps - 1.) * chi4id[i] * pow(xMath::GeVtoifm(), 3) * dmus[i] * dmus[i] * dmus[i];
      xVector[i] += tmp;
    }
    for (int i = 0; i < NN; ++i) {
      xVector[NN + i] = 0.;

      double tmp = 0.;
      for (int j = 0; j < NN; ++j) tmp += -2. * m_v[i] * d2mus[j] * dnis[j];
      xVector[NN + i] += tmp;

      tmp = 0.;
      for (int j = 0; j < NN; ++j) tmp += -m_v[i] * dmus[j] * d2nis[j];
      xVector[NN + i] += tmp;
    }

    solVector = decomp.solve(xVector);

    for (int i = 0; i < NN; ++i) {
      d3ni[i] = solVector[i];
      d3mus[i] = solVector[NN + i];
    }

    for (int i = 0; i < NN; ++i)
      ret[3] += chgs[i] * d3ni[i];

    ret[3] /= pow(xMath::GeVtoifm(), 3);

    return ret;
  }


  double ThermalModelEVDiagonal::CalculateEnergyDensity() {
    if (!m_Calculated) CalculateDensities();
    double ret = 0.;
    double dMu = 0.;
    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      dMu = -m_v[i] * m_Pressure;
      ret += m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::EnergyDensity, m_UseWidth, m_Chem[i], dMu);
    }
    return ret * m_Suppression;
  }

  double ThermalModelEVDiagonal::CalculateEntropyDensity() {
    if (!m_Calculated) CalculateDensities();
    double ret = 0.;
    double dMu = 0.;
    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      dMu = -m_v[i] * m_Pressure;
      ret += m_TPS->Particles()[i].Density(m_Parameters, IdealGasFunctions::EntropyDensity, m_UseWidth, m_Chem[i], dMu);
    }
    return ret * m_Suppression;
  }

  // Dummy
  double ThermalModelEVDiagonal::CalculateBaryonMatterEntropyDensity() {
    double ret = 0.;
    return ret;
  }
  double ThermalModelEVDiagonal::CalculateMesonMatterEntropyDensity() {
    double ret = 0.;
    return ret;
  }

  double ThermalModelEVDiagonal::CalculatePressure() {
    if (!m_Calculated) CalculateDensities();
    return m_Pressure;
  }

  // TODO: Properly for multi-component
  double ThermalModelEVDiagonal::CalculateHadronScaledVariance() {
    return 1.;
  }

  // TODO: Properly for multi-component
  double ThermalModelEVDiagonal::CalculateParticleScaledVariance(int part) {
    return 1.;
  }

  // TODO: properly with excluded volume
  double ThermalModelEVDiagonal::CalculateParticleSkewness(int part) {
    double dMu = -m_v[part] * m_Pressure;
    return m_TPS->Particles()[part].Skewness(m_Parameters, m_UseWidth, m_Chem[part], dMu);
  }

  // TODO: properly with excluded volume
  double ThermalModelEVDiagonal::CalculateParticleKurtosis(int part) {
    double dMu = -m_v[part] * m_Pressure;
    return m_TPS->Particles()[part].Kurtosis(m_Parameters, m_UseWidth, m_Chem[part], dMu);
  }


  double ThermalModelEVDiagonal::CalculateBaryonScaledVariance(bool susc) {
    return 1.;
  }

  double ThermalModelEVDiagonal::CalculateChargeScaledVariance(bool susc) {
    return 1.;
  }

  double ThermalModelEVDiagonal::CalculateStrangenessScaledVariance(bool susc) {
    return 1.;
  }

  double ThermalModelEVDiagonal::ParticleScalarDensity(int part) {
    if (!m_Calculated) CalculateDensities();

    double dMu = -m_v[part] * m_Pressure;
    double ret = m_TPS->Particles()[part].Density(m_Parameters, IdealGasFunctions::ScalarDensity, m_UseWidth, m_Chem[part], dMu);
    return ret * m_Suppression;
  }

  double ThermalModelEVDiagonal::CommonSuppressionFactor()
  {
    if (!m_Calculated)
      CalculateDensities();
    return m_Suppression;
  }

  double ThermalModelEVDiagonal::MuShift(int id)
  {
    if (id >= 0. && id < m_v.size())
      return -m_v[id] * m_Pressure;
    else
      return 0.0;
  }

  double ThermalModelEVDiagonal::CalculateEigenvolumeFraction() {
    double tEV = 0.;
    for (int i = 0; i < m_TPS->Particles().size(); ++i) {
      tEV += m_v[i] * m_densities[i] / 4.;
    }
    return tEV;
  }

  void ThermalModelEVDiagonal::SetRadius(int i, double rad)
  {
    if (i >= 0 && i < m_v.size())
      m_v[i] = CuteHRGHelper::vr(rad);
  }

  double ThermalModelEVDiagonal::VirialCoefficient(int i, int j) const
  {
    return m_v[i];
  }

  void ThermalModelEVDiagonal::SetVirial(int i, int j, double b)
  {
    if (i == j)
      m_v[i] = b;
  }

  std::vector<double> ThermalModelEVDiagonal::BroydenEquationsDEV::Equations(const std::vector<double>& x)
  {
    std::vector<double> ret(1);
    double pressure = m_mnc * exp(x[0]);

    ret[0] = pressure - m_THM->Pressure(pressure);

    return ret;
  }

  Eigen::MatrixXd ThermalModelEVDiagonal::BroydenJacobianDEV::Jacobian(const std::vector<double>& x)
  {
    double pressure = m_mnc * exp(x[0]);

    MatrixXd Jac(1, 1);
    Jac(0, 0) = 0.;
    for (int i = 0; i < m_THM->Densities().size(); ++i)
      Jac(0, 0) += m_THM->VirialCoefficient(i, i) * m_THM->DensityId(i, pressure);
    Jac(0, 0) += 1.;
    Jac(0, 0) *= pressure;

    return Jac;
  }

  std::vector<double> ThermalModelEVDiagonal::BroydenEquationsDEVOrig::Equations(const std::vector<double>& x)
  {
    std::vector<double> ret(1);
    ret[0] = x[0] - m_THM->Pressure(x[0]);
    return ret;
  }

  Eigen::MatrixXd ThermalModelEVDiagonal::BroydenJacobianDEVOrig::Jacobian(const std::vector<double>& x)
  {
    const double &pressure = x[0];

    MatrixXd Jac(1, 1);
    Jac(0, 0) = 0.;
    for (int i = 0; i < m_THM->Densities().size(); ++i)
      Jac(0, 0) += m_THM->VirialCoefficient(i, i) * m_THM->DensityId(i, pressure);
    Jac(0, 0) += 1.;

    return Jac;
  }

  bool ThermalModelEVDiagonal::BroydenSolutionCriteriumDEV::IsSolved(const std::vector<double>& x, const std::vector<double>& f, const std::vector<double>& xdelta) const
  {
    double maxdiff = 0.;
    for (int i = 0; i < x.size(); ++i) {
      maxdiff = std::max(maxdiff, fabs(f[i]) / m_THM->m_Pressure);
    }
    return (maxdiff < m_RelativeError);
  }
} // namespace thermalfist