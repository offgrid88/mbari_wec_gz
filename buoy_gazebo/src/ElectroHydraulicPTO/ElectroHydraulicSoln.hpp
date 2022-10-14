// Copyright 2022 Open Source Robotics Foundation, Inc. and Monterey Bay Aquarium Research Institute
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ELECTROHYDRAULICPTO__ELECTROHYDRAULICSOLN_HPP_
#define ELECTROHYDRAULICPTO__ELECTROHYDRAULICSOLN_HPP_

#include <unsupported/Eigen/NonLinearOptimization>

// Interpolation library for efficiency maps
#include <splinter_ros/splinter2d.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ElectroHydraulicState.hpp"
#include "WindingCurrentTarget.hpp"


/////////////////////////////////////////////////////
// Generic functor
template<typename _Scalar, int NX = Eigen::Dynamic, int NY = Eigen::Dynamic>
struct Functor
{
  typedef _Scalar Scalar;
  enum { InputsAtCompileTime = NX, ValuesAtCompileTime = NY };
  typedef Eigen::Matrix<Scalar, InputsAtCompileTime, 1> InputType;
  typedef Eigen::Matrix<Scalar, ValuesAtCompileTime, 1> ValueType;
  typedef Eigen::Matrix<Scalar, ValuesAtCompileTime, InputsAtCompileTime>
    JacobianType;

  const int m_inputs, m_values;

  Functor()
  : m_inputs(InputsAtCompileTime), m_values(ValuesAtCompileTime) {}
  Functor(int inputs, int values)
  : m_inputs(inputs), m_values(values) {}

  int inputs() const {return m_inputs;}
  int values() const {return m_values;}

  // you should define that in the subclass :
  // void operator() (const InputType& x, ValueType* v, JacobianType* _j=0)
  // const;
};

struct ElectroHydraulicSoln : Functor<double>
{
public:
  const double PressReliefSetPoint = 2850;  // psi
  // ~50GPM/600psi ~= .33472 in^3/psi -> Relief valve flow
  // above setpoint, From SUN RPECLAN data sheet
  const double ReliefValveFlowPerPSI = 50.0 / 600.0;
  const double CubicInchesPerGallon = 231.0;
  const double SecondsPerMinute = 60.0;

  splinter_ros::Splinter1d hyd_eff_v, hyd_eff_m;

  // Class that computes Target Winding Current based on RPM, Scale Factor,
  // limits, etc..
  mutable WindingCurrentTarget I_Wind;
  double Q;
  /// \brief Pump/Motor Displacement per Revolution
  double HydMotorDisp;

private:
  static const std::vector<double> Peff;  // psi
  static const std::vector<double> Neff;  // rpm

  static const std::vector<double> Eff_V;  // volumetric efficiency
  static const std::vector<double> Eff_M;  // mechanical efficiency

public:
  ElectroHydraulicSoln()
  : Functor<double>(2, 2),
    // Set HydraulicMotor Volumetric & Mechanical Efficiency
    hyd_eff_v(Peff, Eff_V), hyd_eff_m(Neff, Eff_M) {}

#if 0
  int df(const Eigen::VectorXd & x, Eigen::MatrixXd & fjac) const
  {
    const double rpm = fabs(x[0U]);
    const double pressure = fabs(x[1U]);
    const double eff_m =
      this->hyd_eff_m.eval(rpm, splinter_ros::USE_BOUNDS);
    const double eff_v =
      this->hyd_eff_v.eval(pressure, splinter_ros::USE_BOUNDS);
    const std::vector<double> J_eff_m =
      this->hyd_eff_m.evalJacobian(rpm, splinter_ros::USE_BOUNDS);
    const std::vector<double> J_eff_v =
      this->hyd_eff_v.evalJacobian(pressure, splinter_ros::USE_BOUNDS);

    const double I = this->I_Wind(x[0U]);
    const double J_I = this->I_Wind.df(x[0U]);
    const double T = 1.375 * this->I_Wind.TorqueConstantInLbPerAmp * I;
    const double dTdx0 = 1.375 * this->I_Wind.TorqueConstantInLbPerAmp * J_I;

    fjac(0, 0) = 1 - J_eff_v[0U] * SecondsPerMinute * this->Q / this->HydMotorDisp;  // df0/dx0
    fjac(0, 1) = -J_eff_v[1U] * SecondsPerMinute * this->Q / this->HydMotorDisp;  // df0/dx1
    fjac(1, 0) =
      (-J_eff_m[0U] * T - eff_m * dTdx0) / (this->HydMotorDisp / (2.0 * M_PI));  // df1/dx0
    fjac(1, 1) = 1 - J_eff_m[1U] * T / (this->HydMotorDisp / (2.0 * M_PI));  // df1/dx1

    // std::cout << "Jacobian: " << fjac << std::endl;

    return 0;
  }
#endif

  // x[0] = RPM
  // x[1] = Pressure (psi)
  int operator()(const Eigen::VectorXd & x, Eigen::VectorXd & fvec) const
  {
    const int n = x.size();
    assert(fvec.size() == n);

    const double rpm = fabs(x[0U]);
    const double pressure = fabs(x[1U]);
    const double eff_m = this->hyd_eff_m.eval(rpm, splinter_ros::USE_BOUNDS);
    const double eff_v = this->hyd_eff_v.eval(pressure, splinter_ros::USE_BOUNDS);

    // 1.375 fudge factor required to match experiments, not yet sure why.
    const double T_applied =
      1.375 * this->I_Wind.TorqueConstantInLbPerAmp * this->I_Wind(x[0U]);

    double QQ = this->Q;
    if (x[1U] > PressReliefSetPoint) {  //  Pressure relief is a one wave valve,
                                        //  relieves when lower pressure is higher
                                        //  than upper (resisting extension)
      QQ += (x[1U] - PressReliefSetPoint) * ReliefValveFlowPerPSI *
        CubicInchesPerGallon / SecondsPerMinute;
    }

    if ((x[0U] > 0) - (-x[1U] < 0)) { //RPM and -deltaP have same sign
      std::cout << "motor quadrant" << x[0U] << "  "  <<  x[1U] << std::endl;
      fvec[0U] = x[0U] - eff_v * SecondsPerMinute * QQ / this->HydMotorDisp;
      fvec[1U] = x[1U] - eff_m * T_applied / (this->HydMotorDisp / (2.0 * M_PI));
    } else {
      std::cout << "pump quadrant" << x[0U] << "  "  <<  x[1U] << std::endl;
      fvec[0U] = eff_v * x[0U] - SecondsPerMinute * QQ / this->HydMotorDisp;
      fvec[1U] = eff_m * x[1U] - T_applied / (this->HydMotorDisp / (2.0 * M_PI));
    }
    return 0;
  }
};

const std::vector<double> ElectroHydraulicSoln::Peff{
  0.0, 145.0, 290.0, 435.0, 580.0, 725.0, 870.0,
  1015.0, 1160.0, 1305.0, 1450.0, 1595.0, 1740.0, 1885.0,
  2030.0, 2175.0, 2320.0, 2465.0, 2610.0, 2755.0, 2900.0};

const std::vector<double> ElectroHydraulicSoln::Neff{
  0.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0,
  800.0, 900.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0, 3500.0,
  4000.0, 4500.0, 5000.0, 5500.0, 6000.0, 15000.0};

const std::vector<double> ElectroHydraulicSoln::Eff_V{
  1.0000, 0.8720, 0.9240, 0.9480, 0.9520, 0.9620, 0.9660, 0.9660,
  0.9720, 0.9760, 0.9760, 0.9800, 0.9800, 0.9800, 0.9800, 0.9800,
  0.9800, 0.9800, 0.9800, 0.9800, 0.9800, 0.9800};

const std::vector<double> ElectroHydraulicSoln::Eff_M{
  1.0, 0.9360, 0.9420, 0.9460, 0.9460, 0.9460, 0.9460, 0.9460,
  0.9460, 0.9460, 0.9460, 0.9500, 0.9500, 0.9500, 0.9500, 0.9400,
  0.9300, 0.9300, 0.9200, 0.9100, 0.9000, 0.8400};

#endif  // ELECTROHYDRAULICPTO__ELECTROHYDRAULICSOLN_HPP_
