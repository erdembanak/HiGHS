/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2020 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**@file ipm/IpxWrapper.h
 * @brief
 * @author Julian Hall, Ivet Galabova, Qi Huangfu and Michael Feldmeier
 */
#ifndef IPM_IPX_WRAPPER_H_
#define IPM_IPX_WRAPPER_H_

#include <algorithm>

#include "ipm/IpxSolution.h"
#include "ipm/IpxStatus.h"
#include "ipm/ipx/include/ipx_status.h"
#include "ipm/ipx/src/lp_solver.h"
#include "lp_data/HConst.h"
#include "lp_data/HighsLp.h"
#include "lp_data/HighsSolution.h"

IpxStatus fillInIpxData(const HighsLp& lp, ipx::Int& num_col,
                        std::vector<double>& obj, std::vector<double>& col_lb,
                        std::vector<double>& col_ub, ipx::Int& num_row,
                        std::vector<ipx::Int>& Ap, std::vector<ipx::Int>& Ai,
                        std::vector<double>& Ax, std::vector<double>& rhs,
                        std::vector<char>& constraint_type) {
  num_col = lp.numCol_;
  num_row = lp.numRow_;

  // For each row with both a lower and an upper bound introduce one new column
  // so num_col may increase. Ignore each free row so num_row may decrease.
  // lba <= a'x <= uba becomes
  // a'x-s = 0 and lba <= s <= uba.

  // For each row with bounds on both sides introduce explicit slack and
  // transfer bounds.
  assert(lp.rowLower_.size() == (unsigned int)num_row);
  assert(lp.rowUpper_.size() == (unsigned int)num_row);

  std::vector<int> general_bounded_rows;
  std::vector<int> free_rows;

  for (int row = 0; row < num_row; row++)
    if (lp.rowLower_[row] < lp.rowUpper_[row] &&
        lp.rowLower_[row] > -HIGHS_CONST_INF &&
        lp.rowUpper_[row] < HIGHS_CONST_INF)
      general_bounded_rows.push_back(row);
    else if (lp.rowLower_[row] == -HIGHS_CONST_INF &&
             lp.rowUpper_[row] == HIGHS_CONST_INF)
      free_rows.push_back(row);

  const int num_slack = general_bounded_rows.size();

  // For each row except free rows add entry to char array and set up rhs
  // vector
  rhs.reserve(num_row);
  constraint_type.reserve(num_row);

  for (int row = 0; row < num_row; row++) {
    if (lp.rowLower_[row] > -HIGHS_CONST_INF &&
        lp.rowUpper_[row] == HIGHS_CONST_INF) {
      rhs.push_back(lp.rowLower_[row]);
      constraint_type.push_back('>');
    } else if (lp.rowLower_[row] == -HIGHS_CONST_INF &&
               lp.rowUpper_[row] < HIGHS_CONST_INF) {
      rhs.push_back(lp.rowUpper_[row]);
      constraint_type.push_back('<');
    } else if (lp.rowLower_[row] == lp.rowUpper_[row]) {
      rhs.push_back(lp.rowUpper_[row]);
      constraint_type.push_back('=');
    } else if (lp.rowLower_[row] > -HIGHS_CONST_INF &&
               lp.rowUpper_[row] < HIGHS_CONST_INF) {
      // general bounded
      rhs.push_back(0);
      constraint_type.push_back('=');
    }
  }

  std::vector<int> reduced_rowmap(lp.numRow_, -1);
  if (free_rows.size() > 0) {
    int counter = 0;
    int findex = 0;
    for (int row = 0; row < lp.numRow_; row++) {
      if (free_rows[findex] == row) {
        findex++;
        continue;
      } else {
        reduced_rowmap[row] = counter;
        counter++;
      }
    }
  } else {
    for (int k = 0; k < lp.numRow_; k++) reduced_rowmap[k] = k;
  }
  num_row -= free_rows.size();
  num_col += num_slack;

  std::vector<int> sizes(num_col, 0);

  for (int col = 0; col < lp.numCol_; col++)
    for (int k = lp.Astart_[col]; k < lp.Astart_[col + 1]; k++) {
      int row = lp.Aindex_[k];
      if (lp.rowLower_[row] > -HIGHS_CONST_INF ||
          lp.rowUpper_[row] < HIGHS_CONST_INF)
        sizes[col]++;
    }
  // Copy Astart and Aindex to ipx::Int array.
  int nnz = lp.Aindex_.size();
  Ap.resize(num_col + 1);
  Ai.reserve(nnz + num_slack);
  Ax.reserve(nnz + num_slack);

  // Set starting points of original and newly introduced columns.
  Ap[0] = 0;
  for (int col = 0; col < lp.numCol_; col++) {
    Ap[col + 1] = Ap[col] + sizes[col];
    //    printf("Struc Ap[%2d] = %2d; Al[%2d] = %2d\n", col, (int)Ap[col], col,
    //    (int)sizes[col]);
  }
  for (int col = lp.numCol_; col < (int)num_col; col++) {
    Ap[col + 1] = Ap[col] + 1;
    //    printf("Slack Ap[%2d] = %2d\n", col, (int)Ap[col]);
  }
  //  printf("Fictn Ap[%2d] = %2d\n", (int)num_col, (int)Ap[num_col]);
  for (int k = 0; k < nnz; k++) {
    int row = lp.Aindex_[k];
    if (lp.rowLower_[row] > -HIGHS_CONST_INF ||
        lp.rowUpper_[row] < HIGHS_CONST_INF) {
      Ai.push_back(reduced_rowmap[lp.Aindex_[k]]);
      Ax.push_back(lp.Avalue_[k]);
    }
  }

  for (int k = 0; k < num_slack; k++) {
    Ai.push_back((ipx::Int)general_bounded_rows[k]);
    Ax.push_back(-1);
  }

  // Column bound vectors.
  col_lb.resize(num_col);
  col_ub.resize(num_col);
  for (int col = 0; col < lp.numCol_; col++) {
    if (lp.colLower_[col] == -HIGHS_CONST_INF)
      col_lb[col] = -INFINITY;
    else
      col_lb[col] = lp.colLower_[col];

    if (lp.colUpper_[col] == HIGHS_CONST_INF)
      col_ub[col] = INFINITY;
    else
      col_ub[col] = lp.colUpper_[col];
  }
  for (int slack = 0; slack < num_slack; slack++) {
    const int row = general_bounded_rows[slack];
    col_lb[lp.numCol_ + slack] = lp.rowLower_[row];
    col_ub[lp.numCol_ + slack] = lp.rowUpper_[row];
  }

  obj.resize(num_col);
  for (int col = 0; col < lp.numCol_; col++) {
    obj[col] = (int)lp.sense_ * lp.colCost_[col];
  }
  obj.insert(obj.end(), num_slack, 0);
#ifdef HiGHSDEV
  printf("IPX model has %d columns, %d rows and %d nonzeros\n", (int)num_col,
         (int)num_row, (int)Ap[num_col]);
#endif
  /*
  for (int col = 0; col < num_col; col++)
    printf("Col %2d: [%11.4g, %11.4g] Cost = %11.4g; Start = %d\n", col,
  col_lb[col], col_ub[col], obj[col], (int)Ap[col]); for (int row = 0; row <
  num_row; row++) printf("Row %2d: RHS = %11.4g; Type = %d\n", row, rhs[row],
  constraint_type[row]); for (int col = 0; col < num_col; col++) { for (int el =
  Ap[col]; el < Ap[col+1]; el++) { printf("El %2d: [%2d, %11.4g]\n", el,
  (int)Ai[el], Ax[el]);
    }
  }
  */

  return IpxStatus::OK;
}

HighsStatus reportIpxSolveStatus(const HighsOptions& options,
                                 const ipx::Int solve_status,
                                 const ipx::Int error_flag) {
  if (solve_status == IPX_STATUS_solved) {
    HighsLogMessage(options.logfile, HighsMessageType::INFO, "Ipx: Solved");
    return HighsStatus::OK;
  } else if (solve_status == IPX_STATUS_stopped) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING, "Ipx: Stopped");
    return HighsStatus::Warning;
  } else if (solve_status == IPX_STATUS_invalid_input) {
    if (error_flag == IPX_ERROR_argument_null) {
      HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                      "Ipx: Invalid input - argument_null");
      return HighsStatus::Error;
    } else if (error_flag == IPX_ERROR_invalid_dimension) {
      HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                      "Ipx: Invalid input - invalid dimension");
      return HighsStatus::Error;
    } else if (error_flag == IPX_ERROR_invalid_matrix) {
      HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                      "Ipx: Invalid input - invalid matrix");
      return HighsStatus::Error;
    } else if (error_flag == IPX_ERROR_invalid_vector) {
      HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                      "Ipx: Invalid input - invalid vector");
      return HighsStatus::Error;
    } else if (error_flag == IPX_ERROR_invalid_basis) {
      HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                      "Ipx: Invalid input - invalid basis");
      return HighsStatus::Error;
    } else {
      HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                      "Ipx: Invalid input - unrecognised error");
      return HighsStatus::Error;
    }
  } else if (solve_status == IPX_STATUS_out_of_memory) {
    HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                    "Ipx: Out of memory");
    return HighsStatus::Error;
  } else if (solve_status == IPX_STATUS_internal_error) {
    HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                    "Ipx: Internal error %d", (int)error_flag);
    return HighsStatus::Error;
  } else {
    HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                    "Ipx: unrecognised solve status = %d", (int)solve_status);
    return HighsStatus::Error;
  }
  return HighsStatus::Error;
}

HighsStatus reportIpxIpmCrossoverStatus(const HighsOptions& options,
                                        const ipx::Int status,
                                        const bool ipm_status) {
  std::string method_name;
  if (ipm_status)
    method_name = "IPM      ";
  else
    method_name = "Crossover";
  if (status == IPX_STATUS_not_run) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING,
                    "Ipx: %s not run", method_name.c_str());
    return HighsStatus::Warning;
  } else if (status == IPX_STATUS_optimal) {
    HighsLogMessage(options.logfile, HighsMessageType::INFO, "Ipx: %s optimal",
                    method_name.c_str());
    return HighsStatus::OK;
  } else if (status == IPX_STATUS_imprecise) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING,
                    "Ipx: %s imprecise", method_name.c_str());
    return HighsStatus::Warning;
  } else if (status == IPX_STATUS_primal_infeas) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING,
                    "Ipx: %s primal infeasible", method_name.c_str());
    return HighsStatus::Warning;
  } else if (status == IPX_STATUS_dual_infeas) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING,
                    "Ipx: %s dual infeasible", method_name.c_str());
    return HighsStatus::Warning;
  } else if (status == IPX_STATUS_time_limit) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING,
                    "Ipx: %s reached time limit", method_name.c_str());
    return HighsStatus::Warning;
  } else if (status == IPX_STATUS_iter_limit) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING,
                    "Ipx: %s reached iteration limit", method_name.c_str());
    return HighsStatus::Warning;
  } else if (status == IPX_STATUS_no_progress) {
    HighsLogMessage(options.logfile, HighsMessageType::WARNING,
                    "Ipx: %s no progress", method_name.c_str());
    return HighsStatus::Warning;
  } else if (status == IPX_STATUS_failed) {
    HighsLogMessage(options.logfile, HighsMessageType::ERROR, "Ipx: %s failed",
                    method_name.c_str());
    return HighsStatus::Error;
  } else if (status == IPX_STATUS_debug) {
    HighsLogMessage(options.logfile, HighsMessageType::ERROR, "Ipx: %s debug",
                    method_name.c_str());
    return HighsStatus::Error;
  } else {
    HighsLogMessage(options.logfile, HighsMessageType::ERROR,
                    "Ipx: %s unrecognised status", method_name.c_str());
    return HighsStatus::Error;
  }
  return HighsStatus::Error;
}

HighsStatus solveLpIpx(const HighsOptions& options, HighsTimer& timer,
                       const HighsLp& lp, HighsBasis& highs_basis,
                       HighsSolution& highs_solution,
                       HighsModelStatus& unscaled_model_status,
                       HighsSolutionParams& unscaled_solution_params) {
  int debug = 0;
  resetModelStatusAndSolutionParams(unscaled_model_status,
                                    unscaled_solution_params, options);
#ifdef CMAKE_BUILD_TYPE
  debug = 1;
#endif

  // Create the LpSolver instance
  ipx::LpSolver lps;
  // Set IPX parameters
  //
  // Cannot set internal IPX parameters directly since they are
  // private, so create instance of parameters
  ipx::Parameters parameters;
  // parameters.crossover = 1; by default
  if (debug) parameters.debug = 1;
  // Set IPX parameters from options
  // Just test feasibility and optimality tolerances for now
  // ToDo Set more parameters
  parameters.ipm_feasibility_tol =
      unscaled_solution_params.primal_feasibility_tolerance;
  parameters.ipm_optimality_tol =
      unscaled_solution_params.dual_feasibility_tolerance;
  // Determine the run time allowed for IPX
  parameters.time_limit = options.time_limit - timer.readRunHighsClock();
  //  parameters.time_limit = 3;
  parameters.ipm_maxiter = options.ipm_iteration_limit;
  // Set the internal IPX parameters
  lps.SetParameters(parameters);

  ipx::Int num_col, num_row;
  std::vector<ipx::Int> Ap, Ai;
  std::vector<double> objective, col_lb, col_ub, Av, rhs;
  std::vector<char> constraint_type;
  IpxStatus result = fillInIpxData(lp, num_col, objective, col_lb, col_ub,
                                   num_row, Ap, Ai, Av, rhs, constraint_type);
  if (result != IpxStatus::OK) return HighsStatus::Error;

  ipx::Int solve_status =
      lps.Solve(num_col, &objective[0], &col_lb[0], &col_ub[0], num_row, &Ap[0],
                &Ai[0], &Av[0], &rhs[0], &constraint_type[0]);

  // Get solver and solution information.
  // Struct ipx_info defined in ipx/include/ipx_info.h
  ipx::Info ipx_info = lps.GetInfo();

  // If not solved...
  if (solve_status != IPX_STATUS_solved) {
    const HighsStatus solve_return_status =
        reportIpxSolveStatus(options, solve_status, ipx_info.errflag);
    if (solve_return_status == HighsStatus::Error) {
      unscaled_model_status = HighsModelStatus::SOLVE_ERROR;
      return HighsStatus::Error;
    }
  }
  bool ipm_status = true;
  const HighsStatus ipm_return_status =
      reportIpxIpmCrossoverStatus(options, ipx_info.status_ipm, ipm_status);
  ipm_status = false;
  const HighsStatus crossover_return_status = reportIpxIpmCrossoverStatus(
      options, ipx_info.status_crossover, ipm_status);
  if (ipm_return_status == HighsStatus::Error ||
      crossover_return_status == HighsStatus::Error) {
    unscaled_model_status = HighsModelStatus::SOLVE_ERROR;
    return HighsStatus::Error;
  }
  // Reach here if Solve() returned IPX_STATUS_solved or IPX_STATUS_stopped
  assert(solve_status == IPX_STATUS_solved ||
         solve_status == IPX_STATUS_stopped);
  // Reach here unless one of ipm_return_status and crossover_return_status is
  // IPX_STATUS_failed or IPX_STATUS_debug

  unscaled_solution_params.ipm_iteration_count = (int)ipx_info.iter;

  if (solve_status == IPX_STATUS_stopped) {
    printf("Why stopped???\n");
    // Look at the reason why IPX stopped
    // Cannot be stopped with primal or dual infeasibility?
    assert(ipx_info.status_ipm != IPX_STATUS_primal_infeas);
    assert(ipx_info.status_ipm != IPX_STATUS_dual_infeas);
    assert(ipx_info.status_crossover != IPX_STATUS_primal_infeas);
    assert(ipx_info.status_crossover != IPX_STATUS_dual_infeas);
    if (ipx_info.status_ipm == IPX_STATUS_time_limit ||
        ipx_info.status_crossover == IPX_STATUS_time_limit) {
      // Reached time limit
      unscaled_model_status = HighsModelStatus::REACHED_TIME_LIMIT;
      return HighsStatus::Warning;
    } else if (ipx_info.status_ipm == IPX_STATUS_iter_limit ||
               ipx_info.status_crossover == IPX_STATUS_iter_limit) {
      // Reached iteration limit
      // Crossover appears not to have an iteration limit
      assert(ipx_info.status_crossover != IPX_STATUS_iter_limit);
      unscaled_model_status = HighsModelStatus::REACHED_ITERATION_LIMIT;
      return HighsStatus::Warning;
    }
  }
  // Reach here if Solve() returned IPX_STATUS_solved
  assert(solve_status == IPX_STATUS_solved);
  // Cannot be solved and reached time limit or iteration limit
  assert(ipx_info.status_ipm != IPX_STATUS_iter_limit &&
         ipx_info.status_crossover != IPX_STATUS_iter_limit);
  assert(ipx_info.status_ipm != IPX_STATUS_time_limit &&
         ipx_info.status_crossover != IPX_STATUS_time_limit);

  if (ipx_info.status_ipm == IPX_STATUS_primal_infeas ||
      ipx_info.status_crossover == IPX_STATUS_primal_infeas) {
    // Identified primal infeasibility
    // Crossover does not (currently) identify primal infeasibility
    assert(ipx_info.status_crossover != IPX_STATUS_primal_infeas);
    unscaled_model_status = HighsModelStatus::PRIMAL_INFEASIBLE;
    return HighsStatus::OK;
  } else if (ipx_info.status_ipm == IPX_STATUS_dual_infeas ||
             ipx_info.status_crossover == IPX_STATUS_dual_infeas) {
    // Identified dual infeasibility
    // Crossover does not (currently) identify dual infeasibility
    assert(ipx_info.status_crossover != IPX_STATUS_dual_infeas);
    unscaled_model_status = HighsModelStatus::PRIMAL_UNBOUNDED;
    return HighsStatus::OK;
  }

  // Get the interior solution (available if IPM was started).
  // GetInteriorSolution() returns the final IPM iterate, regardless if the
  // IPM terminated successfully or not. (Only in case of out-of-memory no
  // solution exists.)
  std::vector<double> x(num_col);
  std::vector<double> xl(num_col);
  std::vector<double> xu(num_col);
  std::vector<double> zl(num_col);
  std::vector<double> zu(num_col);
  std::vector<double> slack(num_row);
  std::vector<double> y(num_row);

  lps.GetInteriorSolution(&x[0], &xl[0], &xu[0], &slack[0], &y[0], &zl[0],
                          &zu[0]);

  if (ipx_info.status_crossover == IPX_STATUS_optimal ||
      ipx_info.status_crossover == IPX_STATUS_imprecise) {
    if (ipx_info.status_crossover == IPX_STATUS_imprecise) {
      HighsLogMessage(
          options.logfile, HighsMessageType::WARNING,
          "Ipx Crossover status imprecise: at least one of primal and dual "
          "infeasibilities of basic solution is not within parameters pfeastol "
          "and dfeastol. Simplex clean up will be required");
      // const double abs_presidual = ipx_info.abs_presidual;
      // const double abs_dresidual = ipx_info.abs_dresidual;
      // const double rel_presidual = ipx_info.rel_presidual;
      // const double rel_dresidual = ipx_info.rel_dresidual;
      // const double rel_objgap = ipx_info.rel_objgap;
    }

    IpxSolution ipx_solution;
    ipx_solution.num_col = num_col;
    ipx_solution.num_row = num_row;
    ipx_solution.ipx_col_value.resize(num_col);
    ipx_solution.ipx_row_value.resize(num_row);
    ipx_solution.ipx_col_dual.resize(num_col);
    ipx_solution.ipx_row_dual.resize(num_row);
    ipx_solution.ipx_row_status.resize(num_row);
    ipx_solution.ipx_col_status.resize(num_col);

    lps.GetBasicSolution(
        &ipx_solution.ipx_col_value[0], &ipx_solution.ipx_row_value[0],
        &ipx_solution.ipx_row_dual[0], &ipx_solution.ipx_col_dual[0],
        &ipx_solution.ipx_row_status[0], &ipx_solution.ipx_col_status[0]);

    // Convert the IPX basic solution to a HiGHS basic solution
    ipxToHighsBasicSolution(options.logfile, lp, rhs, constraint_type,
                            ipx_solution, highs_basis, highs_solution);

    // Set optimal
#ifdef HiGHSDEV
    if (ipx_info.status_crossover != IPX_STATUS_optimal)
      printf("IPX: Setting unscaled model status erroneously to OPTIMAL\n");
#endif
    unscaled_model_status = HighsModelStatus::OPTIMAL;
    unscaled_solution_params.objective_function_value = ipx_info.objval;
    getPrimalDualInfeasibilitiesFromHighsBasicSolution(
        lp, highs_basis, highs_solution, unscaled_solution_params);
  }
  return HighsStatus::OK;
}
#endif
