#ifndef PRECICE_NO_MPI

#include "com/MPIPortsCommunication.hpp"
#include "testing/Testing.hpp"
#include "utils/Parallel.hpp"

using Par = precice::utils::Parallel;
using namespace precice;
using namespace precice::com;

BOOST_AUTO_TEST_SUITE(CommunicationTests)

BOOST_AUTO_TEST_SUITE(MPICommunication,
                      * testing::MinRanks(2)
                      * boost::unit_test::fixture<testing::MPICommRestrictFixture>(std::vector<int>({0, 1}))
                      * boost::unit_test::label("MPI_Ports"))

BOOST_AUTO_TEST_CASE(SendAndReceiveString)
{
  if (Par::getCommunicatorSize() != 2)
    return;

  utils::Parallel::synchronizeProcesses();
  MPIPortsCommunication com;
  if (utils::Parallel::getProcessRank() == 0) {
    com.acceptConnection("A", "R");
    std::string msg("testOne");
    com.send(msg, 0);
    com.receive(msg, 0);
    BOOST_TEST(msg == std::string("testTwo"));

  } else if (utils::Parallel::getProcessRank() == 1) {
    com.requestConnection("A", "R", 0, 1);
    std::string msg;
    com.receive(msg, 0);
    BOOST_TEST(msg == std::string("testOne"));
    msg = "testTwo";
    com.send(msg, 0);
  }
  utils::Parallel::clearGroups();
}

BOOST_AUTO_TEST_CASE(SendAndReceiveStdIntVector)
{
  if (Par::getCommunicatorSize() != 2)
    return;

  utils::Parallel::synchronizeProcesses();
  MPIPortsCommunication com;
  std::vector<int> msg{1, 2, 3};
  std::vector<int> recv;
  if (utils::Parallel::getProcessRank() == 0) {
    com.acceptConnection("A", "R");
    com.send(msg, 0);
    com.receive(recv, 0);
    BOOST_TEST(recv == msg);
  } else if (utils::Parallel::getProcessRank() == 1) {
    com.requestConnection("A", "R", 0, 1);
    com.receive(recv, 0);
    BOOST_CHECK(recv == msg);
    com.send(msg, 0);
  }
  utils::Parallel::clearGroups();
}

BOOST_AUTO_TEST_CASE(SendAndReceiveStdDoubleVector)
{
  if (Par::getCommunicatorSize() != 2)
    return;

  utils::Parallel::synchronizeProcesses();
  MPIPortsCommunication com;
  std::vector<double> msg{1.1, 2.2, 3.3};
  std::vector<double> recv;
  if (utils::Parallel::getProcessRank() == 0) {
    com.acceptConnection("A", "R");
    com.send(msg, 0);
    com.receive(recv, 0);
    BOOST_TEST(recv == msg);
  } else if (utils::Parallel::getProcessRank() == 1) {
    com.requestConnection("A", "R", 0, 1);
    com.receive(recv, 0);
    BOOST_CHECK(recv == msg);
    com.send(msg, 0);
  }
  utils::Parallel::clearGroups();
}


BOOST_AUTO_TEST_CASE(SendAndReceiveEigenVector)
{
  if (Par::getCommunicatorSize() != 2)
    return;

  utils::Parallel::synchronizeProcesses();
  MPIPortsCommunication com;
  if (utils::Parallel::getProcessRank() == 0) {
    com.acceptConnection("A", "R");
    Eigen::Vector3d msg(1, 1, 1);
    com.send(msg.data(), msg.size(), 0);
    com.receive(msg.data(), msg.size(), 0);
    BOOST_TEST(testing::equals(msg, Eigen::Vector3d::Constant(2)));
  } else if (utils::Parallel::getProcessRank() == 1) {
    com.requestConnection("A", "R", 0, 1);
    Eigen::Vector3d msg(0, 0, 0);
    com.receive(msg.data(), msg.size(), 0);
    BOOST_CHECK(testing::equals(msg, Eigen::Vector3d::Constant(1)));
    msg = Eigen::Vector3d::Constant(2);
    com.send(msg.data(), msg.size(), 0);
  }
  utils::Parallel::clearGroups();
}

BOOST_AUTO_TEST_CASE(SendAndReceiveInteger)
{
  if (Par::getCommunicatorSize() != 2)
    return;

  utils::Parallel::synchronizeProcesses();
  MPIPortsCommunication com;
  if (utils::Parallel::getProcessRank() == 0) {
    com.acceptConnection("A", "R");
    int msg = 1;
    com.send(msg, 0);
    com.receive(msg, 0);
    BOOST_TEST(msg == 2);
  } else if (utils::Parallel::getProcessRank() == 1) {
    com.requestConnection("A", "R", 0, 1);
    int msg = 0;
    com.receive(msg, 0);
    BOOST_TEST(msg == 1);
    msg = 2;
    com.send(msg, 0);
  }
  utils::Parallel::clearGroups();
}

BOOST_AUTO_TEST_SUITE_END() // MPICommunication

BOOST_AUTO_TEST_SUITE_END() // Communication

#endif // not PRECICE_NO_MPI
