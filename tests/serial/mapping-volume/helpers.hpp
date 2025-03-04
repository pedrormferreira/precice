#ifndef PRECICE_NO_MPI

#pragma once

#include "testing/TestContext.hpp"

using namespace precice;
using precice::testing::TestContext;

void testMappingVolumeOneTriangle(const std::string configFile, const TestContext &context);
void testMappingVolumeOneTriangleConservative(const std::string configFile, const TestContext &context);

#endif