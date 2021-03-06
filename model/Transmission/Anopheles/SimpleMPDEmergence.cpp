/* This file is part of OpenMalaria.
 * 
 * Copyright (C) 2005-2015 Swiss Tropical and Public Health Institute
 * Copyright (C) 2005-2015 Liverpool School Of Tropical Medicine
 * 
 * OpenMalaria is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "Transmission/Anopheles/SimpleMPDEmergence.h"
#include "Transmission/Anopheles/MosqTransmission.h"
#include "Transmission/Anopheles/Nv0DelayFitting.h"

#include "util/vectors.h"
#include "util/CommandLine.h"
#include "util/errors.h"
#include "schema/entomology.h"

#include "rotate.h"

namespace OM {
namespace Transmission {
namespace Anopheles {
using namespace OM::util;


// -----  Initialisation of model, done before human warmup  ------

SimpleMPDEmergence::SimpleMPDEmergence(const scnXml::SimpleMPD& elt) :
    initNv0FromSv( numeric_limits<double>::quiet_NaN() )
{
    quinquennialS_v.assign (SimTime::fromYearsI(5), 0.0);
    quinquennialOvipositing.assign (SimTime::fromYearsI(5), 0.0);
    mosqEmergeRate.assign (SimTime::oneYear(), 0.0);
    invLarvalResources.assign (SimTime::oneYear(), 0.0);
    
    developmentDuration = SimTime::fromDays(elt.getDevelopmentDuration().getValue());
    if (!(developmentDuration > SimTime::zero()))
        throw util::xml_scenario_error("entomology.vector.simpleMPD.developmentDuration: "
            "must be positive");
    probPreadultSurvival = elt.getDevelopmentSurvival().getValue();
    if (!(0.0 <= probPreadultSurvival && probPreadultSurvival <= 1.0))
        throw util::xml_scenario_error("entomology.vector.simpleMPD.developmentSurvival: "
            "must be a probability (in range [0,1]");
    fEggsLaidByOviposit = elt.getFemaleEggsLaidByOviposit().getValue();
    if (!(fEggsLaidByOviposit > 0.0))
        throw util::xml_scenario_error("entomology.vector.simpleMPD.femaleEggsLaidByOviposit: "
            "must be positive");
    nOvipositingDelayed.assign (developmentDuration, 0.0);
}


// -----  Initialisation of model which is done after creating initial humans  -----

void SimpleMPDEmergence::init2( double tsP_A, double tsP_df, double tsP_dff, double EIRtoS_v, MosqTransmission& transmission ){
    // -----  Calculate required S_v based on desired EIR  -----
    
    initNv0FromSv = initNvFromSv * (1.0 - tsP_A - tsP_df);

    // We scale FSCoeffic to give us S_v instead of EIR.
    // Log-values: adding log is same as exponentiating, multiplying and taking
    // the log again.
    FSCoeffic[0] += log( EIRtoS_v);
    vectors::expIDFT (forcedS_v, FSCoeffic, FSRotateAngle);
    
    transmission.initState ( tsP_A, tsP_df, tsP_dff, initNvFromSv, initOvFromSv, forcedS_v );
    
    // Initialise nOvipositingDelayed
    SimTime y1 = SimTime::oneYear();
    SimTime tau = transmission.getMosqRestDuration();
    for( SimTime t = SimTime::zero(); t < developmentDuration; t += SimTime::oneDay() ){
        nOvipositingDelayed[mod_nn(t+tau, developmentDuration)] =
            tsP_dff * initNvFromSv * forcedS_v[t];
    }
    
    // Crude estimate of mosqEmergeRate: (1 - P_A(t) - P_df(t)) / (T * ρ_S) * S_T(t)
    mosqEmergeRate = forcedS_v;
    vectors::scale (mosqEmergeRate, initNv0FromSv);
    // Used when calculating invLarvalResources (but not a hard constraint):
    assert(tau+developmentDuration <= y1);
    for( SimTime t = SimTime::zero(); t < SimTime::oneYear(); t += SimTime::oneDay() ){
        double yt = fEggsLaidByOviposit * tsP_dff * initNvFromSv *
            forcedS_v[mod_nn(t + y1 - tau - developmentDuration, y1)];
        invLarvalResources[t] = (probPreadultSurvival * yt - mosqEmergeRate[t]) /
            (mosqEmergeRate[t] * yt);
    }
    
    // All set up to drive simulation from forcedS_v

    scaleFactor = 1.0;
    shiftAngle = FSRotateAngle;
    scaled = false;
    rotated = false;
}


// -----  Initialisation of model which is done after running the human warmup  -----

bool SimpleMPDEmergence::initIterate (MosqTransmission& transmission) {
    // Try to match S_v against its predicted value. Don't try with N_v or O_v
    // because the predictions will change - would be chasing a moving target!
    // EIR comes directly from S_v, so should fit after we're done.

    // Compute avgAnnualS_v from quinquennialS_v for fitting 
    vecDay<double> avgAnnualS_v( SimTime::oneYear(), 0.0 );
    for( SimTime i = SimTime::fromYearsI(4); i < SimTime::fromYearsI(5); i += SimTime::oneDay() ){
        avgAnnualS_v[mod_nn(i, SimTime::oneYear())] =
            quinquennialS_v[i];
    }

    double factor = vectors::sum(forcedS_v) / vectors::sum(avgAnnualS_v);

    const double LIMIT = 0.1;

    if(fabs(factor - 1.0) > LIMIT)
    {
        scaled = false;
        double factorDiff = (scaleFactor * factor - scaleFactor) * 1.0;
        scaleFactor += factorDiff;
    }
    else
        scaled = true;

    double rAngle = findAngle(EIRRotateAngle, FSCoeffic, avgAnnualS_v);
    shiftAngle += rAngle;
    rotated = true;

    // cout << "EIRRotateAngle: " << EIRRotateAngle << " rAngle = " << rAngle << ", angle = " << shiftAngle << " scalefactor: " << scaleFactor << " , factor: " << factor << endl;

    // Compute forced_sv from the Fourrier Coeffs
    // shiftAngle rotate the vector to correct the offset between simulated and input EIR
    vectors::expIDFT(mosqEmergeRate, FSCoeffic, -shiftAngle);
    // Scale the vector according to initNv0FromSv to get the mosqEmergerate
    // scaleFactor scales the vector to correct the ratio between simulated and input EIR
    vectors::scale (mosqEmergeRate, scaleFactor * initNv0FromSv);
    // Finally, update nOvipositingDelayed and invLarvalResources
    vectors::scale (nOvipositingDelayed, scaleFactor);
    transmission.initIterateScale (scaleFactor);

    SimTime y1 = SimTime::oneYear(),
        y2 = SimTime::fromYearsI(2),
        y3 = SimTime::fromYearsI(3),
        y4 = SimTime::fromYearsI(4),
        y5 = SimTime::fromYearsI(5);
    assert(mosqEmergeRate.size() == y1);
    
    for( SimTime t = SimTime::zero(); t < y1; t += SimTime::oneDay() ){
        SimTime ttj = t - developmentDuration;
        // b · P_df · avg_N_v(t - θj - τ):
        double yt = fEggsLaidByOviposit * 0.2 * (
            quinquennialOvipositing[ttj + y1] +
            quinquennialOvipositing[ttj + y2] +
            quinquennialOvipositing[ttj + y3] +
            quinquennialOvipositing[ttj + y4] +
            quinquennialOvipositing[mod_nn(ttj + y5, y5)]);
        invLarvalResources[t] = (probPreadultSurvival * yt - mosqEmergeRate[t]) /
            (mosqEmergeRate[t] * yt);
    }
    
    return !(scaled && rotated);

    // const double LIMIT = 0.1;
    // return (fabs(factor - 1.0) > LIMIT) ||
    //        (rAngle > LIMIT * 2*M_PI / sim::stepsPerYear());
    //NOTE: in theory, mosqEmergeRate and annualEggsLaid aren't needed after convergence.
}


double SimpleMPDEmergence::update( SimTime d0, double nOvipositing, double S_v ){
    SimTime d1 = d0 + SimTime::oneDay();
    SimTime d5Year = mod_nn(d1, SimTime::fromYearsI(5));
    quinquennialS_v[d5Year] = S_v;
    
    // Simple Mosquito Population Dynamics model: emergence depends on the
    // adult population, resources available, and larviciding.
    // See: A Simple Periodically-Forced Difference Equation Model for
    // Mosquito Population Dynamics, N. Chitnis, 2012. TODO: publish & link.
    
    double yt = fEggsLaidByOviposit * nOvipositingDelayed[mod_nn(d1, developmentDuration)];
    double emergence = interventionSurvival() * probPreadultSurvival * yt /
        (1.0 + invLarvalResources[mod_nn(d0, SimTime::oneYear())] * yt);
    nOvipositingDelayed[mod_nn(d1, developmentDuration)] = nOvipositing;
    quinquennialOvipositing[mod_nn(d1, SimTime::fromYearsI(5))] = nOvipositing;
    return emergence;
}

double SimpleMPDEmergence::getResAvailability() const {
    //TODO: why offset by one time step? This is effectively getting the resources available on the last time step
    //TODO: only have to add one year because of offset
    SimTime start = sim::now() - SimTime::oneTS() + SimTime::oneYear();
    double total = 0;
    for( SimTime i = start, end = start + SimTime::oneTS(); i < end; i += SimTime::oneDay() ){
        SimTime dYear1 = mod_nn(i, SimTime::oneYear());
        total += 1.0 / invLarvalResources[dYear1];
    }
    return total / SimTime::oneTS().inDays();
}

void SimpleMPDEmergence::checkpoint (istream& stream){ (*this) & stream; }
void SimpleMPDEmergence::checkpoint (ostream& stream){ (*this) & stream; }

}
}
}
