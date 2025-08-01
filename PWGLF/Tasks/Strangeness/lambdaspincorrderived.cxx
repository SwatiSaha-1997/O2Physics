// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file taskLambdaSpinCorr.cxx
/// \brief Analysis task for Lambda spin spin correlation
///
/// \author sourav.kundu@cern.ch

#include "PWGLF/DataModel/LFSpincorrelationTables.h"

#include "Common/Core/trackUtilities.h"

#include "CommonConstants/PhysicsConstants.h"
#include "Framework/ASoAHelpers.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/AnalysisTask.h"
#include "Framework/BinningPolicy.h"
#include "Framework/StepTHn.h"
#include "Framework/runDataProcessing.h"
#include <Framework/Configurable.h>

#include <Math/GenVector/Boost.h>
#include <Math/Vector3D.h>
#include <Math/Vector4D.h>
#include <TLorentzVector.h>
#include <TMath.h>

#include <fairlogger/Logger.h>

#include <cmath> // for std::fabs
#include <deque>
#include <iostream>
#include <iterator>
#include <set> // <<< CHANGED: for dedup sets
#include <string>
#include <type_traits>
#include <unordered_map> // <<< CHANGED: for seenMap
#include <utility>
#include <vector>

// o2 includes.
#include "CCDB/BasicCCDBManager.h"
#include "CCDB/CcdbApi.h"

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::soa;

struct lambdaspincorrderived {
  // BinningType colBinning;
  struct : ConfigurableGroup {
    Configurable<std::string> cfgURL{"cfgURL", "http://alice-ccdb.cern.ch", "Address of the CCDB to browse"};
    Configurable<int64_t> nolaterthan{"ccdb-no-later-than", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), "Latest acceptable timestamp of creation for the object"};
  } cfgCcdbParam;

  // Enable access to the CCDB for the offset and correction constants and save them in dedicated variables.
  Service<o2::ccdb::BasicCCDBManager> ccdb;
  o2::ccdb::CcdbApi ccdbApi;
  TH3D* hweight1;
  TH3D* hweight2;
  TH3D* hweight3;

  Configurable<std::string> ConfWeightPathLL{"ConfWeightPathLL", "Users/s/skundu/My/Object/spincorr/cent010LL", "Weight path"};
  Configurable<std::string> ConfWeightPathALAL{"ConfWeightPathALAL", "Users/s/skundu/My/Object/spincorr/cent010LL", "Weight path"};
  Configurable<std::string> ConfWeightPathLAL{"ConfWeightPathLAL", "Users/s/skundu/My/Object/spincorr/cent010LL", "Weight path"};

  // event sel/////////
  Configurable<float> centMin{"centMin", 0, "Minimum Centrality"};
  Configurable<float> centMax{"centMax", 80, "Maximum Centrality"};

  // Lambda selection ////////////
  Configurable<unsigned> harmonic{"harmonic", 1, "Harmonic delta phi"};
  Configurable<bool> useweight{"useweight", 1, "Use weight"};
  Configurable<bool> usePDGM{"usePDGM", 1, "Use PDG mass"};
  Configurable<bool> checkDoubleStatus{"checkDoubleStatus", 0, "Check Double status"};
  Configurable<float> cosPA{"cosPA", 0.995, "Cosine Pointing Angle"};
  Configurable<float> radiusMin{"radiusMin", 3, "Minimum V0 radius"};
  Configurable<float> radiusMax{"radiusMax", 30, "Maximum V0 radius"};
  Configurable<float> dcaProton{"dcaProton", 0.1, "DCA Proton"};
  Configurable<float> dcaPion{"dcaPion", 0.2, "DCA Pion"};
  Configurable<float> dcaDaughters{"dcaDaughters", 1.0, "DCA between daughters"};
  Configurable<float> ptMin{"ptMin", 0.5, "V0 Pt minimum"};
  Configurable<float> ptMax{"ptMax", 3.0, "V0 Pt maximum"};
  Configurable<float> rapidity{"rapidity", 0.5, "Rapidity cut on lambda"};
  Configurable<float> v0eta{"v0eta", 0.8, "Eta cut on lambda"};

  // Event Mixing
  Configurable<int> nEvtMixing{"nEvtMixing", 10, "Number of events to mix"};
  ConfigurableAxis CfgVtxBins{"CfgVtxBins", {10, -10, 10}, "Mixing bins - z-vertex"};
  ConfigurableAxis CfgMultBins{"CfgMultBins", {8, 0.0, 80}, "Mixing bins - centrality"};
  Configurable<float> etaMix{"etaMix", 0.1, "Eta cut on event mixing"};
  Configurable<float> ptMix{"ptMix", 0.1, "Pt cut on event mixing"};
  Configurable<float> phiMix{"phiMix", 0.1, "Phi cut on event mixing"};
  Configurable<float> massMix{"massMix", 0.0028, "Masscut on event mixing"};

  // THnsparse bining
  ConfigurableAxis configThnAxisInvMass{"configThnAxisInvMass", {50, 1.09, 1.14}, "#it{M} (GeV/#it{c}^{2})"};
  ConfigurableAxis configThnAxisR{"configThnAxisR", {80, 0.0, 8.0}, "#it{R}"};
  ConfigurableAxis configThnAxisPol{"configThnAxisPol", {80, 0.0, 8.0}, "cos#it{#theta *}"};
  ConfigurableAxis configThnAxisCentrality{"configThnAxisCentrality", {8, 0.0, 80.0}, "Centrality"};
  HistogramRegistry histos{"histos", {}, OutputObjHandlingPolicy::AnalysisObject};

  void init(o2::framework::InitContext&)
  {
    histos.add("hPtYSame", "hPtYSame", kTH2F, {{100, 0.0, 10.0}, {200, -1.0, 1.0}});
    histos.add("hPtYMix", "hPtYMix", kTH2F, {{100, 0.0, 10.0}, {200, -1.0, 1.0}});
    histos.add("hCentrality", "Centrality distribution", kTH1F, {{configThnAxisCentrality}});
    histos.add("deltaPhiSame", "deltaPhiSame", HistType::kTH1D, {{72, 0.0, 2.0 * TMath::Pi()}}, true);
    histos.add("deltaPhiMix", "deltaPhiMix", HistType::kTH1D, {{72, 0.0, 2.0 * TMath::Pi()}}, true);
    histos.add("ptCent", "ptCent", HistType::kTH2D, {{100, 0.0, 10.0}, {8, 0.0, 80.0}}, true);
    histos.add("etaCent", "etaCent", HistType::kTH2D, {{32, -0.8, 0.8}, {8, 0.0, 80.0}}, true);

    histos.add("hLambdaSameForLL", "hLambdaSameForLL", HistType::kTH3D, {{50, 0.0, 5.0}, {32, -0.8, 0.8}, {72, 0.0, 2.0 * TMath::Pi()}}, true);
    histos.add("hLambdaSameForLAL", "hLambdaSameForLAL", HistType::kTH3D, {{50, 0.0, 5.0}, {32, -0.8, 0.8}, {72, 0.0, 2.0 * TMath::Pi()}}, true);
    histos.add("hAntiLambdaSameForALAL", "hAntiLambdaSameForALAL", HistType::kTH3D, {{50, 0.0, 5.0}, {32, -0.8, 0.8}, {72, 0.0, 2.0 * TMath::Pi()}}, true);

    histos.add("hLambdaMixForLL", "hLambdaMixForLL", HistType::kTH3D, {{50, 0.0, 5.0}, {32, -0.8, 0.8}, {72, 0.0, 2.0 * TMath::Pi()}}, true);
    histos.add("hLambdaMixForLAL", "hLambdaMixForLAL", HistType::kTH3D, {{50, 0.0, 5.0}, {32, -0.8, 0.8}, {72, 0.0, 2.0 * TMath::Pi()}}, true);
    histos.add("hAntiLambdaMixForALAL", "hAntiLambdaMixForALAL", HistType::kTH3D, {{50, 0.0, 5.0}, {32, -0.8, 0.8}, {72, 0.0, 2.0 * TMath::Pi()}}, true);

    histos.add("hSparseLambdaLambda", "hSparseLambdaLambda", HistType::kTHnSparseF, {configThnAxisInvMass, configThnAxisInvMass, configThnAxisPol, configThnAxisCentrality, configThnAxisR}, true);
    histos.add("hSparseLambdaAntiLambda", "hSparseLambdaAntiLambda", HistType::kTHnSparseF, {configThnAxisInvMass, configThnAxisInvMass, configThnAxisPol, configThnAxisCentrality, configThnAxisR}, true);
    histos.add("hSparseAntiLambdaAntiLambda", "hSparseAntiLambdaAntiLambda", HistType::kTHnSparseF, {configThnAxisInvMass, configThnAxisInvMass, configThnAxisPol, configThnAxisCentrality, configThnAxisR}, true);

    histos.add("hSparseLambdaLambdaMixed", "hSparseLambdaLambdaMixed", HistType::kTHnSparseF, {configThnAxisInvMass, configThnAxisInvMass, configThnAxisPol, configThnAxisCentrality, configThnAxisR}, true);
    histos.add("hSparseLambdaAntiLambdaMixed", "hSparseLambdaAntiLambdaMixed", HistType::kTHnSparseF, {configThnAxisInvMass, configThnAxisInvMass, configThnAxisPol, configThnAxisCentrality, configThnAxisR}, true);
    histos.add("hSparseAntiLambdaAntiLambdaMixed", "hSparseAntiLambdaAntiLambdaMixed", HistType::kTHnSparseF, {configThnAxisInvMass, configThnAxisInvMass, configThnAxisPol, configThnAxisCentrality, configThnAxisR}, true);

    ccdb->setURL(cfgCcdbParam.cfgURL);
    ccdbApi.init("http://alice-ccdb.cern.ch");
    ccdb->setCaching(true);
    ccdb->setLocalObjectValidityChecking();
    ccdb->setCreatedNotAfter(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    LOGF(info, "Getting alignment offsets from the CCDB...");
    hweight1 = ccdb->getForTimeStamp<TH3D>(ConfWeightPathLL.value, cfgCcdbParam.nolaterthan.value);
    hweight2 = ccdb->getForTimeStamp<TH3D>(ConfWeightPathALAL.value, cfgCcdbParam.nolaterthan.value);
    hweight3 = ccdb->getForTimeStamp<TH3D>(ConfWeightPathLAL.value, cfgCcdbParam.nolaterthan.value);
  }

  template <typename T>
  bool selectionV0(T const& candidate)
  {
    auto particle = ROOT::Math::PtEtaPhiMVector(candidate.lambdaPt(), candidate.lambdaEta(), candidate.lambdaPhi(), candidate.lambdaMass());
    if (std::abs(particle.Rapidity()) > rapidity || std::abs(particle.Eta()) > v0eta) {
      return false;
    }
    if (candidate.v0Cospa() < cosPA) {
      return false;
    }
    if (checkDoubleStatus && candidate.doubleStatus()) {
      return false;
    }
    if (candidate.v0Radius() > radiusMax) {
      return false;
    }
    if (candidate.v0Radius() < radiusMin) {
      return false;
    }
    if (candidate.dcaBetweenDaughter() > dcaDaughters) {
      return false;
    }
    if (candidate.v0Status() == 0 && std::abs(candidate.dcaPositive()) < dcaProton && std::abs(candidate.dcaNegative()) < dcaPion) {
      return false;
    }
    if (candidate.v0Status() == 1 && std::abs(candidate.dcaPositive()) < dcaPion && std::abs(candidate.dcaNegative()) < dcaProton) {
      return false;
    }
    if (candidate.lambdaPt() < ptMin) {
      return false;
    }
    if (candidate.lambdaPt() > ptMax) {
      return false;
    }
    return true;
  }

  template <typename T1, typename T2>
  bool checkKinematics(T1 const& candidate1, T2 const& candidate2)
  {
    if (candidate1.v0Status() != candidate2.v0Status()) {
      return false;
    }
    if (std::abs(candidate1.lambdaPt() - candidate2.lambdaPt()) > ptMix) {
      return false;
    }
    if (std::abs(candidate1.lambdaEta() - candidate2.lambdaEta()) > etaMix) {
      return false;
    }
    if (std::abs(RecoDecay::constrainAngle(candidate1.lambdaPhi(), 0.0F, harmonic) - RecoDecay::constrainAngle(candidate2.lambdaPhi(), 0.0F, harmonic)) > phiMix) {
      return false;
    }
    if (std::abs(candidate1.lambdaMass() - candidate2.lambdaMass()) > massMix) {
      return false;
    }
    return true;
  }

  void fillHistograms(int tag1, int tag2,
                      const ROOT::Math::PtEtaPhiMVector& particle1, const ROOT::Math::PtEtaPhiMVector& particle2,
                      const ROOT::Math::PtEtaPhiMVector& daughpart1, const ROOT::Math::PtEtaPhiMVector& daughpart2,
                      double centrality, int datatype, float mixpairweight)
  {

    auto lambda1Mass = 0.0;
    auto lambda2Mass = 0.0;
    if (!usePDGM) {
      lambda1Mass = particle1.M();
      lambda2Mass = particle2.M();
    } else {
      lambda1Mass = o2::constants::physics::MassLambda;
      lambda2Mass = o2::constants::physics::MassLambda;
    }
    auto particle1Dummy = ROOT::Math::PtEtaPhiMVector(particle1.Pt(), particle1.Eta(), particle1.Phi(), lambda1Mass);
    auto particle2Dummy = ROOT::Math::PtEtaPhiMVector(particle2.Pt(), particle2.Eta(), particle2.Phi(), lambda2Mass);
    auto pairDummy = particle1Dummy + particle2Dummy;
    ROOT::Math::Boost boostPairToCM{pairDummy.BoostToCM()}; // boosting vector for pair CM

    // Step1: Boosting both Lambdas to Lambda-Lambda pair rest frame
    auto lambda1CM = boostPairToCM(particle1Dummy);
    auto lambda2CM = boostPairToCM(particle2Dummy);

    // Step 2: Boost Each Lambda to its Own Rest Frame
    ROOT::Math::Boost boostLambda1ToCM{lambda1CM.BoostToCM()};
    ROOT::Math::Boost boostLambda2ToCM{lambda2CM.BoostToCM()};

    // Also boost the daughter protons to the same frame
    auto proton1pairCM = boostPairToCM(daughpart1); // proton1 to pair CM
    auto proton2pairCM = boostPairToCM(daughpart2); // proton2 to pair CM

    // Boost protons into their respective Lambda rest frames
    auto proton1LambdaRF = boostLambda1ToCM(proton1pairCM);
    auto proton2LambdaRF = boostLambda2ToCM(proton2pairCM);

    auto cosThetaDiff = -999.0;
    cosThetaDiff = proton1LambdaRF.Vect().Unit().Dot(proton2LambdaRF.Vect().Unit());
    double deltaPhi = std::abs(RecoDecay::constrainAngle(particle1Dummy.Phi(), 0.0F, harmonic) - RecoDecay::constrainAngle(particle2Dummy.Phi(), 0.0F, harmonic));
    double deltaEta = particle1Dummy.Eta() - particle2Dummy.Eta();
    double deltaR = TMath::Sqrt(deltaEta * deltaEta + deltaPhi * deltaPhi);

    if (datatype == 0) {
      mixpairweight = 1.0;
      histos.fill(HIST("hPtYSame"), particle1.Pt(), particle1.Rapidity(), mixpairweight);
      if (tag1 == 0 && tag2 == 0) {
        histos.fill(HIST("hSparseLambdaLambda"), particle1.M(), particle2.M(), cosThetaDiff, centrality, deltaR, mixpairweight);
        histos.fill(HIST("hLambdaSameForLL"), particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic), mixpairweight);
      } else if ((tag1 == 0 && tag2 == 1) || (tag1 == 1 && tag2 == 0)) {
        histos.fill(HIST("hSparseLambdaAntiLambda"), particle1.M(), particle2.M(), cosThetaDiff, centrality, deltaR, mixpairweight);
        histos.fill(HIST("hLambdaSameForLAL"), particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic), mixpairweight);
      } else if (tag1 == 1 && tag2 == 1) {
        histos.fill(HIST("hSparseAntiLambdaAntiLambda"), particle1.M(), particle2.M(), cosThetaDiff, centrality, deltaR, mixpairweight);
        histos.fill(HIST("hAntiLambdaSameForALAL"), particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic), mixpairweight);
      }
    } else if (datatype == 1) {
      double weight1 = mixpairweight;
      double weight2 = mixpairweight;
      double weight3 = mixpairweight;
      if (useweight) {
        weight1 = mixpairweight * hweight1->GetBinContent(hweight1->FindBin(particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic)));
        weight2 = mixpairweight * hweight2->GetBinContent(hweight2->FindBin(particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic)));
        weight3 = mixpairweight * hweight3->GetBinContent(hweight3->FindBin(particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic)));
      }
      histos.fill(HIST("hPtYMix"), particle1.Pt(), particle1.Rapidity());
      if (tag1 == 0 && tag2 == 0) {
        histos.fill(HIST("hSparseLambdaLambdaMixed"), particle1.M(), particle2.M(), cosThetaDiff, centrality, deltaR, weight1);
        histos.fill(HIST("hLambdaMixForLL"), particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic), weight1);
      } else if ((tag1 == 0 && tag2 == 1) || (tag1 == 1 && tag2 == 0)) {
        histos.fill(HIST("hSparseLambdaAntiLambdaMixed"), particle1.M(), particle2.M(), cosThetaDiff, centrality, deltaR, weight2);
        histos.fill(HIST("hLambdaMixForLAL"), particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic), weight2);
      } else if (tag1 == 1 && tag2 == 1) {
        histos.fill(HIST("hSparseAntiLambdaAntiLambdaMixed"), particle1.M(), particle2.M(), cosThetaDiff, centrality, deltaR, weight3);
        histos.fill(HIST("hAntiLambdaMixForALAL"), particle1.Pt(), particle1.Eta(), RecoDecay::constrainAngle(particle1.Phi(), 0.0F, harmonic), weight3);
      }
    }
  }

  ROOT::Math::PtEtaPhiMVector lambda0, proton0;
  ROOT::Math::PtEtaPhiMVector lambda, proton;
  ROOT::Math::PtEtaPhiMVector lambda2, proton2;

  Filter centralityFilter = (nabs(aod::lambdaevent::cent) < centMax && nabs(aod::lambdaevent::cent) > centMin);

  using EventCandidates = soa::Filtered<aod::LambdaEvents>;
  using AllTrackCandidates = aod::LambdaPairs;

  void processData(EventCandidates::iterator const& collision, AllTrackCandidates const& V0s)
  {
    auto centrality = collision.cent();
    for (const auto& v0 : V0s) {
      if (!selectionV0(v0)) {
        continue;
      }
      histos.fill(HIST("ptCent"), v0.lambdaPt(), centrality);
      histos.fill(HIST("etaCent"), v0.lambdaEta(), centrality);
      proton = ROOT::Math::PtEtaPhiMVector(v0.protonPt(), v0.protonEta(), v0.protonPhi(), o2::constants::physics::MassProton);
      lambda = ROOT::Math::PtEtaPhiMVector(v0.lambdaPt(), v0.lambdaEta(), v0.lambdaPhi(), v0.lambdaMass());
      for (const auto& v02 : V0s) {
        if (v02.index() <= v0.index()) {
          continue;
        }
        if (!selectionV0(v02)) {
          continue;
        }
        if (v0.protonIndex() == v02.protonIndex()) {
          continue;
        }
        if (v0.pionIndex() == v02.pionIndex()) {
          continue;
        }
        proton2 = ROOT::Math::PtEtaPhiMVector(v02.protonPt(), v02.protonEta(), v02.protonPhi(), o2::constants::physics::MassProton);
        lambda2 = ROOT::Math::PtEtaPhiMVector(v02.lambdaPt(), v02.lambdaEta(), v02.lambdaPhi(), v02.lambdaMass());
        histos.fill(HIST("deltaPhiSame"), std::abs(RecoDecay::constrainAngle(v0.lambdaPhi(), 0.0F, harmonic) - RecoDecay::constrainAngle(v02.lambdaPhi(), 0.0F, harmonic)));
        if (v0.v0Status() == 0 && v02.v0Status() == 0) {
          fillHistograms(0, 0, lambda, lambda2, proton, proton2, centrality, 0, 1.0);
        }
        if (v0.v0Status() == 0 && v02.v0Status() == 1) {
          fillHistograms(0, 1, lambda, lambda2, proton, proton2, centrality, 0, 1.0);
        }
        if (v0.v0Status() == 1 && v02.v0Status() == 0) {
          fillHistograms(1, 0, lambda2, lambda, proton2, proton, centrality, 0, 1.0);
        }
        if (v0.v0Status() == 1 && v02.v0Status() == 1) {
          fillHistograms(1, 1, lambda, lambda2, proton, proton2, centrality, 0, 1.0);
        }
      }
    }
  }
  PROCESS_SWITCH(lambdaspincorrderived, processData, "Process data", true);

  // Processing Event Mixing
  SliceCache cache;
  using BinningType = ColumnBinningPolicy<aod::lambdaevent::Posz, aod::lambdaevent::Cent>;
  BinningType colBinning{{CfgVtxBins, CfgMultBins}, true};
  Preslice<aod::LambdaPairs> tracksPerCollisionV0 = aod::lambdapair::lambdaeventId;
  void processME(EventCandidates const& collisions, AllTrackCandidates const& V0s)
  {
    auto collOldIndex = -999;
    std::vector<bool> t1Used;
    for (auto& [collision1, collision2] : selfCombinations(colBinning, nEvtMixing, -1, collisions, collisions)) {
      // LOGF(info, "Mixed event collisions: (%d, %d)", collision1.index(), collision2.index());
      auto centrality = collision1.cent();
      auto groupV01 = V0s.sliceBy(tracksPerCollisionV0, collision1.index());
      auto groupV02 = V0s.sliceBy(tracksPerCollisionV0, collision1.index());
      auto groupV03 = V0s.sliceBy(tracksPerCollisionV0, collision2.index());
      auto collNewIndex = collision1.index();
      // LOGF(info, "Mixed event collisions: (%d, %d)", collNewIndex, collOldIndex);
      if (collOldIndex != collNewIndex) {
        t1Used.resize(groupV01.size(), false);
        // std::fill(t1Used.begin(), t1Used.end(), false);
        // std::vector<bool> t1Used(groupV01.size(), false); // <-- reset here
        collOldIndex = collNewIndex;
      }
      for (auto& [t1, t3] : soa::combinations(o2::soa::CombinationsFullIndexPolicy(groupV01, groupV03))) {
        if (t1Used[t1.index()]) {
          continue;
        }
        if (!checkKinematics(t1, t3)) {
          continue;
        }
        if (!selectionV0(t1)) {
          continue;
        }
        if (!selectionV0(t3)) {
          continue;
        }
        t1Used[t1.index()] = true;
        for (const auto& t2 : groupV02) {
          if (t2.index() <= t1.index()) {
            continue;
          }
          if (!selectionV0(t2)) {
            continue;
          }
          if (t1.protonIndex() == t2.protonIndex()) {
            continue;
          }
          if (t1.pionIndex() == t2.pionIndex()) {
            continue;
          }
          proton = ROOT::Math::PtEtaPhiMVector(t3.protonPt(), t3.protonEta(), t3.protonPhi(), o2::constants::physics::MassProton);
          lambda = ROOT::Math::PtEtaPhiMVector(t3.lambdaPt(), t3.lambdaEta(), t3.lambdaPhi(), t3.lambdaMass());
          proton2 = ROOT::Math::PtEtaPhiMVector(t2.protonPt(), t2.protonEta(), t2.protonPhi(), o2::constants::physics::MassProton);
          lambda2 = ROOT::Math::PtEtaPhiMVector(t2.lambdaPt(), t2.lambdaEta(), t2.lambdaPhi(), t2.lambdaMass());
          histos.fill(HIST("deltaPhiMix"), std::abs(RecoDecay::constrainAngle(t3.lambdaPhi(), 0.0F, harmonic) - RecoDecay::constrainAngle(t2.lambdaPhi(), 0.0F, harmonic)));
          if (t3.v0Status() == 0 && t2.v0Status() == 0) {
            fillHistograms(0, 0, lambda, lambda2, proton, proton2, centrality, 1, 1.0);
          }
          if (t3.v0Status() == 0 && t2.v0Status() == 1) {
            fillHistograms(0, 1, lambda, lambda2, proton, proton2, centrality, 1, 1.0);
          }
          if (t3.v0Status() == 1 && t2.v0Status() == 0) {
            fillHistograms(1, 0, lambda2, lambda, proton2, proton, centrality, 1, 1.0);
          }
          if (t3.v0Status() == 1 && t2.v0Status() == 1) {
            fillHistograms(1, 1, lambda, lambda2, proton, proton2, centrality, 1, 1.0);
          }
        }
      } // replacement track pair
    } // collision pair
  }
  PROCESS_SWITCH(lambdaspincorrderived, processME, "Process data ME", false);

  void processMEV2(EventCandidates const& collisions, AllTrackCandidates const& V0s)
  {
    auto nBins = colBinning.getAllBinsCount();
    std::vector<std::deque<std::pair<int, AllTrackCandidates>>> eventPools(nBins);

    for (auto& collision1 : collisions) {
      int bin = colBinning.getBin(std::make_tuple(collision1.posz(), collision1.cent()));
      auto poolA = V0s.sliceBy(tracksPerCollisionV0, collision1.index());
      float centrality = collision1.cent();

      // <<< CHANGED: map old collision index → set of (t2.idx, t3.idx) we've already filled
      std::unordered_map<int, std::set<std::pair<int, int>>> seenMap;

      for (auto& [t1, t2] : soa::combinations(o2::soa::CombinationsFullIndexPolicy(poolA, poolA))) {
        if (!selectionV0(t1) || !selectionV0(t2))
          continue;
        if (t2.index() <= t1.index())
          continue;
        if (t1.protonIndex() == t2.protonIndex())
          continue;
        if (t1.pionIndex() == t2.pionIndex())
          continue;

        int mixes = 0;
        for (auto it = eventPools[bin].rbegin(); it != eventPools[bin].rend() && mixes < nEvtMixing; ++it, ++mixes) {
          int collision2idx = it->first;
          AllTrackCandidates& poolB = it->second;

          int nRepl = 0;
          for (auto& t3 : poolB) {
            if (selectionV0(t3) && checkKinematics(t1, t3)) {
              ++nRepl;
            }
          }
          if (nRepl == 0)
            continue;
          float invN = 1.0f / static_cast<float>(nRepl);

          for (auto& t3 : poolB) {
            if (!(selectionV0(t3) && checkKinematics(t1, t3))) {
              continue;
            }
            if (collision1.index() == collision2idx) {
              continue;
            }

            // <<< CHANGED: dedupe (t2, t3) pairs per prior collision
            auto key = std::make_pair(t2.index(), t3.index());
            auto& seen = seenMap[collision2idx];
            if (!seen.insert(key).second) {
              continue;
            }

            // reconstruct 4-vectors
            proton = ROOT::Math::PtEtaPhiMVector(t3.protonPt(), t3.protonEta(), t3.protonPhi(), o2::constants::physics::MassProton);
            lambda = ROOT::Math::PtEtaPhiMVector(t3.lambdaPt(), t3.lambdaEta(), t3.lambdaPhi(), t3.lambdaMass());
            proton2 = ROOT::Math::PtEtaPhiMVector(t2.protonPt(), t2.protonEta(), t2.protonPhi(), o2::constants::physics::MassProton);
            lambda2 = ROOT::Math::PtEtaPhiMVector(t2.lambdaPt(), t2.lambdaEta(), t2.lambdaPhi(), t2.lambdaMass());

            float dPhi = std::fabs(RecoDecay::constrainAngle(lambda.Phi(), 0.0F, harmonic) - RecoDecay::constrainAngle(lambda2.Phi(), 0.0F, harmonic));
            histos.fill(HIST("deltaPhiMix"), dPhi, invN);

            if (t3.v0Status() == 0 && t2.v0Status() == 0) {
              fillHistograms(0, 0, lambda, lambda2, proton, proton2, centrality, 1, invN);
            }
            if (t3.v0Status() == 0 && t2.v0Status() == 1) {
              fillHistograms(0, 1, lambda, lambda2, proton, proton2, centrality, 1, invN);
            }
            if (t3.v0Status() == 1 && t2.v0Status() == 0) {
              fillHistograms(1, 0, lambda2, lambda, proton2, proton, centrality, 1, invN);
            }
            if (t3.v0Status() == 1 && t2.v0Status() == 1) {
              fillHistograms(1, 1, lambda, lambda2, proton, proton2, centrality, 1, invN);
            }
          }
        } // end mixing-event loop
      } // end same-event pair loop

      auto sliced = V0s.sliceBy(tracksPerCollisionV0, collision1.index());
      eventPools[bin].emplace_back(collision1.index(), std::move(sliced));
      if (static_cast<int>(eventPools[bin].size()) > nEvtMixing) {
        eventPools[bin].pop_front();
      }
    } // end primary-event loop
  }
  PROCESS_SWITCH(lambdaspincorrderived, processMEV2, "Process data ME", false);
};
WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{adaptAnalysisTask<lambdaspincorrderived>(cfgc)};
}
