#pragma once

#include <Eigen/Core>

#include "DataContext.hpp"
#include "logging/Logger.hpp"
#include "time/SharedPointer.hpp"
#include "time/Time.hpp"

namespace precice {
namespace impl {

/**
 * @brief Stores one Data object with related mesh. Context stores data to be read from and potentially provides a read mapping. Additionally stores Waveform object associated with _providedData.
 *
 * Derived from DataContext
 */
class ReadDataContext : public DataContext {
public:
  /**
   * @brief Construct a new ReadDataContext object without a mapping.
   *
   * @param data Data associated with this ReadDataContext.
   * @param mesh Mesh associated with this ReadDataContext.
   * @param interpolationOrder Order of the Waveform stored by this ReadDataContext.
   */
  ReadDataContext(
      mesh::PtrData data,
      mesh::PtrMesh mesh,
      int           interpolationOrder = time::Time::DEFAULT_INTERPOLATION_ORDER);

  /**
   * @brief Gets _interpolationOrder of _waveform
   *
   * @return _interpolationOrder of _waveform
   */
  int getInterpolationOrder() const;

  /**
   * @brief Adds a MappingContext and the MeshContext required by the read mapping to the correspnding ReadDataContext data structures.
   *
   * A read mapping maps _fromData to _providedData. A ReadDataContext already has _providedData, but additionally requires _fromData.
   *
   * @param[in] mappingContext Context of read mapping
   * @param[in] meshContext Context of mesh this read mapping is mapping from (_fromData)
   *
   * @note Note that read mapping contexts have to be unique and and this function may only be called once for a given ReadDataContext
   */
  void appendMappingConfiguration(const MappingContext &mappingContext, const MeshContext &meshContext) override;

  /**
   * @brief Samples data at a given point in time within the current time window
   *
   * @param normalizedDt Point in time where waveform is sampled. Must be normalized to [0,1], where 0 refers to the beginning and 1 to the end of the current time window.
   */
  Eigen::VectorXd sampleWaveformAt(double normalizedDt);

  /**
   * @brief Initializes the _waveform as a constant function with values from _providedData.
   */
  void initializeWaveform();

  /**
   * @brief Updates _waveform when moving to the next time window.
   */
  void moveToNextWindow();

  /**
   * @brief Stores _providedData as first sample of _waveform.
   */
  void storeDataInWaveform();

private:
  static logging::Logger _log;

  /// Waveform wrapped by this ReadDataContext.
  time::PtrWaveform _waveform;
};

} // namespace impl
} // namespace precice
