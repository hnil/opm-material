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
 * \copydoc Opm::BlackOilFluidStateSimple
 */
#ifndef OPM_BLACK_OIL_FLUID_STATE_SIMPLE_HH
#define OPM_BLACK_OIL_FLUID_STATE_SIMPLE_HH

#include <opm/material/fluidsystems/BlackOilFluidSystem.hpp>
#include <opm/material/common/HasMemberGeneratorMacros.hpp>

#include <opm/material/common/Valgrind.hpp>
#include <opm/material/common/Unused.hpp>
#include <opm/material/common/Exceptions.hpp>
#include <opm/material/common/ConditionalStorage.hpp>

namespace Opm {


/*!
 * \brief Implements a "tailor-made" fluid state class for the black-oil model.
 *
 * I.e., it uses exactly the same quantities which are used by the ECL blackoil
 * model. Further quantities are computed "on the fly" and are accessing them is thus
 * relatively slow.
 */
template <class ScalarT,
          class FluidSystem,
          bool enableTemperature = false,
          bool enableEnergy = false,
          bool enableDissolution = true,
          unsigned numStoragePhases = FluidSystem::numPhases>
class BlackOilFluidStateSimple
{
    enum { waterPhaseIdx = FluidSystem::waterPhaseIdx };
    enum { gasPhaseIdx = FluidSystem::gasPhaseIdx };
    enum { oilPhaseIdx = FluidSystem::oilPhaseIdx };

    enum { waterCompIdx = FluidSystem::waterCompIdx };
    enum { gasCompIdx = FluidSystem::gasCompIdx };
    enum { oilCompIdx = FluidSystem::oilCompIdx };

public:
    typedef ScalarT Scalar;
    enum { numPhases = FluidSystem::numPhases };
    enum { numComponents = FluidSystem::numComponents };

    /*!
     * \brief Make sure that all attributes are defined.
     *
     * This method does not do anything if the program is not run
     * under valgrind. If it is, then valgrind will print an error
     * message if some attributes of the object have not been properly
     * defined.
     */
    void checkDefined() const
    {
#ifndef NDEBUG
        Opm::Valgrind::CheckDefined(pvtRegionIdx_);

        for (unsigned storagePhaseIdx = 0; storagePhaseIdx < numStoragePhases; ++ storagePhaseIdx) {
            Opm::Valgrind::CheckDefined(saturation_[storagePhaseIdx]);
            Opm::Valgrind::CheckDefined(pressure_[storagePhaseIdx]);
            Opm::Valgrind::CheckDefined(density_[storagePhaseIdx]);
            Opm::Valgrind::CheckDefined(invB_[storagePhaseIdx]);

            if (enableEnergy)
                Opm::Valgrind::CheckDefined((*enthalpy_)[storagePhaseIdx]);
        }

        if (enableDissolution) {
            Opm::Valgrind::CheckDefined(*Rs_);
            Opm::Valgrind::CheckDefined(*Rv_);
        }

        if (enableTemperature || enableEnergy)
            Opm::Valgrind::CheckDefined(*temperature_);
#endif // NDEBUG
    }

    /*!
     * \brief Retrieve all parameters from an arbitrary fluid
     *        state.
     */
    template <class FluidState>
    void assign(const FluidState& fs)
    {
        if (enableTemperature || enableEnergy)
            setTemperature(fs.temperature(/*phaseIdx=*/0));

        unsigned pvtRegionIdx = getPvtRegionIndex_<FluidState>(fs);
        setPvtRegionIndex(pvtRegionIdx);

        if (enableDissolution) {
            setRs(Opm::BlackOil::getRs_<FluidSystem, FluidState, Scalar>(fs, pvtRegionIdx));
            setRv(Opm::BlackOil::getRv_<FluidSystem, FluidState, Scalar>(fs, pvtRegionIdx));
        }

        for (unsigned storagePhaseIdx = 0; storagePhaseIdx < numStoragePhases; ++storagePhaseIdx) {
            unsigned phaseIdx = storageToCanonicalPhaseIndex_(storagePhaseIdx);
            setSaturation(phaseIdx, fs.saturation(phaseIdx));
            setPressure(phaseIdx, fs.pressure(phaseIdx));
            setDensity(phaseIdx, fs.density(phaseIdx));

            if (enableEnergy)
                setEnthalpy(phaseIdx, fs.enthalpy(phaseIdx));

            setInvB(phaseIdx, getInvB_<FluidSystem, FluidState, Scalar>(fs, phaseIdx, pvtRegionIdx));
        }
    }

    /*!
     * \brief Set the index of the fluid region
     *
     * This determines which tables are used to compute the quantities that are computed
     * on the fly.
     */
    void setPvtRegionIndex(unsigned newPvtRegionIdx)
    { pvtRegionIdx_ = static_cast<unsigned short>(newPvtRegionIdx); }

    /*!
     * \brief Set the pressure of a fluid phase [-].
     */
    void setPressure(unsigned phaseIdx, const Scalar& p)
    { pressure_[canonicalToStoragePhaseIndex_(phaseIdx)] = p; }

    /*!
     * \brief Set the saturation of a fluid phase [-].
     */
    void setSaturation(unsigned phaseIdx, const Scalar& S)
    { saturation_[canonicalToStoragePhaseIndex_(phaseIdx)] = S; }

    /*!
     * \brief Set the temperature [K]
     *
     * If neither the enableTemperature nor the enableEnergy template arguments are set
     * to true, this method will throw an exception!
     */
    void setTemperature(const Scalar& value)
    {
        assert(enableTemperature || enableEnergy);

        (*temperature_) = value;
    }

    /*!
     * \brief Set the specific enthalpy [J/kg] of a given fluid phase.
     *
     * If the enableEnergy template argument is not set to true, this method will throw
     * an exception!
     */
    void setEnthalpy(unsigned phaseIdx, const Scalar& value)
    {
        assert(enableTemperature || enableEnergy);

        (*enthalpy_)[canonicalToStoragePhaseIndex_(phaseIdx)] = value;
    }

    /*!
     * \ brief Set the inverse formation volume factor of a fluid phase
     */
    void setInvB(unsigned phaseIdx, const Scalar& b)
    { invB_[canonicalToStoragePhaseIndex_(phaseIdx)] = b; }

    /*!
     * \ brief Set the density of a fluid phase
     */
    void setDensity(unsigned phaseIdx, const Scalar& rho)
    { density_[canonicalToStoragePhaseIndex_(phaseIdx)] = rho; }

    /*!
     * \brief Set the gas dissolution factor [m^3/m^3] of the oil phase.
     *
     * This quantity is very specific to the black-oil model.
     */
    void setRs(const Scalar& newRs)
    { *Rs_ = newRs; }

    /*!
     * \brief Set the oil vaporization factor [m^3/m^3] of the gas phase.
     *
     * This quantity is very specific to the black-oil model.
     */
    void setRv(const Scalar& newRv)
    { *Rv_ = newRv; }

    /*!
     * \brief Return the pressure of a fluid phase [Pa]
     */
    const Scalar& pressure(unsigned phaseIdx) const
    { return pressure_[canonicalToStoragePhaseIndex_(phaseIdx)]; }

    /*!
     * \brief Return the saturation of a fluid phase [-]
     */
    const Scalar& saturation(unsigned phaseIdx) const
    { return saturation_[canonicalToStoragePhaseIndex_(phaseIdx)]; }

    /*!
     * \brief Return the temperature [K]
     */
    const Scalar& temperature(unsigned phaseIdx OPM_UNUSED) const
    {
        if (!enableTemperature && !enableEnergy) {
            static Scalar tmp(FluidSystem::reservoirTemperature(pvtRegionIdx_));
            return tmp;
        }

        return *temperature_;
    }

    /*!
     * \brief Return the inverse formation volume factor of a fluid phase [-].
     *
     * This factor expresses the change of density of a pure phase due to increased
     * pressure and temperature at reservoir conditions compared to surface conditions.
     */
    const Scalar& invB(unsigned phaseIdx) const
    { return invB_[canonicalToStoragePhaseIndex_(phaseIdx)]; }

    /*!
     * \brief Return the gas dissulition factor of oil [m^3/m^3].
     *
     * I.e., the amount of gas which is present in the oil phase in terms of cubic meters
     * of gas at surface conditions per cubic meter of liquid oil at surface
     * conditions. This method is specific to the black-oil model.
     */
    const Scalar& Rs() const
    {
        if (!enableDissolution) {
            static Scalar null = 0.0;
            return null;
        }

        return *Rs_;
    }

    /*!
     * \brief Return the oil vaporization factor of gas [m^3/m^3].
     *
     * I.e., the amount of oil which is present in the gas phase in terms of cubic meters
     * of liquid oil at surface conditions per cubic meter of gas at surface
     * conditions. This method is specific to the black-oil model.
     */
    const Scalar& Rv() const
    {
        if (!enableDissolution) {
            static Scalar null = 0.0;
            return null;
        }

        return *Rv_;
    }

    /*!
     * \brief Return the PVT region where the current fluid state is assumed to be part of.
     *
     * This is an ECL specfic concept. It is basically a kludge to account for the fact
     * that the fluids components treated by the black-oil model exhibit different
     * compositions in different parts of the reservoir, while the black-oil model always
     * treats them as "oil", "gas" and "water".
     */
    unsigned short pvtRegionIndex() const
    { return pvtRegionIdx_; }

    /*!
     * \brief Return the density [kg/m^3] of a given fluid phase.
      */
    Scalar density(unsigned phaseIdx) const
    { return density_[canonicalToStoragePhaseIndex_(phaseIdx)]; }

    /*!
     * \brief Return the specific enthalpy [J/kg] of a given fluid phase.
     *
     * If the EnableEnergy property is not set to true, this method will throw an
     * exception!
     */
    const Scalar& enthalpy(unsigned phaseIdx) const
    { return (*enthalpy_)[canonicalToStoragePhaseIndex_(phaseIdx)]; }

    /*!
     * \brief Return the specific internal energy [J/kg] of a given fluid phase.
     *
     * If the EnableEnergy property is not set to true, this method will throw an
     * exception!
     */
    Scalar internalEnergy(unsigned phaseIdx OPM_UNUSED) const
    { return (*enthalpy_)[canonicalToStoragePhaseIndex_(phaseIdx)] - pressure(phaseIdx)/density(phaseIdx); }

    //////
    // slow methods
    //////

    /*!
     * \brief Return the dynamic viscosity of a fluid phase [Pa s].
     */
    Scalar viscosity(unsigned phaseIdx) const
    { return FluidSystem::viscosity(*this, phaseIdx, pvtRegionIdx_); }


private:
    static unsigned storageToCanonicalPhaseIndex_(unsigned storagePhaseIdx)
    {
        if (numStoragePhases == 3)
            return storagePhaseIdx;

        return FluidSystem::activeToCanonicalPhaseIdx(storagePhaseIdx);
    }

    static unsigned canonicalToStoragePhaseIndex_(unsigned canonicalPhaseIdx)
    {
        if (numStoragePhases == 3)
            return canonicalPhaseIdx;

        return FluidSystem::canonicalToActivePhaseIdx(canonicalPhaseIdx);
    }

    Opm::ConditionalStorage<enableTemperature || enableEnergy, Scalar> temperature_;
    Opm::ConditionalStorage<enableEnergy, std::array<Scalar, numStoragePhases> > enthalpy_;
    std::array<Scalar, numStoragePhases> pressure_;
    std::array<Scalar, numStoragePhases> saturation_;
    std::array<Scalar, numStoragePhases> invB_;
    std::array<Scalar, numStoragePhases> density_;
    Opm::ConditionalStorage<enableDissolution,Scalar> Rs_;
    Opm::ConditionalStorage<enableDissolution, Scalar> Rv_;
    unsigned short pvtRegionIdx_;
};

} // namespace Opm

#endif
