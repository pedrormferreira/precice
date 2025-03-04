#pragma once

#include <vector>
#include "action/Action.hpp"
#include "mesh/SharedPointer.hpp"

namespace precice {
namespace action {

/// Action that records invocations for testing purposes
class RecorderAction : public Action {
public:
  /**
	 * @brief Constructor
	 * 
	 * @param[in] timing When to apply the action.
	 * @param[in] mesh the target mesh.
	 * 
	 */
  RecorderAction(
      Timing               timing,
      const mesh::PtrMesh &mesh);

  /// Records the invocation and appends it to the records
  void performAction(
      double time,
      double timeStepSize,
      double computedTimeWindowPart,
      double timeWindowSize) override;

  struct Record {
    Timing timing;
    double time;
    double timeStepSize;
    double computedTimeWindowPart;
    double timeWindowSize;
  };

  /// resets the saved records.
  static void reset();

  static std::vector<Record> records;
};

} // namespace action
} // namespace precice
