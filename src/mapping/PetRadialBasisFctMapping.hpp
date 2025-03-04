#pragma once
#ifndef PRECICE_NO_PETSC

#include "mapping/RadialBasisFctBaseMapping.hpp"

#include <map>
#include <numeric>
#include <vector>
#include "config/MappingConfiguration.hpp"
#include "impl/BasisFunctions.hpp"
#include "math/math.hpp"
#include "precice/impl/versions.hpp"
#include "utils/Petsc.hpp"
namespace petsc = precice::utils::petsc;
#include "utils/Event.hpp"

// Forward declaration to friend the boost test struct
namespace MappingTests {
namespace PetRadialBasisFunctionMapping {
namespace Serial {
struct SolutionCaching;
}
} // namespace PetRadialBasisFunctionMapping
} // namespace MappingTests

namespace precice {
extern bool syncMode;
} // namespace precice

namespace precice {
namespace mapping {

namespace tests {
class PetRadialBasisFctMappingTest; // Forward declaration to friend the class
}

/**
 * @brief Mapping with radial basis functions using the Petsc library to solve the resulting system.
 *
 * With help of the input data points and values an interpolant is constructed.
 * The interpolant is formed by a weighted sum of conditionally positive radial
 * basis functions and a (low order) polynomial, and evaluated at the output
 * data points.
 *
 * The radial basis function type has to be given as template parameter.
 */
template <typename RADIAL_BASIS_FUNCTION_T>
class PetRadialBasisFctMapping : public RadialBasisFctBaseMapping<RADIAL_BASIS_FUNCTION_T> {
public:
  /**
   * @brief Constructor.
   *
   * @param[in] constraint Specifies mapping to be consistent or conservative.
   * @param[in] dimensions Dimensionality of the meshes
   * @param[in] function Radial basis function used for mapping.
   * @param[in] xDead, yDead, zDead Deactivates mapping along an axis
   * @param[in] solverRtol Relative tolerance for the linear solver.
   * @param[in] polynomial Type of polynomial augmentation
   * @param[in] preallocation Sets kind of preallocation of matrices.
   *
   * For description on convergence testing and meaning of solverRtol see http://www.mcs.anl.gov/petsc/petsc-current/docs/manualpages/KSP/KSPConvergedDefault.html#KSPConvergedDefault
   */
  PetRadialBasisFctMapping(
      Mapping::Constraint            constraint,
      int                            dimensions,
      const RADIAL_BASIS_FUNCTION_T &function,
      std::array<bool, 3>            deadAxis,
      double                         solverRtol    = 1e-9,
      Polynomial                     polynomial    = Polynomial::SEPARATE,
      Preallocation                  preallocation = Preallocation::TREE);

  /// Deletes the PETSc objects and the _deadAxis array
  virtual ~PetRadialBasisFctMapping();

  /// Computes the mapping coefficients from the in- and output mesh.
  virtual void computeMapping() override;

  /// Removes a computed mapping.
  virtual void clear() override;

  friend struct MappingTests::PetRadialBasisFunctionMapping::Serial::SolutionCaching;

private:
  /// @copydoc RadialBasisFctBaseMapping::mapConservative
  virtual void mapConservative(DataID inputDataID, DataID outputDataID) override;

  /// @copydoc RadialBasisFctBaseMapping::mapConsistent
  virtual void mapConsistent(DataID inputDataID, DataID outputDataID) override;

  /// Stores col -> value for each row. Used to return the already computed values from the preconditioning
  using VertexData = std::vector<std::vector<std::pair<int, double>>>;

  mutable logging::Logger _log{"mapping::PetRadialBasisFctMapping"};

  /// Interpolation system matrix. Evaluated basis function on the input mesh
  petsc::Matrix _matrixC;

  /// Vandermonde Matrix for linear polynomial, constructed from vertices of the input mesh
  petsc::Matrix _matrixQ;

  /// Interpolation evaluation matrix. Evaluated basis function on the output mesh
  petsc::Matrix _matrixA;

  /// Coordinates of the output mesh to evaluate the separated polynomial
  petsc::Matrix _matrixV;

  /// Interpolant of g(x) = 1 evaluated at output sites, used for rescaling
  petsc::Vector oneInterpolant;

  /// Used to solve matrixC for the RBF weighting factors
  petsc::KSPSolver _solver;

  /// Used to solve the under-determined system for the separated polynomial.
  petsc::KSPSolver _QRsolver;

  /// Maps globalIndex to local index
  AO _AOmapping;

  const double _solverRtol;

  /// Toggles the use of the additional polynomial
  Polynomial _polynomial;

  /// Toggles use of rescaled basis functions, only active when Polynomial == SEPARATE
  bool useRescaling = true;

  /// Number of coefficients for the integrated polynomial. Depends on dimension and number of dead dimensions
  size_t polyparams;

  /// Number of coefficients for the separated polynomial. Depends on dimension and number of dead dimensions
  size_t sepPolyparams;

  /// Equals to polyparams if rank == 0. Is 0 everywhere else
  size_t localPolyparams;

  /// Caches the solution from the previous iteration, used as starting value for current iteration
  std::map<unsigned int, petsc::Vector> previousSolution;

  /// Prints an INFO about the current mapping
  void printMappingInfo(int inputDataID, int dim) const;

  /// Toggles use of preallocation for matrix C and A
  const Preallocation _preallocation;

  /// The CommState used to communicate
  utils::Parallel::CommStatePtr _commState;

  void estimatePreallocationMatrixC(int rows, int cols, mesh::PtrMesh mesh);

  void estimatePreallocationMatrixA(int rows, int cols, mesh::PtrMesh mesh);

  /// Preallocate matrix C by only deriving the preallocation pattern
  /*
   * This actually involves computing the coefficients. They are not saved and thus need to be computed
   * twice. Therefore, this is not really efficient.
   */
  void computePreallocationMatrixC(const mesh::PtrMesh inMesh);

  void computePreallocationMatrixA(const mesh::PtrMesh inMesh, const mesh::PtrMesh outMesh);

  VertexData savedPreallocationMatrixC(const mesh::PtrMesh inMesh);

  VertexData savedPreallocationMatrixA(const mesh::PtrMesh inMesh, const mesh::PtrMesh outMesh);

  /// Preallocate matrix C and saves the coefficients using a boost::geometry spatial tree for neighbor search
  VertexData bgPreallocationMatrixC(const mesh::PtrMesh inMesh);

  VertexData bgPreallocationMatrixA(const mesh::PtrMesh inMesh, const mesh::PtrMesh outMesh);
};

// --------------------------------------------------- HEADER IMPLEMENTATIONS

template <typename RADIAL_BASIS_FUNCTION_T>
PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::PetRadialBasisFctMapping(
    Mapping::Constraint            constraint,
    int                            dimensions,
    const RADIAL_BASIS_FUNCTION_T &function,
    std::array<bool, 3>            deadAxis,
    double                         solverRtol,
    Polynomial                     polynomial,
    Preallocation                  preallocation)
    : RadialBasisFctBaseMapping<RADIAL_BASIS_FUNCTION_T>(constraint, dimensions, function, deadAxis),
      _matrixC("C"),
      _matrixQ("Q"),
      _matrixA("A"),
      _matrixV("V"),
      _solver("Coefficient Solver"),
      _QRsolver("QR Solver"),
      _AOmapping(nullptr),
      _solverRtol(solverRtol),
      _polynomial(polynomial),
      _preallocation(preallocation),
      _commState(utils::Parallel::current())
{
  polyparams      = (_polynomial == Polynomial::ON) ? this->getPolynomialParameters() : 0;
  sepPolyparams   = (_polynomial == Polynomial::SEPARATE) ? this->getPolynomialParameters() : 0;
  localPolyparams = (_commState->rank() > 0) ? 0 : polyparams;
}

template <typename RADIAL_BASIS_FUNCTION_T>
PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::~PetRadialBasisFctMapping()
{
  petsc::destroy(&_AOmapping);
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::computeMapping()
{
  PRECICE_TRACE();
  precice::utils::Event e("map.pet.computeMapping.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);
  precice::utils::Event ePreCompute("map.pet.preComputeMapping.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  clear();

  if (_polynomial == Polynomial::ON) {
    PRECICE_DEBUG("Using integrated polynomial.");
  }
  if (_polynomial == Polynomial::OFF) {
    PRECICE_DEBUG("Using no polynomial.");
  }
  if (_polynomial == Polynomial::SEPARATE) {
    PRECICE_DEBUG("Using separated polynomial.");
  }

  PRECICE_ASSERT(this->input()->getDimensions() == this->output()->getDimensions(),
                 this->input()->getDimensions(), this->output()->getDimensions());
  int const     dimensions = this->input()->getDimensions();
  mesh::PtrMesh inMesh;
  mesh::PtrMesh outMesh;
  if (this->hasConstraint(Mapping::CONSERVATIVE)) {
    inMesh  = this->output();
    outMesh = this->input();
  } else {
    inMesh  = this->input();
    outMesh = this->output();
  }

  // Indizes that are used to build the Petsc AO mapping
  std::vector<PetscInt> myIndizes;

  // Indizes for Q^T, holding the polynomial
  if (_commState->rank() <= 0) // Rank 0 or not in IntraComm mode
    for (size_t i = 0; i < polyparams; i++)
      myIndizes.push_back(i); // polyparams reside in the first rows (which are always on rank 0)

  // Indizes for the vertices with polyparams offset
  for (const mesh::Vertex &v : inMesh->vertices())
    if (v.isOwner())
      myIndizes.push_back(v.getGlobalIndex() + polyparams);

  auto n = myIndizes.size(); // polyparams, if on rank 0, are included here

  auto outputSize = outMesh->vertices().size();

  PetscErrorCode ierr = 0;
  PRECICE_DEBUG("inMesh->vertices().size() = {}", inMesh->vertices().size());
  PRECICE_DEBUG("outMesh->vertices().size() = {}", outMesh->vertices().size());
  ePreCompute.stop();

  precice::utils::Event eCreateMatrices("map.pet.createMatrices.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  // Matrix C: Symmetric, sparse matrix with n x n local size.
  _matrixC.init(n, n, PETSC_DETERMINE, PETSC_DETERMINE, MATSBAIJ);
  PRECICE_DEBUG("Set matrix C to local size {0} x {0}", n);
  ierr = MatSetOption(_matrixC, MAT_SYMMETRIC, PETSC_TRUE);
  CHKERRV(ierr);
  ierr = MatSetOption(_matrixC, MAT_SYMMETRY_ETERNAL, PETSC_TRUE);
  CHKERRV(ierr);

  // Matrix Q: Dense, holds the input mesh for the polynomial if set to SEPERATE. Zero size otherwise
  _matrixQ.init(n, PETSC_DETERMINE, PETSC_DETERMINE, sepPolyparams, MATDENSE);
  PRECICE_DEBUG("Set matrix Q to local size {} x {}", n, sepPolyparams);

  // Matrix V: Dense, holds the output mesh for polynomial if set to SEPERATE. Zero size otherwise
  _matrixV.init(outputSize, PETSC_DETERMINE, PETSC_DETERMINE, sepPolyparams, MATDENSE);
  PRECICE_DEBUG("Set matrix V to local size {} x {}", outputSize, sepPolyparams);

  // Matrix A: Sparse matrix with outputSize x n local size.
  _matrixA.init(outputSize, n, PETSC_DETERMINE, PETSC_DETERMINE, MATAIJ);
  PRECICE_DEBUG("Set matrix A to local size {} x {}", outputSize, n);

  eCreateMatrices.stop();
  precice::utils::Event eAO("map.pet.AO.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  auto const ownerRangeABegin = _matrixA.ownerRange().first;
  auto const ownerRangeAEnd   = _matrixA.ownerRange().second;

  // A mapping from globalIndex -> local col/row
  ierr = AOCreateMapping(_commState->comm, myIndizes.size(), myIndizes.data(), nullptr, &_AOmapping);
  CHKERRV(ierr);

  eAO.stop();

  Eigen::VectorXd distance(dimensions);

  // We do preallocating of the matrices C and A. That means we traverse the input data once, just
  // to know where we have entries in the sparse matrix. This information petsc can use to
  // preallocate the matrix. In the second phase we actually fill the matrix.

  // Stores col -> value for each row;
  VertexData vertexData;

  if (_preallocation == Preallocation::SAVE) {
    vertexData = savedPreallocationMatrixC(inMesh);
  }
  if (_preallocation == Preallocation::COMPUTE) {
    computePreallocationMatrixC(inMesh);
  }
  if (_preallocation == Preallocation::ESTIMATE) {
    estimatePreallocationMatrixC(n, n, inMesh);
  }
  if (_preallocation == Preallocation::TREE) {
    vertexData = bgPreallocationMatrixC(inMesh);
  }

  // -- BEGIN FILL LOOP FOR MATRIX C --
  PRECICE_DEBUG("Begin filling matrix C");
  precice::utils::Event eFillC("map.pet.fillC.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  // We collect entries for each row and set them blockwise using MatSetValues.
  PetscInt const           idxSize = std::max(_matrixC.getSize().second, _matrixQ.getSize().second);
  std::vector<PetscInt>    colIdx(idxSize);  // holds the columns indices of the entries
  std::vector<PetscScalar> rowVals(idxSize); // holds the values of the entries

  int      preallocRow = 0;
  PetscInt row         = _matrixC.ownerRange().first + localPolyparams;
  for (const mesh::Vertex &inVertex : inMesh->vertices()) {
    if (not inVertex.isOwner())
      continue;

    PetscInt colNum = 0; // holds the number of non-zero columns in current row

    // -- SETS THE POLYNOMIAL PART OF THE MATRIX --
    if (_polynomial == Polynomial::ON or _polynomial == Polynomial::SEPARATE) {
      colIdx[colNum]    = colNum;
      rowVals[colNum++] = 1;

      for (int dim = 0; dim < dimensions; dim++) {
        if (not this->_deadAxis[dim]) {
          colIdx[colNum]    = colNum;
          rowVals[colNum++] = inVertex.getCoords()[dim];
        }
      }

      // cols are always the first ones for the polynomial, no need to translate
      if (_polynomial == Polynomial::ON) {
        ierr = MatSetValues(_matrixC, colNum, colIdx.data(), 1, &row, rowVals.data(), INSERT_VALUES);
        CHKERRV(ierr);
      } else if (_polynomial == Polynomial::SEPARATE) {
        ierr = MatSetValues(_matrixQ, 1, &row, colNum, colIdx.data(), rowVals.data(), INSERT_VALUES);
        CHKERRV(ierr);
      }
      colNum = 0;
    }

    // -- SETS THE COEFFICIENTS --
    if (_preallocation == Preallocation::SAVE or _preallocation == Preallocation::TREE) {
      auto const &rowVertices = vertexData[preallocRow];
      for (const auto &vertex : rowVertices) {
        rowVals[colNum]  = this->_basisFunction.evaluate(vertex.second);
        colIdx[colNum++] = vertex.first;
      }
      ++preallocRow;
    } else {
      for (const mesh::Vertex &vj : inMesh->vertices()) {
        int const col = vj.getGlobalIndex() + polyparams;
        if (row > col)
          continue; // matrix is symmetric
        distance = inVertex.getCoords() - vj.getCoords();
        for (int d = 0; d < dimensions; d++) {
          if (this->_deadAxis[d]) {
            distance[d] = 0;
          }
        }
        double const norm = distance.norm();
        if (this->_basisFunction.getSupportRadius() > norm) {
          rowVals[colNum]  = this->_basisFunction.evaluate(norm);
          colIdx[colNum++] = col; // column of entry is the globalIndex
        }
      }
    }
    ierr = AOApplicationToPetsc(_AOmapping, colNum, colIdx.data());
    CHKERRV(ierr);
    ierr = MatSetValues(_matrixC, 1, &row, colNum, colIdx.data(), rowVals.data(), INSERT_VALUES);
    CHKERRV(ierr);
    ++row;
  }
  PRECICE_DEBUG("Finished filling Matrix C");
  eFillC.stop();
  // -- END FILL LOOP FOR MATRIX C --

  // PETSc requires that all diagonal entries are set, even if set to zero.
  _matrixC.assemble(MAT_FLUSH_ASSEMBLY);
  auto zeros = petsc::Vector::allocate(_matrixC);
  VecZeroEntries(zeros);
  zeros.assemble();
  //MatDiagonalSet(_matrixC, zeros, INSERT_VALUES);
  MatDiagonalSet(_matrixC, zeros, ADD_VALUES);

  // Begin assembly here, all assembly is ended at the end of this function.
  ierr = MatAssemblyBegin(_matrixC, MAT_FINAL_ASSEMBLY);
  CHKERRV(ierr);
  ierr = MatAssemblyBegin(_matrixQ, MAT_FINAL_ASSEMBLY);
  CHKERRV(ierr);

  if (_preallocation == Preallocation::SAVE) {
    vertexData = savedPreallocationMatrixA(inMesh, outMesh);
  }
  if (_preallocation == Preallocation::COMPUTE) {
    computePreallocationMatrixA(inMesh, outMesh);
  }
  if (_preallocation == Preallocation::ESTIMATE) {
    estimatePreallocationMatrixA(outputSize, n, inMesh);
  }
  if (_preallocation == Preallocation::TREE) {
    vertexData = bgPreallocationMatrixA(inMesh, outMesh);
  }

  // holds the columns indices of the entries
  colIdx.resize(std::max(_matrixA.getSize().second, _matrixV.getSize().second));
  // holds the values of the entries
  rowVals.resize(std::max(_matrixA.getSize().second, _matrixV.getSize().second));

  // -- BEGIN FILL LOOP FOR MATRIX A --
  PRECICE_DEBUG("Begin filling matrix A.");
  precice::utils::Event eFillA("map.pet.fillA.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  for (PetscInt row = ownerRangeABegin; row < ownerRangeAEnd; ++row) {
    mesh::Vertex const &oVertex = outMesh->vertices()[row - _matrixA.ownerRange().first];

    // -- SET THE POLYNOMIAL PART OF THE MATRIX --
    if (_polynomial == Polynomial::ON or _polynomial == Polynomial::SEPARATE) {
      petsc::Matrix *m      = _polynomial == Polynomial::ON ? &_matrixA : &_matrixV;
      PetscInt       colNum = 0;

      colIdx[colNum]    = colNum;
      rowVals[colNum++] = 1;

      for (int dim = 0; dim < dimensions; dim++) {
        if (not this->_deadAxis[dim]) {
          colIdx[colNum]    = colNum;
          rowVals[colNum++] = oVertex.getCoords()[dim];
        }
      }
      ierr = MatSetValues(*m, 1, &row, colNum, colIdx.data(), rowVals.data(), INSERT_VALUES);
      CHKERRV(ierr);
    }

    // -- SETS THE COEFFICIENTS --
    PetscInt colNum = 0;

    if (_preallocation == Preallocation::SAVE or _preallocation == Preallocation::TREE) {
      auto const &rowVertices = vertexData[row - ownerRangeABegin];
      for (const auto &vertex : rowVertices) {
        rowVals[colNum]  = this->_basisFunction.evaluate(vertex.second);
        colIdx[colNum++] = vertex.first;
      }
    } else {
      for (const mesh::Vertex &inVertex : inMesh->vertices()) {
        distance = oVertex.getCoords() - inVertex.getCoords();
        for (int d = 0; d < dimensions; d++) {
          if (this->_deadAxis[d])
            distance[d] = 0;
        }
        double const norm = distance.norm();
        if (this->_basisFunction.getSupportRadius() > norm) {
          rowVals[colNum]  = this->_basisFunction.evaluate(norm);
          colIdx[colNum++] = inVertex.getGlobalIndex() + polyparams;
        }
      }
    }
    ierr = AOApplicationToPetsc(_AOmapping, colNum, colIdx.data());
    CHKERRV(ierr);
    ierr = MatSetValues(_matrixA, 1, &row, colNum, colIdx.data(), rowVals.data(), INSERT_VALUES);
    CHKERRV(ierr);
  }
  PRECICE_DEBUG("Finished filling Matrix A");
  eFillA.stop();
  // -- END FILL LOOP FOR MATRIX A --

  precice::utils::Event ePostFill("map.pet.postFill.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  ierr = MatAssemblyBegin(_matrixA, MAT_FINAL_ASSEMBLY);
  CHKERRV(ierr);

  ierr = MatAssemblyEnd(_matrixC, MAT_FINAL_ASSEMBLY);
  CHKERRV(ierr);
  ierr = MatAssemblyEnd(_matrixQ, MAT_FINAL_ASSEMBLY);
  CHKERRV(ierr);
  ierr = MatAssemblyEnd(_matrixA, MAT_FINAL_ASSEMBLY);
  CHKERRV(ierr);
  _matrixV.assemble();

  ePostFill.stop();

  precice::utils::Event eSolverInit("map.pet.solverInit.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  // -- CONFIGURE SOLVER FOR POLYNOMIAL --
  if (_polynomial == Polynomial::SEPARATE) {
    PC pc;
    KSPGetPC(_QRsolver, &pc);
    PCSetType(pc, PCNONE);
    KSPSetType(_QRsolver, KSPLSQR);
    KSPSetOperators(_QRsolver, _matrixQ, _matrixQ);
  }

  // -- CONFIGURE SOLVER FOR SYSTEM MATRIX --
  KSPSetOperators(_solver, _matrixC, _matrixC);
  CHKERRV(ierr);
  // The fourth argument defines the divergence tolerance, i.e. the tolerance, for which the linear system is
  // considered as diverged and the computation is aborted. The PETSC_DEFAULT value is 1e9. However, the
  // divergence residual is scaled using the RHS b of the system. In case the RHS is (still nonzero) very small
  // and we have a (bad) nonzero initial guess x as defined below, the divergence tolerance might be exceeded
  // in the first iteration, although the system could be solved in the usual way. This behavior is essentially
  // a glitch in the divergence check. Therefore, we select a very high value (1e30) in order to disable the
  // divergence check. In practice, the check is very rarely needed with Krylov methods. According to the PETSc
  // people, the rare use cases aim for a bad preconditioner, which is not even used in our configuration.
  // Hence, we can disable the divergence check without concerns.
  KSPSetTolerances(_solver, _solverRtol, PETSC_DEFAULT, 1e30, PETSC_DEFAULT);
  KSPSetInitialGuessNonzero(_solver, PETSC_TRUE);
  CHKERRV(ierr);                            // Reuse the results from the last iteration, held in the out vector.
  KSPSetOptionsPrefix(_solver, "solverC_"); // s.t. options for only this solver can be set on the command line
  KSPSetFromOptions(_solver);

  eSolverInit.stop();

  // if (totalNNZ > static_cast<size_t>(20*n)) {
  //   PRECICE_DEBUG("Using Cholesky decomposition as direct solver for dense matrix.");
  //   PC prec;
  //   KSPSetType(_solver, KSPPREONLY);
  //   KSPGetPC(_solver, &prec);
  //   PCSetType(prec, PCCHOLESKY);
  //   PCFactorSetShiftType(prec, MAT_SHIFT_NONZERO);
  // }

  // -- COMPUTE RESCALING COEFFICIENTS USING THE SYSTEM MATRIX C SOLVER --
  if (useRescaling and (_polynomial == Polynomial::SEPARATE)) {
    precice::utils::Event eRescaling("map.pet.computeRescaling.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);
    auto                  rhs             = petsc::Vector::allocate(_matrixC);
    auto                  rescalingCoeffs = petsc::Vector::allocate(_matrixC);
    VecSet(rhs, 1);
    rhs.assemble();
    if (_solver.solve(rhs, rescalingCoeffs) == petsc::KSPSolver::SolverResult::Converged) {
      PRECICE_INFO("Using rescaling. {}", _solver.summaryFor(rhs));
    } else {
      PRECICE_WARN("Deactivating rescaling! {}", _solver.summaryFor(rhs));
      useRescaling = false;
    }

    eRescaling.addData("Iterations", _solver.getIterationNumber());
    ierr = MatCreateVecs(_matrixA, nullptr, &oneInterpolant.vector);
    CHKERRV(ierr);
    ierr = MatMult(_matrixA, rescalingCoeffs, oneInterpolant);
    CHKERRV(ierr); // get the output of g(x) = 1
    // set values close to zero to exactly 0.0, s.t. PointwiseDevide doesn't do division on these entries
    ierr = VecChop(oneInterpolant, 1e-6);
    CHKERRV(ierr);
  }

  this->_hasComputedMapping = true;

  PRECICE_DEBUG("Number of mallocs for matrix C = {}", _matrixC.getInfo(MAT_LOCAL).mallocs);
  PRECICE_DEBUG("Non-zeros allocated / used / unused for matrix C = {} / {} / {}",
                _matrixC.getInfo(MAT_LOCAL).nz_allocated, _matrixC.getInfo(MAT_LOCAL).nz_used, _matrixC.getInfo(MAT_LOCAL).nz_unneeded);
  PRECICE_DEBUG("Number of mallocs for matrix A = {}", _matrixA.getInfo(MAT_LOCAL).mallocs);
  PRECICE_DEBUG("Non-zeros allocated / used / unused for matrix A = {} / {} / {}",
                _matrixA.getInfo(MAT_LOCAL).nz_allocated, _matrixA.getInfo(MAT_LOCAL).nz_used, _matrixA.getInfo(MAT_LOCAL).nz_unneeded);
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::clear()
{
  _matrixC.reset();
  _matrixA.reset();
  _matrixQ.reset();
  _matrixV.reset();

  _solver.reset();
  _QRsolver.reset();

  petsc::destroy(&_AOmapping);

  previousSolution.clear();
  this->_hasComputedMapping = false;
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::mapConsistent(DataID inputDataID, DataID outputDataID)
{
  PRECICE_TRACE(inputDataID, outputDataID);
  precice::utils::Event e("map.pet.mapData.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscErrorCode ierr      = 0;
  auto const &   inValues  = this->input()->data(inputDataID)->values();
  auto &         outValues = this->output()->data(outputDataID)->values();

  int const valueDim = this->input()->data(inputDataID)->getDimensions();
  PRECICE_ASSERT(valueDim == this->output()->data(outputDataID)->getDimensions(),
                 valueDim, this->output()->data(outputDataID)->getDimensions());
  PRECICE_ASSERT(this->hasConstraint(Mapping::CONSISTENT) || this->hasConstraint(Mapping::SCALEDCONSISTENT));

  auto out = petsc::Vector::allocate(_matrixA, "out");
  auto in  = petsc::Vector::allocate(_matrixC, "in");
  auto a   = petsc::Vector::allocate(_matrixQ, "a", petsc::Vector::RIGHT); // holds the solution of the LS polynomial

  PetscScalar const *vecArray;

  // For every data dimension, perform mapping
  for (int dim = 0; dim < valueDim; dim++) {
    printMappingInfo(inputDataID, dim);

    // Fill input from input data values
    std::vector<PetscScalar> inVals;
    inVals.reserve(this->input()->vertices().size());
    std::vector<PetscInt> inIdx;
    inIdx.reserve(this->input()->vertices().size());
    for (size_t i = 0; i < this->input()->vertices().size(); ++i) {
      if (not this->input()->vertices()[i].isOwner())
        continue;
      inVals.emplace_back(inValues[i * valueDim + dim]);
      inIdx.emplace_back(inIdx.size() + in.ownerRange().first + localPolyparams);
    }
    ierr = VecSetValues(in, inIdx.size(), inIdx.data(), inVals.data(), INSERT_VALUES);
    CHKERRV(ierr);
    in.assemble();

    if (_polynomial == Polynomial::SEPARATE) {
      switch (_QRsolver.solve(in, a)) {
      case (petsc::KSPSolver::SolverResult::Converged):
        PRECICE_DEBUG("The polynomial QR system of the RBF mapping from mesh {} to mesh {} converged. {}",
                      this->input()->getName(), this->output()->getName(), _QRsolver.summaryFor(in));
        break;
      case (petsc::KSPSolver::SolverResult::Stopped):
        PRECICE_WARN("The polynomial QR system of the RBF mapping from mesh {} to mesh {} has not converged. "
                     "This means most probably that the mapping problem is not well-posed or your relative tolerance is too conservative. "
                     "Please check if your coupling meshes are correct. "
                     "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                     this->input()->getName(), this->output()->getName(), _QRsolver.summaryFor(in));
        break;
      case (petsc::KSPSolver::SolverResult::Diverged):
        KSPView(_QRsolver, PETSC_VIEWER_STDOUT_WORLD);
        PRECICE_ERROR("The polynomial QR system of the RBF mapping from mesh {} to mesh {} has diverged. "
                      "This means most probably that the mapping problem is not well-posed. "
                      "Please check if your coupling meshes are correct. "
                      "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                      this->input()->getName(), this->output()->getName(), _QRsolver.summaryFor(in));
        break;
      }

      VecScale(a, -1);
      MatMultAdd(_matrixQ, a, in, in); // Subtract the polynomial from the input values
    }

    petsc::Vector &p = std::get<0>( // Save and reuse the solution from the previous iteration
                           previousSolution.emplace(std::piecewise_construct,
                                                    std::forward_as_tuple(inputDataID + outputDataID * 10 + dim * 100),
                                                    std::forward_as_tuple(petsc::Vector::allocate(_matrixC, "p"))))
                           ->second;

    utils::Event eSolve("map.pet.solveConsistent.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);
    const auto   solverResult = _solver.solve(in, p);
    eSolve.addData("Iterations", _solver.getIterationNumber());
    eSolve.stop();

    switch (solverResult) {
    case (petsc::KSPSolver::SolverResult::Converged):
      PRECICE_DEBUG("The linear system of the RBF mapping from mesh {} to mesh {} converged. {}",
                    this->input()->getName(), this->output()->getName(), _solver.summaryFor(in));
      break;
    case (petsc::KSPSolver::SolverResult::Stopped):
      PRECICE_WARN("The linear system of the RBF mapping from mesh {} to mesh {} has not converged. "
                   "This means most probably that the mapping problem is not well-posed or your relative tolerance is too conservative. "
                   "Please check if your coupling meshes are correct. "
                   "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                   this->input()->getName(), this->output()->getName(), _solver.summaryFor(in));
      break;
    case (petsc::KSPSolver::SolverResult::Diverged):
      KSPView(_solver, PETSC_VIEWER_STDOUT_WORLD);
      PRECICE_ERROR("The linear system of the RBF mapping from mesh {} to mesh {} has diverged. "
                    "This means most probably that the mapping problem is not well-posed. "
                    "Please check if your coupling meshes are correct. "
                    "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                    this->input()->getName(), this->output()->getName(), _solver.summaryFor(in));
      break;
    }

    ierr = MatMult(_matrixA, p, out);
    CHKERRV(ierr);

    if (useRescaling and _polynomial == Polynomial::SEPARATE) {
      ierr = VecPointwiseDivide(out, out, oneInterpolant);
      CHKERRV(ierr);
    }

    if (_polynomial == Polynomial::SEPARATE) {
      ierr = VecScale(a, -1); // scale it back to add the polynomial
      ierr = MatMultAdd(_matrixV, a, out, out);
      CHKERRV(ierr);
    }
    VecChop(out, 1e-9);

    // Copy mapped data to output data values
    ierr = VecGetArrayRead(out, &vecArray);
    CHKERRV(ierr);
    int size = out.getLocalSize();
    for (int i = 0; i < size; i++) {
      outValues[i * valueDim + dim] = vecArray[i];
    }

    VecRestoreArrayRead(out, &vecArray);
  }
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::mapConservative(DataID inputDataID, DataID outputDataID)
{
  PRECICE_TRACE(inputDataID, outputDataID);
  precice::utils::Event e("map.pet.mapData.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscErrorCode ierr      = 0;
  auto const &   inValues  = this->input()->data(inputDataID)->values();
  auto &         outValues = this->output()->data(outputDataID)->values();

  int const valueDim = this->input()->data(inputDataID)->getDimensions();
  PRECICE_ASSERT(valueDim == this->output()->data(outputDataID)->getDimensions(),
                 valueDim, this->output()->data(outputDataID)->getDimensions());
  PRECICE_ASSERT(this->hasConstraint(Mapping::CONSERVATIVE));

  auto au = petsc::Vector::allocate(_matrixA, "au", petsc::Vector::RIGHT);
  auto in = petsc::Vector::allocate(_matrixA, "in");
  int  inRangeStart, inRangeEnd;
  std::tie(inRangeStart, inRangeEnd) = in.ownerRange();
  for (int dim = 0; dim < valueDim; dim++) {
    printMappingInfo(inputDataID, dim);

    // Fill input from input data values
    for (size_t i = 0; i < this->input()->vertices().size(); i++) {
      auto const globalIndex = this->input()->vertices()[i].getGlobalIndex(); // globalIndex is target row
      if (globalIndex >= inRangeStart and globalIndex < inRangeEnd)           // only fill local rows
        ierr = VecSetValue(in, globalIndex, inValues[i * valueDim + dim], INSERT_VALUES);
      CHKERRV(ierr);
    }
    in.assemble();

    // Gets the petsc::vector for the given combination of outputData, inputData and dimension
    // If none created yet, create one, based on _matrixC
    petsc::Vector &out = std::get<0>(
                             previousSolution.emplace(std::piecewise_construct,
                                                      std::forward_as_tuple(inputDataID + outputDataID * 10 + dim * 100),
                                                      std::forward_as_tuple(petsc::Vector::allocate(_matrixC, "out"))))
                             ->second;

    if (_polynomial == Polynomial::SEPARATE) {
      auto epsilon = petsc::Vector::allocate(_matrixV, "epsilon", petsc::Vector::RIGHT);
      ierr         = MatMultTranspose(_matrixV, in, epsilon);
      CHKERRV(ierr);
      auto eta = petsc::Vector::allocate(_matrixA, "eta", petsc::Vector::RIGHT);
      ierr     = MatMultTranspose(_matrixA, in, eta);
      CHKERRV(ierr);
      auto mu = petsc::Vector::allocate(_matrixC, "mu", petsc::Vector::LEFT);
      _solver.solve(eta, mu);
      VecScale(epsilon, -1);
      auto tau = petsc::Vector::allocate(_matrixQ, "tau", petsc::Vector::RIGHT);
      ierr     = MatMultTransposeAdd(_matrixQ, mu, epsilon, tau);
      CHKERRV(ierr);
      auto sigma = petsc::Vector::allocate(_matrixQ, "sigma", petsc::Vector::LEFT);

      switch (_QRsolver.solveTranspose(tau, sigma)) {
      case (petsc::KSPSolver::SolverResult::Converged):
        PRECICE_DEBUG("The polynomial linear system of the RBF mapping from mesh {} to mesh {} converged. {}",
                      this->input()->getName(), this->output()->getName(), _QRsolver.summaryFor(tau));
        break;
      case (petsc::KSPSolver::SolverResult::Stopped):
        PRECICE_WARN("The polynomial linear system of the RBF mapping from mesh {} to mesh {} has not converged. "
                     "This means most probably that the mapping problem is not well-posed or your relative tolerance is too conservative. "
                     "Please check if your coupling meshes are correct. "
                     "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                     this->input()->getName(), this->output()->getName(), _QRsolver.summaryFor(tau));
        break;
      case (petsc::KSPSolver::SolverResult::Diverged):
        KSPView(_QRsolver, PETSC_VIEWER_STDOUT_WORLD);
        PRECICE_ERROR("The polynomial linear system of the RBF mapping from mesh {} to mesh {} "
                      "has diverged. This means most probably that the mapping problem is not well-posed. "
                      "Please check if your coupling meshes are correct. "
                      "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                      this->input()->getName(), this->output()->getName(), _QRsolver.summaryFor(tau));
        break;
      }

      VecWAXPY(out, -1, sigma, mu);
    } else {
      ierr = MatMultTranspose(_matrixA, in, au);
      CHKERRV(ierr);
      utils::Event eSolve("map.pet.solveConservative.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);
      const auto   solverResult = _solver.solve(au, out);
      eSolve.addData("Iterations", _solver.getIterationNumber());
      eSolve.stop();

      switch (solverResult) {
      case (petsc::KSPSolver::SolverResult::Converged):
        PRECICE_DEBUG("The linear system of the RBF mapping from mesh {} to mesh {} converged. {}",
                      this->input()->getName(), this->output()->getName(), _solver.summaryFor(au));
        break;
      case (petsc::KSPSolver::SolverResult::Stopped):
        PRECICE_WARN("The linear system of the RBF mapping from mesh {} to mesh {} has not converged. "
                     "This means most probably that the mapping problem is not well-posed or your relative tolerance is too conservative. "
                     "Please check if your coupling meshes are correct. "
                     "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                     this->input()->getName(), this->output()->getName(), _solver.summaryFor(au));
        break;
      case (petsc::KSPSolver::SolverResult::Diverged):
        KSPView(_solver, PETSC_VIEWER_STDOUT_WORLD);
        PRECICE_ERROR("The linear system of the RBF mapping from mesh {} to mesh {} "
                      "has diverged. This means most probably that the mapping problem is not well-posed. "
                      "Please check if your coupling meshes are correct. "
                      "Maybe you need to fix axis-aligned mapping setups by marking perpendicular axes as dead? {}",
                      this->input()->getName(), this->output()->getName(), _solver.summaryFor(au));
        break;
      }
    }

    VecChop(out, 1e-9);

    // Copy mapped data to output data values
    const PetscScalar *outArray;
    VecGetArrayRead(out, &outArray);

    int count = 0, ownerCount = 0;
    for (const mesh::Vertex &vertex : this->output()->vertices()) {
      if (vertex.isOwner()) {
        outValues[count * valueDim + dim] = outArray[ownerCount + localPolyparams];
        ownerCount++;
      } else {
        PRECICE_ASSERT(outValues[count * valueDim + dim] == 0.0);
      }
      count++;
    }
    VecRestoreArrayRead(out, &outArray);
  }
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::printMappingInfo(int inputDataID, int dim) const
{
  std::string constraintName;
  if (this->hasConstraint(Mapping::CONSISTENT)) {
    constraintName = "consistent";
  } else if (this->hasConstraint(Mapping::SCALEDCONSISTENT)) {
    constraintName = "scaled-consistent";
  } else {
    constraintName = "conservative";
  }

  const std::string polynomialName = _polynomial == Polynomial::ON ? "on" : _polynomial == Polynomial::OFF ? "off" : "separate";

  PRECICE_INFO("Mapping \"{}\" {} from \"{}\" (ID {}) to \"{}\" (ID {}) for dimension {} with polynomial set to {}",
               this->input()->data(inputDataID)->getName(), constraintName,
               this->input()->getName(), this->input()->getID(),
               this->output()->getName(), this->output()->getID(),
               dim, polynomialName);
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::estimatePreallocationMatrixC(
    int rows, int cols, mesh::PtrMesh mesh)
{
  std::ignore = rows;
  std::ignore = cols;

  // auto rank = _commState->rank();
  auto size          = _commState->size();
  auto supportRadius = this->_basisFunction.getSupportRadius();

  auto bbox     = mesh->getBoundingBox();
  auto meshSize = mesh->vertices().size();

  double meshArea = bbox.getArea(this->_deadAxis);
  // PRECICE_WARN(bbox);

  // supportVolume = math::PI * 4.0/3.0 * std::pow(supportRadius, 3);
  double supportVolume = 0;
  if (this->getDimensions() == 2)
    supportVolume = 2 * supportRadius;
  else if (this->getDimensions() == 3)
    supportVolume = math::PI * std::pow(supportRadius, 2);

  PRECICE_WARN("supportVolume = {}", supportVolume);
  PRECICE_WARN("meshArea = {}", meshArea);
  PRECICE_WARN("meshSize = {}", meshSize);

  int nnzPerRow = meshSize / meshArea * supportVolume;
  PRECICE_WARN("nnzPerRow = {}", nnzPerRow);
  // int nnz = nnzPerRow * rows;

  if (_commState->size() == 1) {
    MatSeqSBAIJSetPreallocation(_matrixC, _matrixC.blockSize(), nnzPerRow / 2, nullptr);
  } else {
    MatMPISBAIJSetPreallocation(_matrixC, _matrixC.blockSize(),
                                nnzPerRow / (size * 2), nullptr, nnzPerRow / (size * 2), nullptr);
  }
  MatSetOption(_matrixC, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_FALSE);
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::estimatePreallocationMatrixA(
    int rows, int cols, mesh::PtrMesh mesh)
{
  std::ignore = rows;
  std::ignore = cols;

  // auto rank = _commState->rank();
  auto size          = _commState->size();
  auto supportRadius = this->_basisFunction.getSupportRadius();

  auto bbox     = mesh->getBoundingBox();
  auto meshSize = mesh->vertices().size();

  double meshArea = bbox.getArea(this->_deadAxis);
  // PRECICE_WARN(bbox);

  // supportVolume = math::PI * 4.0/3.0 * std::pow(supportRadius, 3);
  double supportVolume = 0;
  if (this->getDimensions() == 2)
    supportVolume = 2 * supportRadius;
  else if (this->getDimensions() == 3)
    supportVolume = math::PI * std::pow(supportRadius, 2);

  PRECICE_WARN("supportVolume = {}", supportVolume);
  PRECICE_WARN("meshArea = {}", meshArea);
  PRECICE_WARN("meshSize = {}", meshSize);

  int nnzPerRow = meshSize / meshArea * supportVolume;
  PRECICE_WARN("nnzPerRow = {}", nnzPerRow);
  // int nnz = nnzPerRow * rows;

  if (_commState->size() == 1) {
    MatSeqSBAIJSetPreallocation(_matrixA, _matrixA.blockSize(), nnzPerRow, nullptr);
  } else {
    MatMPISBAIJSetPreallocation(_matrixA, _matrixA.blockSize(),
                                nnzPerRow / size, nullptr, nnzPerRow / size, nullptr);
  }
  MatSetOption(_matrixA, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_FALSE);
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::computePreallocationMatrixC(const mesh::PtrMesh inMesh)
{
  precice::utils::Event ePreallocC("map.pet.preallocC.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscInt n, ierr;

  const int       dimensions = this->input()->getDimensions();
  Eigen::VectorXd distance(dimensions);

  std::tie(n, std::ignore) = _matrixC.getLocalSize();
  std::vector<PetscInt> d_nnz(n), o_nnz(n);
  PetscInt              colOwnerRangeCBegin, colOwnerRangeCEnd;
  std::tie(colOwnerRangeCBegin, colOwnerRangeCEnd) = _matrixC.ownerRangeColumn();

  size_t local_row = 0;
  // -- PREALLOCATES THE POLYNOMIAL PART OF THE MATRIX --
  if (_polynomial == Polynomial::ON) {
    for (local_row = 0; local_row < localPolyparams; local_row++) {
      d_nnz[local_row] = colOwnerRangeCEnd - colOwnerRangeCBegin;
      o_nnz[local_row] = _matrixC.getSize().first - d_nnz[local_row];
    }
  }

  for (const mesh::Vertex &inVertex : inMesh->vertices()) {
    if (not inVertex.isOwner())
      continue;

    PetscInt  col        = polyparams - 1;
    const int global_row = local_row + _matrixC.ownerRange().first;
    d_nnz[local_row]     = 0;
    o_nnz[local_row]     = 0;

    // -- PREALLOCATES THE COEFFICIENTS --
    for (mesh::Vertex &vj : inMesh->vertices()) {
      col++;

      // the actual col where the vertex end up
      PetscInt mappedCol = vj.getGlobalIndex() + polyparams;
      ierr               = AOApplicationToPetsc(_AOmapping, 1, &mappedCol);
      CHKERRV(ierr); // likely not efficient in the inner loop

      if (global_row > mappedCol) // Skip, since we are below the diagonal
        continue;

      distance = inVertex.getCoords() - vj.getCoords();
      for (int d = 0; d < dimensions; d++) {
        if (this->_deadAxis[d]) {
          distance[d] = 0;
        }
      }

      if (this->_basisFunction.getSupportRadius() > distance.norm() or col == global_row) {
        if (mappedCol >= colOwnerRangeCBegin and mappedCol < colOwnerRangeCEnd)
          d_nnz[local_row]++;
        else
          o_nnz[local_row]++;
      }
    }
    local_row++;
  }

  if (_commState->size() == 1) {
    // std::cout << "Computed Preallocation C Seq diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0) << '\n';
    ierr = MatSeqSBAIJSetPreallocation(_matrixC, _matrixC.blockSize(), 0, d_nnz.data());
    CHKERRV(ierr);
  } else {
    // std::cout << "Computed Preallocation C MPI diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0)
    // << ", off-diagonal = " << std::accumulate(o_nnz.begin(), o_nnz.end(), 0) << '\n';
    ierr = MatMPISBAIJSetPreallocation(_matrixC, _matrixC.blockSize(), 0, d_nnz.data(), 0, o_nnz.data());
    CHKERRV(ierr);
  }
  MatSetOption(_matrixC, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_TRUE);

  ePreallocC.stop();
}

template <typename RADIAL_BASIS_FUNCTION_T>
void PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::computePreallocationMatrixA(
    const mesh::PtrMesh inMesh, const mesh::PtrMesh outMesh)
{
  precice::utils::Event ePreallocA("map.pet.preallocA.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscInt ownerRangeABegin, ownerRangeAEnd, colOwnerRangeABegin, colOwnerRangeAEnd;
  PetscInt outputSize, ierr;

  std::tie(outputSize, std::ignore) = _matrixA.getLocalSize();

  std::vector<PetscInt> d_nnz(outputSize), o_nnz(outputSize);

  std::tie(ownerRangeABegin, ownerRangeAEnd)       = _matrixA.ownerRange();
  std::tie(colOwnerRangeABegin, colOwnerRangeAEnd) = _matrixA.ownerRangeColumn();

  const int       dimensions = this->input()->getDimensions();
  Eigen::VectorXd distance(dimensions);

  for (int localRow = 0; localRow < ownerRangeAEnd - ownerRangeABegin; localRow++) {
    d_nnz[localRow]             = 0;
    o_nnz[localRow]             = 0;
    PetscInt            col     = 0;
    const mesh::Vertex &oVertex = outMesh->vertices()[localRow];

    // -- PREALLOCATE THE POLYNOM PART OF THE MATRIX --
    // col does not need mapping here, because the first polyparams col are always identity mapped
    if (_polynomial == Polynomial::ON) {
      if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
        d_nnz[localRow]++;
      else
        o_nnz[localRow]++;
      col++;

      for (int dim = 0; dim < dimensions; dim++) {
        if (not this->_deadAxis[dim]) {
          if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
            d_nnz[localRow]++;
          else
            o_nnz[localRow]++;
          col++;
        }
      }
    }

    // -- PREALLOCATE THE COEFFICIENTS --
    for (const mesh::Vertex &inVertex : inMesh->vertices()) {
      distance = oVertex.getCoords() - inVertex.getCoords();
      for (int d = 0; d < dimensions; d++) {
        if (this->_deadAxis[d])
          distance[d] = 0;
      }

      if (this->_basisFunction.getSupportRadius() > distance.norm()) {
        col = inVertex.getGlobalIndex() + polyparams;
        AOApplicationToPetsc(_AOmapping, 1, &col);
        if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
          d_nnz[localRow]++;
        else
          o_nnz[localRow]++;
      }
    }
  }
  if (_commState->size() == 1) {
    // std::cout << "Preallocation A Seq diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0) << '\n';
    ierr = MatSeqAIJSetPreallocation(_matrixA, 0, d_nnz.data());
    CHKERRV(ierr);
  } else {
    // std::cout << "Preallocation A MPI diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0)
    // << ", off-diagonal = " << std::accumulate(o_nnz.begin(), o_nnz.end(), 0) << '\n';
    ierr = MatMPIAIJSetPreallocation(_matrixA, 0, d_nnz.data(), 0, o_nnz.data());
    CHKERRV(ierr);
  }
  MatSetOption(_matrixA, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_TRUE);

  ePreallocA.stop();
}

template <typename RADIAL_BASIS_FUNCTION_T>
typename PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::VertexData
PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::savedPreallocationMatrixC(mesh::PtrMesh const inMesh)
{
  precice::utils::Event ePreallocC("map.pet.preallocC.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscInt n;

  const int       dimensions = this->input()->getDimensions();
  Eigen::VectorXd distance(dimensions);

  std::tie(n, std::ignore) = _matrixC.getLocalSize();
  std::vector<PetscInt> d_nnz(n), o_nnz(n);
  PetscInt              colOwnerRangeCBegin, colOwnerRangeCEnd;
  std::tie(colOwnerRangeCBegin, colOwnerRangeCEnd) = _matrixC.ownerRangeColumn();

  VertexData vertexData(n - localPolyparams);

  size_t local_row = 0;
  // -- PREALLOCATES THE POLYNOMIAL PART OF THE MATRIX --
  if (_polynomial == Polynomial::ON) {
    for (local_row = 0; local_row < localPolyparams; local_row++) {
      d_nnz[local_row] = colOwnerRangeCEnd - colOwnerRangeCBegin;
      o_nnz[local_row] = _matrixC.getSize().first - d_nnz[local_row];
    }
  }

  for (const mesh::Vertex &inVertex : inMesh->vertices()) {
    if (not inVertex.isOwner())
      continue;

    PetscInt  col        = polyparams - 1;
    const int global_row = local_row + _matrixC.ownerRange().first;
    d_nnz[local_row]     = 0;
    o_nnz[local_row]     = 0;

    // -- PREALLOCATES THE COEFFICIENTS --
    for (mesh::Vertex &vj : inMesh->vertices()) {
      col++;

      PetscInt mappedCol = vj.getGlobalIndex() + polyparams;
      AOApplicationToPetsc(_AOmapping, 1, &mappedCol); // likely not efficient in the inner loop

      if (global_row > mappedCol) // Skip, since we are below the diagonal
        continue;

      distance = inVertex.getCoords() - vj.getCoords();
      for (int d = 0; d < dimensions; d++)
        if (this->_deadAxis[d])
          distance[d] = 0;

      double const norm = distance.norm();
      if (this->_basisFunction.getSupportRadius() > norm or col == global_row) {
        vertexData[local_row - localPolyparams].emplace_back(vj.getGlobalIndex() + polyparams, norm);
        if (mappedCol >= colOwnerRangeCBegin and mappedCol < colOwnerRangeCEnd)
          d_nnz[local_row]++;
        else
          o_nnz[local_row]++;
      }
    }
    local_row++;
  }

  if (_commState->size() == 1) {
    // std::cout << "Computed Preallocation C Seq diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0) << '\n';
    MatSeqSBAIJSetPreallocation(_matrixC, _matrixC.blockSize(), 0, d_nnz.data());
  } else {
    // std::cout << "Computed Preallocation C MPI diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0)
    //           << ", off-diagonal = " << std::accumulate(o_nnz.begin(), o_nnz.end(), 0) << '\n';
    MatMPISBAIJSetPreallocation(_matrixC, _matrixC.blockSize(), 0, d_nnz.data(), 0, o_nnz.data());
  }
  MatSetOption(_matrixC, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_TRUE);

  ePreallocC.stop();

  return vertexData;
}

template <typename RADIAL_BASIS_FUNCTION_T>
typename PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::VertexData
PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::savedPreallocationMatrixA(mesh::PtrMesh const inMesh, mesh::PtrMesh const outMesh)
{
  PRECICE_INFO("Using saved preallocation");
  precice::utils::Event ePreallocA("map.pet.preallocA.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscInt ownerRangeABegin, ownerRangeAEnd, colOwnerRangeABegin, colOwnerRangeAEnd;
  PetscInt outputSize;

  std::tie(outputSize, std::ignore)                = _matrixA.getLocalSize(); // n hier nehmen
  std::tie(ownerRangeABegin, ownerRangeAEnd)       = _matrixA.ownerRange();
  std::tie(colOwnerRangeABegin, colOwnerRangeAEnd) = _matrixA.ownerRangeColumn();
  const int dimensions                             = this->input()->getDimensions();

  std::vector<PetscInt> d_nnz(outputSize), o_nnz(outputSize);
  Eigen::VectorXd       distance(dimensions);

  // Contains localRow<localCols<colPosition, distance>>>
  VertexData vertexData(outputSize);

  for (int localRow = 0; localRow < ownerRangeAEnd - ownerRangeABegin; localRow++) {
    d_nnz[localRow]             = 0;
    o_nnz[localRow]             = 0;
    PetscInt            col     = 0;
    const mesh::Vertex &oVertex = outMesh->vertices()[localRow];

    // -- PREALLOCATE THE POLYNOM PART OF THE MATRIX --
    // col does not need mapping here, because the first polyparams col are always identity mapped
    if (_polynomial == Polynomial::ON) {
      if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
        d_nnz[localRow]++;
      else
        o_nnz[localRow]++;
      col++;

      for (int dim = 0; dim < dimensions; dim++) {
        if (not this->_deadAxis[dim]) {
          if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
            d_nnz[localRow]++;
          else
            o_nnz[localRow]++;
          col++;
        }
      }
    }

    // -- PREALLOCATE THE COEFFICIENTS --
    for (const mesh::Vertex &inVertex : inMesh->vertices()) {
      distance = oVertex.getCoords() - inVertex.getCoords();
      for (int d = 0; d < dimensions; d++)
        if (this->_deadAxis[d])
          distance[d] = 0;

      double const norm = distance.norm();
      if (this->_basisFunction.getSupportRadius() > norm) {
        col = inVertex.getGlobalIndex() + polyparams;
        vertexData[localRow].emplace_back(col, norm);

        AOApplicationToPetsc(_AOmapping, 1, &col);
        if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
          d_nnz[localRow]++;
        else
          o_nnz[localRow]++;
      }
    }
  }
  if (_commState->size() == 1) {
    // std::cout << "Preallocation A Seq diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0) << '\n';
    MatSeqAIJSetPreallocation(_matrixA, 0, d_nnz.data());
  } else {
    // std::cout << "Preallocation A MPI diagonal = " << std::accumulate(d_nnz.begin(), d_nnz.end(), 0)
    //           << ", off-diagonal = " << std::accumulate(o_nnz.begin(), o_nnz.end(), 0) << '\n';
    MatMPIAIJSetPreallocation(_matrixA, 0, d_nnz.data(), 0, o_nnz.data());
  }
  MatSetOption(_matrixA, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_TRUE);

  ePreallocA.stop();
  return vertexData;
}

template <typename RADIAL_BASIS_FUNCTION_T>
typename PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::VertexData
PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::bgPreallocationMatrixC(mesh::PtrMesh const inMesh)
{
  PRECICE_INFO("Using tree-based preallocation for matrix C");
  precice::utils::Event ePreallocC("map.pet.preallocC.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscInt n;
  std::tie(n, std::ignore) = _matrixC.getLocalSize();
  std::vector<PetscInt> d_nnz(n), o_nnz(n);

  const double supportRadius = this->_basisFunction.getSupportRadius();

  const int       dimensions = this->input()->getDimensions();
  Eigen::VectorXd distance(dimensions);

  PetscInt colOwnerRangeCBegin, colOwnerRangeCEnd;
  std::tie(colOwnerRangeCBegin, colOwnerRangeCEnd) = _matrixC.ownerRangeColumn();

  VertexData vertexData(n - localPolyparams);

  size_t local_row = 0;
  // -- PREALLOCATES THE POLYNOMIAL PART OF THE MATRIX --
  if (_polynomial == Polynomial::ON) {
    for (local_row = 0; local_row < localPolyparams; local_row++) {
      d_nnz[local_row] = colOwnerRangeCEnd - colOwnerRangeCBegin;
      o_nnz[local_row] = _matrixC.getSize().first - d_nnz[local_row];
    }
  }

  for (const mesh::Vertex &inVertex : inMesh->vertices()) {
    if (not inVertex.isOwner())
      continue;

    PetscInt       col        = polyparams;
    PetscInt const global_row = local_row + _matrixC.ownerRange().first;
    d_nnz[local_row]          = 0;
    o_nnz[local_row]          = 0;

    // -- PREALLOCATES THE COEFFICIENTS --

    for (auto const i : inMesh->index().getVerticesInsideBox(inVertex, supportRadius)) {
      const mesh::Vertex &vj = inMesh->vertices()[i];

      PetscInt mappedCol = vj.getGlobalIndex() + polyparams;
      AOApplicationToPetsc(_AOmapping, 1, &mappedCol); // likely not efficient in the inner loop

      if (global_row > mappedCol) // Skip, since we are below the diagonal
        continue;

      distance = inVertex.getCoords() - vj.getCoords();
      for (int d = 0; d < dimensions; d++)
        if (this->_deadAxis[d])
          distance[d] = 0;

      double const norm = distance.norm();
      if (supportRadius > norm or col == global_row) {
        vertexData[local_row - localPolyparams].emplace_back(vj.getGlobalIndex() + polyparams, norm);
        if (mappedCol >= colOwnerRangeCBegin and mappedCol < colOwnerRangeCEnd)
          d_nnz[local_row]++;
        else
          o_nnz[local_row]++;
      }
      col++;
    }
    local_row++;
  }

  if (_commState->size() == 1) {
    MatSeqSBAIJSetPreallocation(_matrixC, _matrixC.blockSize(), 0, d_nnz.data());
  } else {
    MatMPISBAIJSetPreallocation(_matrixC, _matrixC.blockSize(), 0, d_nnz.data(), 0, o_nnz.data());
  }
  MatSetOption(_matrixC, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_TRUE);

  ePreallocC.stop();

  return vertexData;
}

template <typename RADIAL_BASIS_FUNCTION_T>
typename PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::VertexData
PetRadialBasisFctMapping<RADIAL_BASIS_FUNCTION_T>::bgPreallocationMatrixA(mesh::PtrMesh const inMesh, mesh::PtrMesh const outMesh)
{
  PRECICE_INFO("Using tree-based preallocation for matrix A");
  precice::utils::Event ePreallocA("map.pet.preallocA.From" + this->input()->getName() + "To" + this->output()->getName(), precice::syncMode);

  PetscInt       ownerRangeABegin, ownerRangeAEnd, colOwnerRangeABegin, colOwnerRangeAEnd;
  PetscInt const outputSize    = _matrixA.getLocalSize().first;
  double const   supportRadius = this->_basisFunction.getSupportRadius();

  std::tie(ownerRangeABegin, ownerRangeAEnd)       = _matrixA.ownerRange();
  std::tie(colOwnerRangeABegin, colOwnerRangeAEnd) = _matrixA.ownerRangeColumn();
  const int dimensions                             = this->input()->getDimensions();

  std::vector<PetscInt> d_nnz(outputSize), o_nnz(outputSize);
  Eigen::VectorXd       distance(dimensions);

  // Contains localRow<localCols<colPosition, distance>>>
  VertexData vertexData(outputSize);

  for (int localRow = 0; localRow < ownerRangeAEnd - ownerRangeABegin; ++localRow) {
    d_nnz[localRow]             = 0;
    o_nnz[localRow]             = 0;
    PetscInt            col     = 0;
    mesh::Vertex const &oVertex = outMesh->vertices()[localRow];

    // -- PREALLOCATE THE POLYNOM PART OF THE MATRIX --
    // col does not need mapping here, because the first polyparams col are always identity mapped
    if (_polynomial == Polynomial::ON) {
      if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
        d_nnz[localRow]++;
      else
        o_nnz[localRow]++;
      col++;

      for (int dim = 0; dim < dimensions; dim++) {
        if (not this->_deadAxis[dim]) {
          if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
            d_nnz[localRow]++;
          else
            o_nnz[localRow]++;
          col++;
        }
      }
    }

    // -- PREALLOCATE THE COEFFICIENTS --
    for (auto i : inMesh->index().getVerticesInsideBox(oVertex, supportRadius)) {
      const mesh::Vertex &inVertex = inMesh->vertices()[i];
      distance                     = oVertex.getCoords() - inVertex.getCoords();

      for (int d = 0; d < dimensions; d++)
        if (this->_deadAxis[d])
          distance[d] = 0;

      double const norm = distance.norm();
      if (supportRadius > norm) {
        col = inVertex.getGlobalIndex() + polyparams;
        vertexData[localRow].emplace_back(col, norm);

        AOApplicationToPetsc(_AOmapping, 1, &col);
        if (col >= colOwnerRangeABegin and col < colOwnerRangeAEnd)
          d_nnz[localRow]++;
        else
          o_nnz[localRow]++;
      }
    }
  }
  if (_commState->size() == 1) {
    MatSeqAIJSetPreallocation(_matrixA, 0, d_nnz.data());
  } else {
    MatMPIAIJSetPreallocation(_matrixA, 0, d_nnz.data(), 0, o_nnz.data());
  }
  MatSetOption(_matrixA, MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_TRUE);

  ePreallocA.stop();
  return vertexData;
}

} // namespace mapping
} // namespace precice

#endif // PRECICE_NO_PETSC
