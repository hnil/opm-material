// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/
/*!
 * \file
 *
 * \brief This is the unit test for the black oil PVT classes
 *
 * This test requires the presence of opm-parser.
 */
#include "config.h"

#if !HAVE_ECL_INPUT
#error "The test for the black oil PVT classes requires eclipse input support in opm-common"
#endif

#include <iostream>
#include <iomanip>
#include <cmath>
#include <limits>
//#include <boost/program_options.hpp>


#include <opm/material/fluidsystems/blackoilpvt/LiveOilPvt.hpp>
#include <opm/material/fluidsystems/blackoilpvt/DeadOilPvt.hpp>
#include <opm/material/fluidsystems/blackoilpvt/ConstantCompressibilityOilPvt.hpp>
#include <opm/material/fluidsystems/blackoilpvt/WetGasPvt.hpp>
#include <opm/material/fluidsystems/blackoilpvt/DryGasPvt.hpp>
#include <opm/material/fluidsystems/blackoilpvt/ConstantCompressibilityWaterPvt.hpp>

#include <opm/material/fluidsystems/blackoilpvt/GasPvtMultiplexer.hpp>
#include <opm/material/fluidsystems/blackoilpvt/OilPvtMultiplexer.hpp>
#include <opm/material/fluidsystems/blackoilpvt/WaterPvtMultiplexer.hpp>

#include <opm/material/fluidsystems/BlackOilFluidSystem.hpp>



#include <opm/material/densead/Evaluation.hpp>
#include <opm/material/densead/Math.hpp>

//#include <ewoms/common/propertysystem.hh>
//#include <ewoms/common/parametersystem.hh>
//#include <opm/autodiff/MissingFeatures.hpp>
//#include <opm/common/utility/parameters/ParameterGroup.hpp>
#include <opm/material/common/ResetLocale.hpp>

#include <opm/parser/eclipse/EclipseState/checkDeck.hpp>
#include <opm/parser/eclipse/Parser/ParseContext.hpp>
#include <opm/parser/eclipse/Parser/Parser.hpp>
#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/EclipseState/EclipseState.hpp>

#include <dune/common/parallel/mpihelper.hh>

// values of strings based on the first SPE1 test case of opm-data.  note that in the
// real world it does not make much sense to specify a fluid phase using more than a
// single keyword, but for a unit test, this saves a lot of boiler-plate code.




template <class Scalar>
inline void testSaturationLine(const Opm::Deck& deck,const Opm::EclipseState& eclState,bool output_all = false)
{
    static const Scalar tolerance = std::numeric_limits<Scalar>::epsilon()*1e3;


    const auto& pvtwKeyword = deck.getKeyword("PVTW");
    size_t numPvtRegions = pvtwKeyword.size();

    std::cout << "Number of pvt regions " << numPvtRegions << std::endl;

    /////////////////////////////////////////
    // constant compressibility water
    //////////
    Opm::ConstantCompressibilityWaterPvt<Scalar> constCompWaterPvt;
    constCompWaterPvt.initFromDeck(deck, eclState);
    Opm::GasPvtMultiplexer<Scalar> gasPvt;
    Opm::OilPvtMultiplexer<Scalar> oilPvt;
    Opm::WaterPvtMultiplexer<Scalar> waterPvt;

    gasPvt.initFromDeck(deck, eclState);
    oilPvt.initFromDeck(deck, eclState);
    waterPvt.initFromDeck(deck, eclState);


    static const Scalar eps = std::sqrt(std::numeric_limits<Scalar>::epsilon());
    int steps = 50;
    
    bool all_fine = true;
    double Rs_old=0;
    double Rsv_old=0;
    for (int regionIdx = 0; regionIdx < numPvtRegions; ++regionIdx){
        std::cout << " ********************************************** " << std::endl;
        std::cout << " Testing pvt region " << regionIdx << std::endl;
        for (unsigned i = 0; i < steps; ++i) {        
            Scalar p = Scalar(i)/steps*600e5 + 10e5;
            Scalar T = 273.0;
            if(output_all){
                std::cout << "Testing at p: " << p << " T " << T << std::endl;
            }
            /*
              Scalar So = 0.3;
              Scalar Sg = 0.3;
              Scalar MaxSo = 0.7;
              Scalar MaxSg = 0.7;
              Scalar RsSat = oilPvt.saturatedGasDissolutionFactor(regionIdx, T, p, So, MaxSo);
              Scalar RvSat = gasPvt.saturatedOilVaporizationFactor(regionIdx, T, p, Sg, MaxSg);
            */
            // check consistency on saturated line
            Scalar RsSat = oilPvt.saturatedGasDissolutionFactor(regionIdx, T, p);
            Scalar RvSat = gasPvt.saturatedOilVaporizationFactor(regionIdx, T, p);
            Scalar Rs = RsSat;
            Scalar bo = oilPvt.inverseFormationVolumeFactor(regionIdx, T, p, Rs);
            Scalar bo_sat = oilPvt.saturatedInverseFormationVolumeFactor(regionIdx, T, p);

            
            if ((Opm::abs(bo - bo_sat) > eps) or output_all){

              if (Opm::abs(bo - bo_sat) > eps){
                std::cout << "********************* Error OilPVT bo*********************" << std::endl;
                all_fine=false;
                }
                std::cout << "Saturated table evaluation and saturated value not consitent " << std::endl;
                std::cout << "Pressure " << p << " Temperature " << T << " RsSat " << RsSat << std::endl;
                std::cout << "bo " << bo << " bo_sat " << bo_sat << std::endl;
                std::abort();
              
            }

            Scalar muo = oilPvt.inverseFormationVolumeFactor(regionIdx, T, p, Rs);
            Scalar muo_sat = oilPvt.saturatedInverseFormationVolumeFactor(regionIdx, T, p);

            
            if ((Opm::abs(muo - muo_sat) > eps) or output_all){

              if (Opm::abs(muo - muo_sat) > eps){
                std::cout << "********************* Error OilPVT mu*********************" << std::endl;
                all_fine=false;
                }
                std::cout << "Saturated table evaluation and saturated value not consitent " << std::endl;
                std::cout << "Pressure " << p << " Temperature " << T << " RsSat " << RsSat << std::endl;
                std::cout << "bo " << muo << " bo_sat " << muo_sat << std::endl;
                std::abort();
              
            }

            
            Scalar Rv = RvSat;
            Scalar bg = gasPvt.inverseFormationVolumeFactor(regionIdx, T, p, Rv);
            Scalar bg_sat = gasPvt.saturatedInverseFormationVolumeFactor(regionIdx, T, p);

        
            if ((Opm::abs(bg - bg_sat) > eps) or output_all){
              if (Opm::abs(bg - bg_sat) > eps){
                std::cout << "********************* Error GasPVT bg*********************" << std::endl;
                 all_fine=false;
              }
                std::cout << "Saturated table evaluation and saturated value not consitent " << std::endl;
                std::cout << "Pressure " << p << " Temperature " << T << " RvSat " << RvSat << std::endl;
                std::cout << "bg " << bg << " bg_sat " << bg_sat << std::endl;
                std::abort();
               
            }

            Scalar Rv = RvSat;
            Scalar mug = gasPvt.viscosity(regionIdx, T, p, Rv);
            Scalar mug_sat = gasPvt.saturatedViscosity(regionIdx, T, p);

        
            if ((Opm::abs(mug - mu_sat) > eps) or output_all){
                if (Opm::abs(mug - mu_sat) > eps){
                std::cout << "********************* Error GasPVT mu*********************" << std::endl;
                 all_fine=false;
              }
                std::cout << "Saturated table evaluation and saturated value not consitent " << std::endl;
                std::cout << "Pressure " << p << " Temperature " << T << " RvSat " << RvSat << std::endl;
                std::cout << "bg " << mug << " bg_sat " << mug_sat << std::endl;
                std::abort();
               
            }
            
            // do the same for viscosity
            
            // check derivatives on saturated line b muB mu

            // check something with vap par ????

            // check that evaluations everywhere do not fail
            // mu>0 b>0 muB>0 

            // check derivatives with numeric diffentiation

            // Advanced
            // check total compressibility

            // check saturation line increasing
            // b factors on line should be increasing

            // check strange initializations
            if(Rv<Rv_old){
                std::cout << "Rv_sat not increasing " << std::endl;
                std::abort();
            }
            if(Rs<Rs_old){
                std::cout << "Rs_sat not increasing " << std::endl;
                std::abort();
            }

            Rs_old=Rs;
            Rv_old =Rv
            
        }
    }
    if(!all_fine){
        std::cout << "Pvt evaluation on the saturation: error " << std::endl;
        std::abort();
    }else{
      std::cout << "Pvt evaluation on the saturation: All test fine" << std::endl;
    }
    
    
}

template <class Scalar>
inline void testFluidSystem(const Opm::Deck& deck,const Opm::EclipseState& eclState,bool output_all = false){
    Opm::BlackOilFluidSystem<double> fluidsystem;
    fluidsystem.initFromDeck(deck,eclipseState);
    static const Scalar tolerance = std::numeric_limits<Scalar>::epsilon()*1e3;


    const auto& pvtwKeyword = deck.getKeyword("PVTW");
    size_t numPvtRegions = pvtwKeyword.size();

    std::cout << "Number of pvt regions " << numPvtRegions << std::endl;

    static const Scalar eps = std::sqrt(std::numeric_limits<Scalar>::epsilon());
    int stepsx = 50;
    int stepsy = 50;
    
    bool all_fine = true;
    for (int regionIdx = 0; regionIdx < numPvtRegions; ++regionIdx){
        for (unsigned i = 0; i < stepsx; ++i) {
            for (unsigned j = 0; j < steps; ++j) {        
                Scalar p = Scalar(i)/steps*600e5 + 10e5;
                Scalar Rs = Scalar(i)/steps*400;
                Scalar Rv = Scalar(i)/steps*400;
            
                Scalar T = 273.0;
                FluidState fluidState;
                
                if (fluidsystem.phaseIsActive(waterPhaseIdx)){
                    fluidState.setSaturation(waterPhaseIdx, Sw);
                }
                if (fluidsystem.phaseIsActive(gasPhaseIdx)){
                    fluidState.setSaturation(gasPhaseIdx, Sg);
                }

                if (fluidsystem.phaseIsActive(oilPhaseIdx)){
                    fluidState.setSaturation(oilPhaseIdx, So);
                }

                for(int phindx=0; phindx < numPhases; ++phindx){
                    fluidState.setPressure(phaseIdx, p);
                }

                if (fluidsystem.enableVaporizedOil()) {
                    const auto& RvSat = fluidsystem.saturatedDissolutionFactor(fluidState_,
                                                                               gasPhaseIdx,
                                                                               pvtRegionIdx);                
                    fluidState.setRv(Opm::min(Rv, RvSat));
                }


                if (fluidsystem.enableDissolvedGas()) {
                    const auto& RsSat =
                        fluidsystem.saturatedDissolutionFactor(fluidState_,
                                                               oilPhaseIdx,
                                                               pvtRegionIdx);
                    fluidState.setRs(Opm::min(Rv, RsSat));
                    fluidState.setPressure(phaseIdx, p);
                }

                for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx) {
                    if (!fluidsystem.phaseIsActive(phaseIdx)){
                        continue;
                    }
                
                    const auto& b = fluidsystem.inverseFormationVolumeFactor(fluidState, phaseIdx, pvtRegionIdx);
                    fluidState.setInvB(phaseIdx, b);
                    const auto& mu = fluidsystem.viscosity(fluidState_, paramCache, phaseIdx);
                }

                // what to check

            
                if(output_all){
                    std::cout << "Testing at p: " << p << " T " << T << std::endl;
                }

            }
        }
    }
}
bool checkFluidState(const FluidState& fluidState,const FluidsSystem& fluidsystem){
    for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx) { 
        if (!fluidsystem.phaseIsActive(phaseIdx)){
            if(fluidstate.pressure(phaseIdx)<0){
                std::abort();
            }
            if(fluidstate.Rs()<0 and fluidstate.Rv()<0){
                std::abort()
            }
        }
    }
    
    
}

int main(int argc, char **argv)
{

  if(not(argc == 2)){
    std::cout << "Usage xxx deckfilename" << std::endl;
    return 1;
  }
   std::string deckFilename(argv[1]);
   std::cout << deckFilename << std::endl;
  
  
    Dune::MPIHelper::instance(argc, argv);
    std::cout << std::setprecision(std::numeric_limits<long double>::digits10 + 1);

    
    std::cout << "Reading deck file '" << deckFilename << "'\n";
    std::cout.flush();
    Opm::Parser parser;
    typedef std::pair<std::string, Opm::InputError::Action> ParseModePair;
    typedef std::vector<ParseModePair> ParseModePairs;
    ParseModePairs tmp;
    tmp.push_back(ParseModePair(Opm::ParseContext::PARSE_RANDOM_SLASH, Opm::InputError::IGNORE));
    tmp.push_back(ParseModePair(Opm::ParseContext::PARSE_MISSING_DIMS_KEYWORD, Opm::InputError::WARN));
    tmp.push_back(ParseModePair(Opm::ParseContext::SUMMARY_UNKNOWN_WELL, Opm::InputError::WARN));
    tmp.push_back(ParseModePair(Opm::ParseContext::SUMMARY_UNKNOWN_GROUP, Opm::InputError::WARN));
    Opm::ParseContext parseContext(tmp);

    Opm::Deck deck = parser.parseFile(deckFilename , parseContext);
    Opm::checkDeck(deck, parser);
    //Opm::MissingFeatures::checkKeywords(deck);
    
    Opm::Runspec runspec( deck );
    const auto& phases = runspec.phases();

    Opm::EclipseState eclipseState( deck, parseContext );

    

    testSaturationLine<double>(deck, eclipseState, true);
    


    
    //testAll<float>(deckString2);
    //    testAll<double>(deckString2);
    //testAll<float>(deckString2);

    return 0;
}
