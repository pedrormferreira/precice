#include "DataConfiguration.hpp"
#include <ostream>
#include "logging/LogMacros.hpp"
#include "utils/assertion.hpp"
#include "xml/ConfigParser.hpp"
#include "xml/XMLAttribute.hpp"

namespace precice {
namespace mesh {

DataConfiguration::DataConfiguration(xml::XMLTag &parent)
{
  using namespace xml;

  auto attrName = XMLAttribute<std::string>(ATTR_NAME)
                      .setDocumentation("Unique name for the data set.");

  auto attrHasGradient = makeXMLAttribute(ATTR_HAS_GRADIENT, false)
                             .setDocumentation(
                                 "If this attribute is set to \"on\", the data must have gradient values");

  XMLTag tagScalar(*this, VALUE_SCALAR, XMLTag::OCCUR_ARBITRARY, TAG);
  tagScalar.setDocumentation("Defines a scalar data set to be assigned to meshes.");
  tagScalar.addAttribute(attrName);
  tagScalar.addAttribute(attrHasGradient);
  parent.addSubtag(tagScalar);

  XMLTag tagVector(*this, VALUE_VECTOR, XMLTag::OCCUR_ARBITRARY, TAG);
  tagVector.setDocumentation("Defines a vector data set to be assigned to meshes. The number of "
                             "components of each data entry depends on the spatial dimensions set "
                             "in tag <solver-interface>.");
  tagVector.addAttribute(attrName);
  tagVector.addAttribute(attrHasGradient);
  parent.addSubtag(tagVector);
}

void DataConfiguration::setDimensions(
    int dimensions)
{
  PRECICE_TRACE(dimensions);
  PRECICE_ASSERT((dimensions == 2) || (dimensions == 3), dimensions);
  _dimensions = dimensions;
}

const std::vector<DataConfiguration::ConfiguredData> &
DataConfiguration::data() const
{
  return _data;
}

DataConfiguration::ConfiguredData DataConfiguration::getRecentlyConfiguredData() const
{
  PRECICE_ASSERT(_data.size() > 0);
  PRECICE_ASSERT(_indexLastConfigured >= 0);
  PRECICE_ASSERT(_indexLastConfigured < (int) _data.size());
  return _data[_indexLastConfigured];
}

void DataConfiguration::xmlTagCallback(
    const xml::ConfigurationContext &context,
    xml::XMLTag &                    tag)
{
  if (tag.getNamespace() == TAG) {
    PRECICE_ASSERT(_dimensions != 0);
    const std::string &name           = tag.getStringAttributeValue(ATTR_NAME);
    bool               hasGradient    = tag.getBooleanAttributeValue(ATTR_HAS_GRADIENT);
    const std::string &typeName       = tag.getName();
    int                dataDimensions = getDataDimensions(typeName);
    addData(name, dataDimensions, hasGradient);
  } else {
    PRECICE_ASSERT(false, "Received callback from an unknown tag.", tag.getName());
  }
}

void DataConfiguration::xmlEndTagCallback(
    const xml::ConfigurationContext &context,
    xml::XMLTag &                    tag)
{
}

void DataConfiguration::addData(
    const std::string &name,
    int                dataDimensions,
    bool               hasGradient)
{
  if (hasGradient) {

    ConfiguredData data(name, dataDimensions, hasGradient);
    // Check if data with same name has been added already
    for (auto &elem : _data) {
      PRECICE_CHECK(elem.name != data.name,
                    "Data \"{0}\" has already been defined. Please rename or remove one of the data tags with name=\"{0}\".",
                    data.name);
    }
    _data.push_back(data);
  } else {

    // Check if data with same name has been added already
    for (auto &elem : _data) {
      PRECICE_CHECK(elem.name != name,
                    "Data \"{0}\" has already been defined. Please rename or remove one of the data tags with name=\"{0}\".",
                    name);
    }

    _data.push_back({name, dataDimensions});
  }
}

int DataConfiguration::getDataDimensions(
    const std::string &typeName) const
{
  if (typeName == VALUE_VECTOR) {
    return _dimensions;
  } else if (typeName == VALUE_SCALAR) {
    return 1;
  }
  // We should never reach this point
  PRECICE_UNREACHABLE("Unknown data type \"{}\".", typeName);
}

} // namespace mesh
} // namespace precice
