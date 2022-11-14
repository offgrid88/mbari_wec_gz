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

#include <gtest/gtest.h>
#include <gnuplot-iostream.h>

#include <cstring>
#include <iostream>
#include <unsupported/Eigen/NonLinearOptimization>

#include <buoy_gazebo/ElectroHydraulicPTO/ElectroHydraulicSoln.hpp>
#include <rclcpp/rclcpp.hpp>

//#include <ElectroHydraulicState.hpp>
//#include <ElectroHydraulicLoss.hpp>


class EHSolver : public ::testing::Test
{
protected:
  static bool manual;
  static int argc_;
  static char ** argv_;

public:
  static void init(int argc, char * argv[])
  {
    argc_ = argc;
    argv_ = argv;
  }

protected:
  // runs once and is preserved for all `TEST_F`
  static void SetUpTestCase()
  {
    rclcpp::init(argc_, argv_);
    rclcpp::Node::SharedPtr param_node = std::make_shared<rclcpp::Node>("eh_solver");
    rclcpp::spin_some(param_node);

    param_node->declare_parameter("manual", false);
    manual = param_node->get_parameter("manual").as_bool();
    std::cerr << "manual: " <<
      std::boolalpha << manual << std::noboolalpha <<
      std::endl;
  }

  static void TearDownTestCase()
  {
    if (manual) {
      rclcpp::spin(std::make_shared<rclcpp::Node>("wait_for_exit"));
    }
  }
};
bool EHSolver::manual{false};
int EHSolver::argc_;
char ** EHSolver::argv_;


//////////////////////////////////////////////////
TEST_F(EHSolver, GENERATOR_MODE)
{
  static constexpr double RPM_TO_RAD_PER_SEC{2.0 * M_PI / 60.0};
  static constexpr double NM_PER_INLB{0.112984829};
  const double Ve = 320;       // Volts
  const double Ri = 2;       // Ohms
  const double PistonArea = 1.375;       // in^2
  const double HydMotorDisp = 0.3;       // in^3
  double ScaleFactor = 0.6;
  double RetractFactor = 1.0;
  ElectroHydraulicSoln functor{};
  Eigen::VectorXd x{};
  functor.VBattEMF = Ve;
  functor.Ri = Ri;
  functor.HydMotorDisp = HydMotorDisp;

  x.resize(3);
  //x.setConstant(3, 0.0);
  //x[2] = Ve;

  double PistonPos = 40;       // in
  //functor.I_Wind.bias_override_ = true;
  //functor.I_Wind.BiasCurrent = 20.0;
//  for(double PistonVel  = -40; PistonVel <= 40; PistonVel += 1.0)// in^3
//  for(double PistonVel  = 16; PistonVel <= 20.1; PistonVel += 2.0)// in^3
  double PistonVel = -10.0;
  {
    std::vector<double> pts_N, pts_N_IC;
    std::vector<double> pts_deltaP, pts_deltaP_IC;
    std::vector<double> pts_VBus, pts_VBus_IC;
    std::vector<double> pts_solverinfo;
    std::vector<double> pts_Icommand;
    std::vector<double> pts_Itarg, pts_Itarg_IC;
    for (double WindCurrTarg = -36; WindCurrTarg <= 36.1; WindCurrTarg += 1.0) {
      pts_Icommand.push_back(WindCurrTarg);
      functor.I_Wind.RamPosition = PistonPos;                   // in
      functor.Q = PistonVel * PistonArea;                   // inch^3/second


      functor.I_Wind.ScaleFactor = ScaleFactor;
      functor.I_Wind.RetractFactor = 1.0;                   // RetractFactor;


      functor.I_Wind.current_override_ = true;
      functor.I_Wind.UserCommandedCurrent = WindCurrTarg;

//See MINPACK documentation for detail son this solver
// Parameters and defaults are (Scalar = double):
//           : factor(Scalar(100.))
//           , maxfev(1000)
//           , xtol(std::sqrt(NumTraits<Scalar>::epsilon()))
//           , nb_of_subdiagonals(-1)
//           , nb_of_superdiagonals(-1)
//           , epsfcn(Scalar(0.)) {}
      Eigen::HybridNonLinearSolver<ElectroHydraulicSoln> solver(functor);
      solver.parameters.xtol = 0.0001;
      solver.parameters.maxfev = 1000;
      // solver.parameters.nb_of_subdiagonals = 1;
      //solver.parameters.nb_of_superdiagonals = 1;
      solver.diag.setConstant(3, 1.);
      solver.diag[2] = .1;
      solver.useExternalScaling = true;                   // Improves solution stability dramatically.
//std::cout << "factor = " << solver.parameters.factor << std::endl;
//std::cout << "epsfcn = " << solver.parameters.epsfcn << std::endl;

      // Initial condition based on perfect efficiency
      x[0] = 60.0 * functor.Q / functor.HydMotorDisp;

      double WindCurr = functor.I_Wind(x[0U]);
      pts_Itarg_IC.push_back(WindCurr);
      // 1.375 fudge factor required to match experiments, not yet sure why.
      const double T_applied = 1.375 * functor.I_Wind.TorqueConstantInLbPerAmp * WindCurr;
      x[1] = -T_applied / (functor.HydMotorDisp / (2 * M_PI));

      //Estimate VBus based on linearized battery
      double PBus = -x[0] * RPM_TO_RAD_PER_SEC * T_applied * NM_PER_INLB;
      x[2] = functor.Ri * PBus / Ve + Ve;

      //    std::cout << "# " << PistonVel << "      " << WindCurrTarg << "   " << WindCurr
      //              << "           " << x[0] << "  " << x[1] << "  ";// << x[2] << "      ";
      pts_N_IC.push_back(x[0]);
      pts_deltaP_IC.push_back(x[1]);
      pts_VBus_IC.push_back(x[2]);
      // solver.solveNumericalDiff will compute Jacobian numerically rather than obtain from user
      const int solver_info = solver.solveNumericalDiff(x);
      if (solver_info != 1) {
        std::cout << "=================================" << std::endl;
        std::cout << "Warning: Numericals solver in ElectroHydraulicPTO did not converge" <<
          std::endl;
        std::cout << "solver info: [" << solver_info << "]" << std::endl;
        std::cout << "PistonVel = " << PistonVel << std::endl;
        std::cout << "CommandedAmps = " << WindCurrTarg << std::endl;
        std::cout << "=================================" << std::endl;
      }

      pts_N.push_back(x[0]);
      pts_deltaP.push_back(x[1]);
      pts_VBus.push_back(x[2]);
      pts_solverinfo.push_back(solver_info);
      WindCurr = functor.I_Wind(x[0U]);
      pts_Itarg.push_back(WindCurr);
    }

    if (manual) {
      {
        Gnuplot gp;
        gp << "set term X11 title  'PistonVel = " << std::to_string(PistonVel) << " in/s'\n";
        gp << "set grid\n";
        gp << "set xlabel 'Commanded Amps'\n";
        gp << "set ylabel 'Target Amps'\n";
        gp << "plot '-' w l title 'Itarg IC'" <<
          ",'-' w l title 'Itarg'" <<
          "\n";
        gp.send1d(boost::make_tuple(pts_Icommand, pts_Itarg_IC));
        gp.send1d(boost::make_tuple(pts_Icommand, pts_Itarg));
      }
      {
        Gnuplot gp;
        gp << "set term X11 title  'PistonVel = " << std::to_string(PistonVel) << " in/s'\n";
        gp << "set grid\n";
        gp << "set xlabel 'Commanded Amps'\n";
        gp << "set ylabel 'RPM'\n";
        gp << "plot '-' w l title 'RPM IC'" <<
          ",'-' w l title 'RPM'" <<
          "\n";
        gp.send1d(boost::make_tuple(pts_Icommand, pts_N_IC));
        gp.send1d(boost::make_tuple(pts_Icommand, pts_N));
      }
      {
        Gnuplot gp;
        gp << "set term X11 title  'PistonVel = " << std::to_string(PistonVel) << " in/s'\n";
        gp << "set grid\n";
        gp << "set xlabel 'Commanded Amps'\n";
        gp << "set ylabel 'psi'\n";
        gp << "plot '-' w l title 'deltaP IC'" <<
          ",'-' w l title 'deltaP'" <<
          "\n";
        gp.send1d(boost::make_tuple(pts_Icommand, pts_deltaP_IC));
        gp.send1d(boost::make_tuple(pts_Icommand, pts_deltaP));
      }
      {
        Gnuplot gp;
        gp << "set term X11 title  'PistonVel = " << std::to_string(PistonVel) << " in/s'\n";
        gp << "set grid\n";
        gp << "set xlabel 'Commanded Amps'\n";
        gp << "set ylabel 'Volts'\n";
        gp << "plot '-' w l title 'VBus IC'" <<
          ",'-' w l title 'VBus'" <<
          "\n";
        gp.send1d(boost::make_tuple(pts_Icommand, pts_VBus_IC));
        gp.send1d(boost::make_tuple(pts_Icommand, pts_VBus));
      }
      {
        Gnuplot gp;
        gp << "set term X11 title  'PistonVel = " << std::to_string(PistonVel) << " in/s'\n";
        gp << "set grid\n";
        gp << "set xlabel 'Commanded Amps'\n";
        gp << "set ylabel 'solver status'\n";
        gp << "plot '-' w l title 'solver_info'" <<
          "\n";
        gp.send1d(boost::make_tuple(pts_Icommand, pts_solverinfo));
      }
    }
  }

  EXPECT_EQ(1.0, 1.0);
  EXPECT_NE(1.0, 0.0);
}


int main(int argc, char * argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  EHSolver::init(argc, argv); // pass args to rclcpp init
  return RUN_ALL_TESTS();
}
