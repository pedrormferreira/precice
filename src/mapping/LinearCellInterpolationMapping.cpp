#include "LinearCellInterpolationMapping.hpp"
#include "logging/LogMacros.hpp"
#include "query/Index.hpp"
#include "utils/Event.hpp"
#include "utils/Statistics.hpp"
#include "utils/assertion.hpp"

namespace precice {
extern bool syncMode;

namespace mapping {

LinearCellInterpolationMapping::LinearCellInterpolationMapping(
    Constraint constraint,
    int        dimensions)
    : BarycentricBaseMapping(constraint, dimensions)
{
  PRECICE_CHECK(getDimensions() == 2, "Volume mapping not available in 3D.");
  if (constraint == CONSISTENT) {
    setInputRequirement(Mapping::MeshRequirement::FULL);
    setOutputRequirement(Mapping::MeshRequirement::VERTEX);
  } else if (constraint == CONSERVATIVE) {
    setInputRequirement(Mapping::MeshRequirement::VERTEX);
    setOutputRequirement(Mapping::MeshRequirement::FULL);
  } else {
    PRECICE_ASSERT(constraint == SCALEDCONSISTENT, constraint);
    setInputRequirement(Mapping::MeshRequirement::FULL);
    setOutputRequirement(Mapping::MeshRequirement::FULL);
  }

  PRECICE_CHECK(constraint != SCALEDCONSISTENT, "Volume mapping doesn't support scaled-consistent mappings.");
}

void LinearCellInterpolationMapping::computeMapping()
{
  PRECICE_TRACE(input()->vertices().size(), output()->vertices().size());
  const std::string     baseEvent = "map.vci.computeMapping.From" + input()->getName() + "To" + output()->getName();
  precice::utils::Event e(baseEvent, precice::syncMode);
  PRECICE_ASSERT(getDimensions() == 2, "Volume mapping not available in 3D.");

  // Setup Direction of Mapping
  mesh::PtrMesh origins, searchSpace;
  if (hasConstraint(CONSERVATIVE)) {
    PRECICE_DEBUG("Compute conservative mapping");
    origins     = input();
    searchSpace = output();
  } else {
    PRECICE_DEBUG("Compute consistent mapping");
    origins     = output();
    searchSpace = input();
  }

  const auto &fVertices           = origins->vertices();
  bool        missingConnectivity = false;

  if (getDimensions() == 2) {
    if (!fVertices.empty() && searchSpace->triangles().empty()) {
      PRECICE_WARN("2D Mesh \"{}\" does not contain triangles. "
                   "Linear cell interpolation falls back to nearest projection mapping.",
                   searchSpace->getName());
      missingConnectivity = true;
    }
  } else {
    // TODO, shouldn't be reached
    PRECICE_ERROR("Linear Cell interpolation in 3D not implemented yet. This line should be unreachable");
  }

  // Amount of nearest elements to fetch for detailed comparison.
  // This safety margin results in a candidate set which forms the base for the
  // local nearest projection and counters the loss of detail due to bounding box generation.
  // @TODO Add a configuration option for this factor
  constexpr int nnearest = 4;

  auto &                                 index = searchSpace->index();
  utils::statistics::DistanceAccumulator fallbackStatistics;

  _interpolations.clear();
  _interpolations.reserve(fVertices.size());

  for (const auto &fVertex : fVertices) {
    // Find tetrahedra (3D) or triangle (2D) or fall-back on NP
    auto match = index.findCellOrProjection(fVertex.getCoords(), nnearest);
    _interpolations.push_back(std::move(match.polation));
    if (!math::equals(match.distance, 0.0)) {
      // Only push when fall-back occurs, so the number of entries is the number of vertices outside the domain
      fallbackStatistics(match.distance);
    }
  }

  if (!fallbackStatistics.empty() && !missingConnectivity) {
    PRECICE_INFO(
        "Linear Cell Interpolation is used, but some points from {} don't lie in the domain defined by the {}. "
        "These points have been projected on the domain boundary. This could come from non-matching discrete geometries or erroneous connectivity information."
        "If distances seem too large, please check your mesh. "
        "The projection statistics are: {} ",
        searchSpace->getName(), getDimensions() == 2 ? "triangles" : "tetrahedra",
        fallbackStatistics);
  } else {
    PRECICE_INFO("All vertices are inside cells, no fallback to Nearest-Projection required");
  }

  _hasComputedMapping = true;
}

} // namespace mapping
} // namespace precice
