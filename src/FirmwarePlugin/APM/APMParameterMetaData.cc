/*=====================================================================
 
 QGroundControl Open Source Ground Control Station
 
 (c) 2009 - 2014 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 
 This file is part of the QGROUNDCONTROL project
 
 QGROUNDCONTROL is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 QGROUNDCONTROL is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with QGROUNDCONTROL. If not, see <http://www.gnu.org/licenses/>.
 
 ======================================================================*/

#include "APMParameterMetaData.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QStack>

QGC_LOGGING_CATEGORY(APMParameterMetaDataLog,           "APMParameterMetaDataLog")
QGC_LOGGING_CATEGORY(APMParameterMetaDataVerboseLog,    "APMParameterMetaDataVerboseLog")

APMParameterMetaData::APMParameterMetaData(void)
    : _parameterMetaDataLoaded(false)
{
    // APM meta data is not yet versioned
    _loadParameterFactMetaData();
}

/// Converts a string to a typed QVariant
///     @param string String to convert
///     @param type Type for Fact which dictates the QVariant type as well
///     @param convertOk Returned: true: conversion success, false: conversion failure
/// @return Returns the correctly type QVariant
QVariant APMParameterMetaData::_stringToTypedVariant(const QString& string,
                                                     FactMetaData::ValueType_t type, bool* convertOk)
{
    QVariant var(string);

    int convertTo = QVariant::Int; // keep compiler warning happy
    switch (type) {
        case FactMetaData::valueTypeUint8:
        case FactMetaData::valueTypeUint16:
        case FactMetaData::valueTypeUint32:
            convertTo = QVariant::UInt;
            break;
        case FactMetaData::valueTypeInt8:
        case FactMetaData::valueTypeInt16:
        case FactMetaData::valueTypeInt32:
            convertTo = QVariant::Int;
            break;
        case FactMetaData::valueTypeFloat:
            convertTo = QMetaType::Float;
            break;
        case FactMetaData::valueTypeDouble:
            convertTo = QVariant::Double;
            break;
    }
    
    *convertOk = var.convert(convertTo);
    
    return var;
}

QString APMParameterMetaData::mavTypeToString(MAV_TYPE vehicleTypeEnum)
{
    QString vehicleName;

    switch(vehicleTypeEnum) {
        case MAV_TYPE_FIXED_WING:
            vehicleName = "ArduPlane";
            break;
        case MAV_TYPE_QUADROTOR:
        case MAV_TYPE_COAXIAL:
        case MAV_TYPE_HELICOPTER:
        case MAV_TYPE_SUBMARINE:
        case MAV_TYPE_HEXAROTOR:
        case MAV_TYPE_OCTOROTOR:
        case MAV_TYPE_TRICOPTER:
            vehicleName = "ArduCopter";
            break;
        case MAV_TYPE_ANTENNA_TRACKER:
            vehicleName = "Antenna Tracker";
        case MAV_TYPE_GENERIC:
        case MAV_TYPE_GCS:
        case MAV_TYPE_AIRSHIP:
        case MAV_TYPE_FREE_BALLOON:
        case MAV_TYPE_ROCKET:
            break;
        case MAV_TYPE_GROUND_ROVER:
        case MAV_TYPE_SURFACE_BOAT:
            vehicleName = "ArduRover";
            break;
        case MAV_TYPE_FLAPPING_WING:
        case MAV_TYPE_KITE:
        case MAV_TYPE_ONBOARD_CONTROLLER:
        case MAV_TYPE_VTOL_DUOROTOR:
        case MAV_TYPE_VTOL_QUADROTOR:
        case MAV_TYPE_VTOL_TILTROTOR:
        case MAV_TYPE_VTOL_RESERVED2:
        case MAV_TYPE_VTOL_RESERVED3:
        case MAV_TYPE_VTOL_RESERVED4:
        case MAV_TYPE_VTOL_RESERVED5:
        case MAV_TYPE_GIMBAL:
        case MAV_TYPE_ENUM_END:
        default:
            break;
    }
    return vehicleName;
}

/// Load Parameter Fact meta data
///
/// The meta data comes from firmware parameters.xml file.
void APMParameterMetaData::_loadParameterFactMetaData()
{
    if (_parameterMetaDataLoaded) {
        return;
    }
    _parameterMetaDataLoaded = true;

    QRegExp parameterCategories = QRegExp("ArduCopter|ArduPlane|APMrover2|AntennaTracker");
    QString currentCategory;

    QString parameterFilename;

    // Fixme:: always picking up the bundled xml, we would like to update it from web
    // just not sure right now as the xml is in bad shape.
    if (parameterFilename.isEmpty() || !QFile(parameterFilename).exists()) {
        parameterFilename = ":/FirmwarePlugin/APM/APMParameterFactMetaData.xml";
    }

    qCDebug(APMParameterMetaDataLog) << "Loading parameter meta data:" << parameterFilename;

    QFile xmlFile(parameterFilename);
    Q_ASSERT(xmlFile.exists());

    bool success = xmlFile.open(QIODevice::ReadOnly);
    Q_UNUSED(success);
    Q_ASSERT(success);

    QXmlStreamReader xml(xmlFile.readAll());
    xmlFile.close();
    if (xml.hasError()) {
        qCWarning(APMParameterMetaDataLog) << "Badly formed XML, reading failed: " << xml.errorString();
        return;
    }

    QString             errorString;
    bool                badMetaData = true;
    QStack<int>         xmlState;
    APMFactMetaDataRaw* rawMetaData = NULL;

    xmlState.push(XmlStateNone);

    QMap<QString,QStringList> groupMembers; //used to remove groups with single item

    while (!xml.atEnd()) {
        if (xml.isStartElement()) {
            QString elementName = xml.name().toString();

            if (elementName.isEmpty()) {
                // skip empty elements
            } else if (elementName == "paramfile") {
                if (xmlState.top() != XmlStateNone) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, paramfile matched";
                }
                xmlState.push(XmlstateParamFileFound);
                // we don't really do anything with this element
            } else if (elementName == "vehicles") {
                if (xmlState.top() != XmlstateParamFileFound) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, vehicles matched";
                    return;
                }
                xmlState.push(XmlStateFoundVehicles);
            } else if (elementName == "libraries") {
                if (xmlState.top() != XmlstateParamFileFound) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, libraries matched";
                    return;
                }
                currentCategory = "libraries";
                xmlState.push(XmlStateFoundLibraries);
            } else if (elementName == "parameters") {
                if (xmlState.top() != XmlStateFoundVehicles && xmlState.top() != XmlStateFoundLibraries) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, parameters matched"
                                                       << "but we don't have proper vehicle or libraries yet";
                    return;
                }

                if (xml.attributes().hasAttribute("name")) {
                    // we will handle metadata only for specific MAV_TYPEs and libraries
                    const QString nameValue = xml.attributes().value("name").toString();
                    if (nameValue.contains(parameterCategories)) {
                        xmlState.push(XmlStateFoundParameters);
                        currentCategory = nameValue;
                    } else if(xmlState.top() == XmlStateFoundLibraries) {
                        // we handle all libraries section under the same category libraries
                        // so not setting currentCategory
                        xmlState.push(XmlStateFoundParameters);
                    } else {
                        qCDebug(APMParameterMetaDataVerboseLog) << "not interested in this block of parameters, skipping:" << nameValue;
                        if (skipXMLBlock(xml, "parameters")) {
                            qCWarning(APMParameterMetaDataLog) << "something wrong with the xml, skip of the xml failed";
                            return;
                        }
                        xml.readNext();
                        continue;
                    }
                }
            }  else if (elementName == "param") {
                if (xmlState.top() != XmlStateFoundParameters) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, element param matched"
                                                       << "while we are not yet in parameters";
                    return;
                }
                xmlState.push(XmlStateFoundParameter);

                if (!xml.attributes().hasAttribute("name")) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, parameter attribute name missing";
                    return;
                }

                QString name = xml.attributes().value("name").toString();
                if (name.contains(':')) {
                    name = name.split(':').last();
                }
                QString group = name.split('_').first();
                group = group.remove(QRegExp("[0-9]*$")); // remove any numbers from the end

                QString shortDescription = xml.attributes().value("humanName").toString();
                QString longDescription = xml.attributes().value("docmentation").toString();
                QString userLevel = xml.attributes().value("user").toString();

                qCDebug(APMParameterMetaDataVerboseLog) << "Found parameter name:" << name
                          << "short Desc:" << shortDescription
                          << "longDescription:" << longDescription
                          << "user level: " << userLevel
                          << "group: " << group;

                Q_ASSERT(!rawMetaData);
                rawMetaData = new APMFactMetaDataRaw();
                if (_vehicleTypeToParametersMap[currentCategory].contains(name)) {
                    // We can't trust the meta dafa since we have dups
                    qCWarning(APMParameterMetaDataLog) << "Duplicate parameter found:" << name;
                    badMetaData = true;
                } else {
                    qCDebug(APMParameterMetaDataVerboseLog) << "inserting metadata for field" << name;
                    _vehicleTypeToParametersMap[currentCategory][name] = rawMetaData;
                    rawMetaData->name = name;
                    rawMetaData->group = group;
                    rawMetaData->shortDescription = shortDescription;
                    rawMetaData->longDescription = longDescription;

                    groupMembers[group] << name;
                }

            } else {
                // We should be getting meta data now
                if (xmlState.top() != XmlStateFoundParameter) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, while reading parameter fields wrong state";
                    return;
                }
                if (!badMetaData) {
                    if (!parseParameterAttributes(xml, rawMetaData)) {
                        qCDebug(APMParameterMetaDataLog) << "Badly formed XML, failed to read parameter attributes";
                        return;
                    }
                    continue;
                }
            }
        } else if (xml.isEndElement()) {
            QString elementName = xml.name().toString();

            if (elementName == "param" && xmlState.top() == XmlStateFoundParameter) {
                // Done loading this parameter
                // Reset for next parameter
                qCDebug(APMParameterMetaDataVerboseLog) << "done loading parameter";
                rawMetaData = NULL;
                badMetaData = false;
                xmlState.pop();
            } else if (elementName == "parameters") {
                qCDebug(APMParameterMetaDataVerboseLog) << "end of parameters for category: " << currentCategory;
                correctGroupMemberships(_vehicleTypeToParametersMap[currentCategory], groupMembers);
                groupMembers.clear();
                xmlState.pop();
            } else if (elementName == "vehicles") {
                qCDebug(APMParameterMetaDataVerboseLog) << "vehicles end here, libraries will follow";
                xmlState.pop();
            }
        }
        xml.readNext();
    }
}

void APMParameterMetaData::correctGroupMemberships(ParameterNametoFactMetaDataMap& parameterToFactMetaDataMap,
                                                   QMap<QString,QStringList>& groupMembers)
{
    foreach(const QString& groupName, groupMembers.keys()) {
            if (groupMembers[groupName].count() == 1) {
                foreach(const QString& parameter, groupMembers.value(groupName)) {
                    parameterToFactMetaDataMap[parameter]->group = "others";
                }
            }
        }
}

bool APMParameterMetaData::skipXMLBlock(QXmlStreamReader& xml, const QString& blockName)
{
    QString elementName;
    do {
        xml.readNext();
        elementName = xml.name().toString();
    } while ((elementName != blockName) && (xml.isEndElement()));
    return !xml.isEndDocument();
}

bool APMParameterMetaData::parseParameterAttributes(QXmlStreamReader& xml, APMFactMetaDataRaw* rawMetaData)
{
    QString elementName = xml.name().toString();
    QList<QPair<QString,QString> > values;
    // as long as param doens't end
    while (!(elementName == "param" && xml.isEndElement())) {
        if (elementName.isEmpty()) {
            // skip empty elements. Somehow I am getting lot of these. Dont know what to do with them.
        } else if (elementName == "field") {
            QString attributeName = xml.attributes().value("name").toString();

            if ( attributeName == "Range") {
                QString range = xml.readElementText().trimmed();
                QStringList rangeList = range.split(' ');
                if (rangeList.count() != 2) {
                    qCDebug(APMParameterMetaDataVerboseLog) << "space seperator didn't work',trying 'to' separator";
                    rangeList = range.split("to");
                    if (rangeList.count() != 2) {
                        qCDebug(APMParameterMetaDataVerboseLog) << " 'to' seperaator didn't work', trying '-' as seperator";
                        rangeList = range.split('-');
                        if (rangeList.count() != 2) {
                            qCDebug(APMParameterMetaDataLog) << "something wrong with range, all three separators have failed" << range;
                        }
                    }
                }

                // everything should be good. lets collect min and max
                if (rangeList.count() == 2) {
                    rawMetaData->min = rangeList.first().trimmed();
                    rawMetaData->max = rangeList.last().trimmed();

                    // sanitize min and max off any comments that they may have
                    if (rawMetaData->min.contains(' ')) {
                        rawMetaData->min = rawMetaData->min.split(' ').first();
                    }
                    if(rawMetaData->max.contains(' ')) {
                        rawMetaData->max = rawMetaData->max.split(' ').first();
                    }
                    qCDebug(APMParameterMetaDataVerboseLog) << "read field parameter " << "min: " << rawMetaData->min
                                                     << "max: " << rawMetaData->max;
                }
            } else if (attributeName == "Increment") {
                QString increment = xml.readElementText();
                qCDebug(APMParameterMetaDataVerboseLog) << "read Increment: " << increment;
                rawMetaData->incrementSize = increment;
            } else if (attributeName == "Units") {
                QString units = xml.readElementText();
                qCDebug(APMParameterMetaDataVerboseLog) << "read Units: " << units;
                rawMetaData->units = units;
            } else if (attributeName == "Bitmask") {
                bool    parseError = false;

                QString bitmaskString = xml.readElementText();
                qCDebug(APMParameterMetaDataVerboseLog) << "read Bitmask: " << bitmaskString;
                QStringList bitmaskList = bitmaskString.split(",");
                if (bitmaskList.count() > 0) {
                    foreach (const QString& bitmask, bitmaskList) {
                        QStringList pair = bitmask.split(":");
                        if (pair.count() == 2) {
                            rawMetaData->bitmask << QPair<QString, QString>(pair[0], pair[1]);
                        } else {
                            qCDebug(APMParameterMetaDataLog) << "parse error: bitmask:" << bitmaskString << "pair count:" << pair.count();
                            parseError = true;
                            break;
                        }
                    }
                }

                if (parseError) {
                    rawMetaData->bitmask.clear();
                }
            } else if (attributeName == "RebootRequired") {
                QString strValue = xml.readElementText().trimmed();
                if (strValue.compare("true", Qt::CaseInsensitive) == 0) {
                    rawMetaData->rebootRequired = true;
                }
            }
        } else if (elementName == "values") {
            // doing nothing individual value will follow anyway. May be used for sanity checking.
        } else if (elementName == "value") {
            QString valueValue = xml.attributes().value("code").toString();
            QString valueName = xml.readElementText();
            qCDebug(APMParameterMetaDataVerboseLog) << "read value parameter " << "value desc: "
                                             << valueName << "code: " << valueValue;
            values << QPair<QString,QString>(valueValue, valueName);
            rawMetaData->values = values;
        } else {
            qCWarning(APMParameterMetaDataLog) << "Unknown parameter element in XML: " << elementName;
        }
        xml.readNext();
        elementName = xml.name().toString();
    }
    return true;
}

void APMParameterMetaData::addMetaDataToFact(Fact* fact, MAV_TYPE vehicleType)
{
    const QString mavTypeString = mavTypeToString(vehicleType);
    APMFactMetaDataRaw* rawMetaData = NULL;

    // check if we have metadata for fact, use generic otherwise
    if (_vehicleTypeToParametersMap[mavTypeString].contains(fact->name())) {
        rawMetaData = _vehicleTypeToParametersMap[mavTypeString][fact->name()];
    } else if (_vehicleTypeToParametersMap["libraries"].contains(fact->name())) {
        rawMetaData = _vehicleTypeToParametersMap["libraries"][fact->name()];
    }

    FactMetaData *metaData = new FactMetaData(fact->type(), fact);

    // we don't have data for this fact
    if (!rawMetaData) {
        fact->setMetaData(metaData);
        qCDebug(APMParameterMetaDataLog) << "No metaData for " << fact->name() << "using generic metadata";
        return;
    }

    metaData->setName(rawMetaData->name);
    metaData->setGroup(rawMetaData->group);
    metaData->setRebootRequired(rawMetaData->rebootRequired);

    if (!rawMetaData->shortDescription.isEmpty()) {
        metaData->setShortDescription(rawMetaData->shortDescription);
    }

    if (!rawMetaData->longDescription.isEmpty()) {
        metaData->setLongDescription(rawMetaData->longDescription);
    }

    if (!rawMetaData->units.isEmpty()) {
        metaData->setRawUnits(rawMetaData->units);
    }

    if (!rawMetaData->min.isEmpty()) {
        QVariant varMin;
        QString errorString;
        if (metaData->convertAndValidateRaw(rawMetaData->min, false /* validate as well */, varMin, errorString)) {
            metaData->setRawMin(varMin);
        } else {
            qCDebug(APMParameterMetaDataLog) << "Invalid min value, name:" << metaData->name()
                                             << " type:" << metaData->type() << " min:" << rawMetaData->min
                                             << " error:" << errorString;
        }
    }

    if (!rawMetaData->max.isEmpty()) {
        QVariant varMax;
        QString errorString;
        if (metaData->convertAndValidateRaw(rawMetaData->max, false /* validate as well */, varMax, errorString)) {
            metaData->setRawMax(varMax);
        } else {
            qCDebug(APMParameterMetaDataLog) << "Invalid max value, name:" << metaData->name() << " type:"
                                             << metaData->type() << " max:" << rawMetaData->max
                                             << " error:" << errorString;
        }
    }

    if (rawMetaData->values.count() > 0) {
        QStringList     enumStrings;
        QVariantList    enumValues;

        for (int i=0; i<rawMetaData->values.count(); i++) {
            QVariant    enumValue;
            QString     errorString;
            QPair<QString, QString> enumPair = rawMetaData->values[i];

            if (metaData->convertAndValidateRaw(enumPair.first, false /* validate */, enumValue, errorString)) {
                enumValues << enumValue;
                enumStrings << enumPair.second;
            } else {
                qCDebug(APMParameterMetaDataLog) << "Invalid enum value, name:" << metaData->name()
                                                 << " type:" << metaData->type() << " value:" << enumPair.first
                                                 << " error:" << errorString;
                enumStrings.clear();
                enumValues.clear();
                break;
            }
        }

        if (enumStrings.count() != 0) {
            metaData->setEnumInfo(enumStrings, enumValues);
        }
    }

    if (rawMetaData->bitmask.count() > 0) {
        QStringList     bitmaskStrings;
        QVariantList    bitmaskValues;

        for (int i=0; i<rawMetaData->bitmask.count(); i++) {
            QVariant    bitmaskValue;
            QString     errorString;
            QPair<QString, QString> bitmaskPair = rawMetaData->bitmask[i];

            bool ok = false;
            unsigned int bitSet = bitmaskPair.first.toUInt(&ok);
            bitSet = 1 << bitSet;

            QVariant typedBitSet;

            switch (fact->type()) {
            case FactMetaData::valueTypeInt8:
                typedBitSet = QVariant((signed char)bitSet);
                break;

            case FactMetaData::valueTypeInt16:
                typedBitSet = QVariant((short int)bitSet);
                break;

            case FactMetaData::valueTypeInt32:
                typedBitSet = QVariant((int)bitSet);
                break;

            case FactMetaData::valueTypeUint8:
            case FactMetaData::valueTypeUint16:
            case FactMetaData::valueTypeUint32:
                typedBitSet = QVariant(bitSet);
                break;

            default:
                break;
            }
            if (typedBitSet.isNull()) {
                qCDebug(APMParameterMetaDataLog) << "Invalid type for bitmask, name:" << metaData->name()
                                                 << " type:" << metaData->type();
            }

            if (!ok) {
                qCDebug(APMParameterMetaDataLog) << "Invalid bitmask value, name:" << metaData->name()
                                                 << " type:" << metaData->type() << " value:" << bitSet
                                                 << " error: toUInt failed";
                bitmaskStrings.clear();
                bitmaskValues.clear();
                break;
            }

            if (metaData->convertAndValidateRaw(typedBitSet, false /* validate */, bitmaskValue, errorString)) {
                bitmaskValues << bitmaskValue;
                bitmaskStrings << bitmaskPair.second;
            } else {
                qCDebug(APMParameterMetaDataLog) << "Invalid bitmask value, name:" << metaData->name()
                                                 << " type:" << metaData->type() << " value:" << typedBitSet
                                                 << " error:" << errorString;
                bitmaskStrings.clear();
                bitmaskValues.clear();
                break;
            }
        }

        if (bitmaskStrings.count() != 0) {
            metaData->setBitmaskInfo(bitmaskStrings, bitmaskValues);
        }
    }

    if (!rawMetaData->incrementSize.isEmpty()) {
        double  increment;
        bool    ok;
        increment = rawMetaData->incrementSize.toDouble(&ok);
        if (ok) {
            metaData->setIncrement(increment);
        } else {
            qCDebug(APMParameterMetaDataLog) << "Invalid value for increment, name:" << metaData->name() << " increment:" << rawMetaData->incrementSize;
        }
    }

    // FixMe:: not handling increment size as their is no place for it in FactMetaData and no ui
    fact->setMetaData(metaData);
}

void APMParameterMetaData::getParameterMetaDataVersionInfo(const QString& metaDataFile, int& majorVersion, int& minorVersion)
{
    Q_UNUSED(metaDataFile);

    // Versioning not yet supported
    majorVersion = -1;
    minorVersion = -1;
}
