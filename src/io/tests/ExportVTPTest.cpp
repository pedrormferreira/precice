#ifndef PRECICE_NO_MPI

#include <Eigen/Core>
#include <algorithm>
#include <map>
#include <string>
#include "com/SharedPointer.hpp"
#include "io/Export.hpp"
#include "io/ExportVTP.hpp"
#include "mesh/Mesh.hpp"
#include "testing/TestContext.hpp"
#include "testing/Testing.hpp"
#include "utils/Parallel.hpp"

namespace precice {
namespace mesh {
class Edge;
class Vertex;
} // namespace mesh
} // namespace precice

BOOST_AUTO_TEST_SUITE(IOTests)

using namespace precice;

BOOST_AUTO_TEST_SUITE(VTPExport)

BOOST_AUTO_TEST_CASE(ExportPolygonalMeshSerial)
{
  PRECICE_TEST(""_on(1_rank).setupIntraComm());
  int           dim = 2;
  mesh::Mesh    mesh("MyMesh", dim, testing::nextMeshID());
  mesh::Vertex &v1 = mesh.createVertex(Eigen::Vector2d::Zero());
  mesh::Vertex &v2 = mesh.createVertex(Eigen::Vector2d::Constant(1));
  mesh::Vertex &v3 = mesh.createVertex(Eigen::Vector2d{1.0, 0.0});

  mesh.createEdge(v1, v2);
  mesh.createEdge(v2, v3);
  mesh.createEdge(v3, v1);

  io::ExportVTP exportVTP;
  std::string   filename = "io-VTPExport-ExportPolygonalMesh";
  std::string   location = "";
  exportVTP.doExport(filename, location, mesh);
}

BOOST_AUTO_TEST_CASE(ExportPolygonalMesh)
{
  PRECICE_TEST(""_on(4_ranks).setupIntraComm());
  int        dim = 2;
  mesh::Mesh mesh("MyMesh", dim, testing::nextMeshID());

  if (context.isRank(0)) {
    mesh::Vertex &v1 = mesh.createVertex(Eigen::Vector2d::Zero());
    mesh::Vertex &v2 = mesh.createVertex(Eigen::Vector2d::Constant(1));
    mesh::Vertex &v3 = mesh.createVertex(Eigen::Vector2d{1.0, 0});

    mesh.createEdge(v1, v2);
    mesh.createEdge(v2, v3);
    mesh.createEdge(v3, v1);
    mesh.getVertexOffsets() = {3, 3, 6, 7};
  } else if (context.isRank(1)) {
    // nothing
  } else if (context.isRank(2)) {
    mesh::Vertex &v1 = mesh.createVertex(Eigen::Vector2d::Constant(1));
    mesh::Vertex &v2 = mesh.createVertex(Eigen::Vector2d::Constant(2));
    mesh::Vertex &v3 = mesh.createVertex(Eigen::Vector2d{1.0, 0.0});

    mesh.createEdge(v1, v2);
    mesh.createEdge(v2, v3);
    mesh.createEdge(v3, v1);
  } else if (context.isRank(3)) {
    mesh.createVertex(Eigen::Vector2d::Constant(3.0));
  }

  io::ExportVTP exportVTP;
  std::string   filename = "io-ExportVTPTest-testExportPolygonalMesh";
  std::string   location = "";
  exportVTP.doExport(filename, location, mesh);
}

BOOST_AUTO_TEST_CASE(ExportTriangulatedMesh)
{
  PRECICE_TEST(""_on(4_ranks).setupIntraComm());
  int        dim = 3;
  mesh::Mesh mesh("MyMesh", dim, testing::nextMeshID());

  if (context.isRank(0)) {
    mesh::Vertex &v1 = mesh.createVertex(Eigen::Vector3d::Zero());
    mesh::Vertex &v2 = mesh.createVertex(Eigen::Vector3d::Constant(1));
    mesh::Vertex &v3 = mesh.createVertex(Eigen::Vector3d{1.0, 0.0, 0.0});

    mesh::Edge &e1 = mesh.createEdge(v1, v2);
    mesh::Edge &e2 = mesh.createEdge(v2, v3);
    mesh::Edge &e3 = mesh.createEdge(v3, v1);
    mesh.createTriangle(e1, e2, e3);
    mesh.getVertexOffsets() = {3, 3, 6, 7};

  } else if (context.isRank(1)) {
    // nothing
  } else if (context.isRank(2)) {
    mesh::Vertex &v1 = mesh.createVertex(Eigen::Vector3d::Constant(1));
    mesh::Vertex &v2 = mesh.createVertex(Eigen::Vector3d::Constant(2));
    mesh::Vertex &v3 = mesh.createVertex(Eigen::Vector3d{0.0, 1.0, 0.0});

    mesh::Edge &e1 = mesh.createEdge(v1, v2);
    mesh::Edge &e2 = mesh.createEdge(v2, v3);
    mesh::Edge &e3 = mesh.createEdge(v3, v1);
    mesh.createTriangle(e1, e2, e3);
  } else if (context.isRank(3)) {
    mesh.createVertex(Eigen::Vector3d::Constant(3.0));
  }

  io::ExportVTP exportVTP;
  std::string   filename = "io-ExportVTPTest-testExportTriangulatedMesh";
  std::string   location = "";
  exportVTP.doExport(filename, location, mesh);
}

BOOST_AUTO_TEST_CASE(ExportSplitSquare)
{
  PRECICE_TEST(""_on(4_ranks).setupIntraComm());
  int        dim = 3;
  mesh::Mesh mesh("MyMesh", dim, testing::nextMeshID());

  mesh::Vertex &vm = mesh.createVertex(Eigen::Vector3d::Zero());
  if (context.isRank(0)) {
    mesh::Vertex &v1  = mesh.createVertex(Eigen::Vector3d{-1.0, 1.0, 0.0});
    mesh::Vertex &v2  = mesh.createVertex(Eigen::Vector3d{1.0, 1.0, 0.0});
    mesh::Vertex &vo  = mesh.createVertex(Eigen::Vector3d{0.0, 2.0, 0.0});
    mesh::Edge &  em1 = mesh.createEdge(vm, v1);
    mesh::Edge &  e12 = mesh.createEdge(v1, v2);
    mesh::Edge &  e2m = mesh.createEdge(v2, vm);
    mesh.createTriangle(em1, e12, e2m);
    mesh::Edge &eo1 = mesh.createEdge(vo, v1);
    mesh::Edge &e2o = mesh.createEdge(v2, vo);
    mesh.createTriangle(eo1, e12, e2o);

    mesh.getVertexOffsets() = {3, 6, 9, 12};
  } else if (context.isRank(1)) {
    mesh::Vertex &v1  = mesh.createVertex(Eigen::Vector3d{1.0, -1.0, 0.0});
    mesh::Vertex &v2  = mesh.createVertex(Eigen::Vector3d{-1.0, -1.0, 0.0});
    mesh::Vertex &vo  = mesh.createVertex(Eigen::Vector3d{0.0, -2.0, 0.0});
    mesh::Edge &  em1 = mesh.createEdge(vm, v1);
    mesh::Edge &  e12 = mesh.createEdge(v1, v2);
    mesh::Edge &  e2m = mesh.createEdge(v2, vm);
    mesh.createTriangle(em1, e12, e2m);
    mesh::Edge &eo1 = mesh.createEdge(vo, v1);
    mesh::Edge &e2o = mesh.createEdge(v2, vo);
    mesh.createTriangle(eo1, e12, e2o);
  } else if (context.isRank(2)) {
    mesh::Vertex &v1  = mesh.createVertex(Eigen::Vector3d{-1.0, 1.0, 0.0});
    mesh::Vertex &v2  = mesh.createVertex(Eigen::Vector3d{-1.0, -1.0, 0.0});
    mesh::Vertex &vo  = mesh.createVertex(Eigen::Vector3d{-2.0, 0.0, 0.0});
    mesh::Edge &  em1 = mesh.createEdge(vm, v1);
    mesh::Edge &  e12 = mesh.createEdge(v1, v2);
    mesh::Edge &  e2m = mesh.createEdge(v2, vm);
    mesh.createTriangle(em1, e12, e2m);
    mesh::Edge &eo1 = mesh.createEdge(vo, v1);
    mesh::Edge &e2o = mesh.createEdge(v2, vo);
    mesh.createTriangle(eo1, e12, e2o);
  } else if (context.isRank(3)) {
    mesh::Vertex &v1  = mesh.createVertex(Eigen::Vector3d{1.0, 1.0, 0.0});
    mesh::Vertex &v2  = mesh.createVertex(Eigen::Vector3d{1.0, -1.0, 0.0});
    mesh::Vertex &vo  = mesh.createVertex(Eigen::Vector3d{2.0, 0.0, 0.0});
    mesh::Edge &  em1 = mesh.createEdge(vm, v1);
    mesh::Edge &  e12 = mesh.createEdge(v1, v2);
    mesh::Edge &  e2m = mesh.createEdge(v2, vm);
    mesh.createTriangle(em1, e12, e2m);
    mesh::Edge &eo1 = mesh.createEdge(vo, v1);
    mesh::Edge &e2o = mesh.createEdge(v2, vo);
    mesh.createTriangle(eo1, e12, e2o);
  }

  io::ExportVTP exportVTP;
  std::string   filename = "io-ExportVTPTest-Square";
  std::string   location = "";
  exportVTP.doExport(filename, location, mesh);
}

BOOST_AUTO_TEST_SUITE_END() // IOTests
BOOST_AUTO_TEST_SUITE_END() // VTPExport

#endif // PRECICE_NO_MPI
