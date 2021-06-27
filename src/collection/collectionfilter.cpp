/*
 * Strawberry Music Player
 * Copyright 2021-2024, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <QSortFilterProxyModel>
#include <QVariant>
#include <QString>
#include <QStringList>

#include "core/logging.h"
#include "utilities/timeconstants.h"
#include "utilities/searchparserutils.h"

#include "collectionfilter.h"
#include "collectionmodel.h"
#include "collectionitem.h"

const QStringList CollectionFilter::Operators = QStringList() << QStringLiteral(":")
                                                              << QStringLiteral("=")
                                                              << QStringLiteral("==")
                                                              << QStringLiteral("<>")
                                                              << QStringLiteral("<")
                                                              << QStringLiteral("<=")
                                                              << QStringLiteral(">")
                                                              << QStringLiteral(">=");

CollectionFilter::CollectionFilter(QObject *parent) : QSortFilterProxyModel(parent) {}

bool CollectionFilter::filterAcceptsRow(const int source_row, const QModelIndex &source_parent) const {

  CollectionModel *model = qobject_cast<CollectionModel*>(sourceModel());
  if (!model) return false;
  QModelIndex idx = sourceModel()->index(source_row, 0, source_parent);
  if (!idx.isValid()) return false;
  CollectionItem *item = model->IndexToItem(idx);
  if (!item) return false;

  if (item->type == CollectionItem::Type_LoadingIndicator) return true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QString filter = filterRegularExpression().pattern().remove(QLatin1Char('\\'));
#else
  QString filter = filterRegExp().pattern();
#endif

  if (filter.isEmpty()) return true;

  filter = filter.replace(QRegularExpression(QStringLiteral("\\s*:\\s*")), QStringLiteral(":"))
                 .replace(QRegularExpression(QStringLiteral("\\s*=\\s*")), QStringLiteral("="))
                 .replace(QRegularExpression(QStringLiteral("\\s*==\\s*")), QStringLiteral("=="))
                 .replace(QRegularExpression(QStringLiteral("\\s*<>\\s*")), QStringLiteral("<>"))
                 .replace(QRegularExpression(QStringLiteral("\\s*<\\s*")), QStringLiteral("<"))
                 .replace(QRegularExpression(QStringLiteral("\\s*>\\s*")), QStringLiteral(">"))
                 .replace(QRegularExpression(QStringLiteral("\\s*<=\\s*")), QStringLiteral("<="))
                 .replace(QRegularExpression(QStringLiteral("\\s*>=\\s*")), QStringLiteral(">="));

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  const QStringList tokens = filter.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
#else
  const QStringList tokens = filter.split(QRegularExpression(QStringLiteral("\\s+")), QString::SkipEmptyParts);
#endif

  filter.clear();

  FilterDataList filterdata_list;
  static QRegularExpression operator_regex(QStringLiteral("(=|<[>=]?|>=?|!=)"));
  for (int i = 0; i < tokens.count(); ++i) {
    const QString &token = tokens[i];
    if (token.contains(QLatin1Char(':'))) {
      QString field = token.section(QLatin1Char(':'), 0, 0).remove(QLatin1Char(':')).trimmed();
      QString value = token.section(QLatin1Char(':'), 1, -1).remove(QLatin1Char(':')).trimmed();
      if (field.isEmpty() || value.isEmpty()) continue;
      if (Song::kTextSearchColumns.contains(field, Qt::CaseInsensitive) && value.count(QLatin1Char('"')) <= 2) {
        bool quotation_mark_start = false;
        bool quotation_mark_end = false;
        if (value.left(1) == QLatin1Char('"')) {
          value.remove(0, 1);
          quotation_mark_start = true;
          if (value.length() >= 1 && value.count(QLatin1Char('"')) == 1) {
            value = value.section(QLatin1Char(QLatin1Char('"')), 0, 0).remove(QLatin1Char('"')).trimmed();
            quotation_mark_end = true;
          }
        }
        for (int y = i + 1; y < tokens.count() && !quotation_mark_end; ++y) {
          QString next_value = tokens[y];
          if (!quotation_mark_start && ContainsOperators(next_value)) {
            break;
          }
          if (quotation_mark_start && next_value.contains(QLatin1Char('"'))) {
            next_value = next_value.section(QLatin1Char(QLatin1Char('"')), 0, 0).remove(QLatin1Char('"')).trimmed();
            quotation_mark_end = true;
          }
          value.append(QLatin1Char(' ') + next_value);
          i = y;
        }
        if (!field.isEmpty() && !value.isEmpty()) {
          filterdata_list.insert(field, FilterData(field, value));
        }
        continue;
      }
    }
    else if (token.contains(operator_regex)) {
      QRegularExpressionMatch re_match = operator_regex.match(token);
      if (re_match.hasMatch()) {
        const QString foperator = re_match.captured(0);
        const QString field = token.section(foperator, 0, 0).remove(foperator).trimmed();
        const QString value = token.section(foperator, 1, -1).remove(foperator).trimmed();
        if (value.isEmpty()) continue;
        if (Song::kNumericalSearchColumns.contains(field, Qt::CaseInsensitive)) {
          if (Song::kIntSearchColumns.contains(field, Qt::CaseInsensitive)) {
            bool ok = false;
            const int value_int = value.toInt(&ok);
            if (ok) {
              filterdata_list.insert(field, FilterData(field, value_int, foperator));
              continue;
            }
          }
          else if (Song::kUIntSearchColumns.contains(field, Qt::CaseInsensitive)) {
            bool ok = false;
            const uint value_uint = value.toUInt(&ok);
            if (ok) {
              filterdata_list.insert(field, FilterData(field, value_uint, foperator));
              continue;
            }
          }
          else if (field.compare(QStringLiteral("length"), Qt::CaseInsensitive) == 0) {
            filterdata_list.insert(field, FilterData(field, static_cast<qint64>(Utilities::ParseSearchTime(value)) * kNsecPerSec, foperator));
            continue;
          }
          else if (field.compare(QStringLiteral("rating"), Qt::CaseInsensitive) == 0) {
            filterdata_list.insert(field, FilterData(field, Utilities::ParseSearchRating(value), foperator));
          }
        }
      }
    }
    if (!filter.isEmpty()) filter.append(QLatin1Char(' '));
    filter += token;
  }

  if (ItemMatchesFilter(model, item, filterdata_list, filter)) return true;

  for (CollectionItem *parent = item ; parent && parent->type != CollectionItem::Type::Type_Root ; parent = parent->parent) {
    if (ItemMatchesFilter(model, parent, filterdata_list, filter)) return true;
  }

  return ChildrenMatches(model, item, filterdata_list, filter);

}

bool CollectionFilter::ItemMatchesFilter(CollectionModel *model, CollectionItem *item, const FilterDataList &filterdata_list, const QString &filter) {

  return (filter.isEmpty() || item->DisplayText().contains(filter, Qt::CaseInsensitive)) &&
         (
           filterdata_list.isEmpty() // If no filterdata_list were specified, only the filter needs to match.
           ||
           (item->type == CollectionItem::Type::Type_Song &&
            item->metadata.is_valid() &&
            ItemMetadataMatches(item->metadata, filterdata_list)) // Song node
           ||
           (item->type == CollectionItem::Type::Type_Container &&
            item->container_level >= 0 && item->container_level <= 2 &&
            ItemMetadataMatches(item->metadata, filterdata_list, FieldsFromGroupBy(model->GetGroupBy()[item->container_level]))) // Container node
          );

}

bool CollectionFilter::ChildrenMatches(CollectionModel *model, CollectionItem *item, const FilterDataList &filterdata_list, const QString &filter) {

  for (CollectionItem *child : item->children) {
    if (ItemMatchesFilter(model, child, filterdata_list, filter)) return true;
    if (ChildrenMatches(model, child, filterdata_list, filter)) return true;
  }

  return false;

}

bool CollectionFilter::ItemMetadataMatches(const Song &metadata, const FilterDataList &filterdata_list, const QStringList &fields) {

  for (FilterDataList::const_iterator it = filterdata_list.begin() ; it != filterdata_list.end() ; ++it) {
    const QString &field = it.key();
    const FilterData &filter_data = it.value();
    const QVariant &value = filter_data.value;
    const QString &foperator = filter_data.foperator;
    if (field.isEmpty() || !value.isValid()) {
      continue;
    }
    if (!fields.isEmpty() && !fields.contains(field)) {
      return false;
    }
    const QVariant data = DataFromField(field, metadata);
    if (
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        value.metaType() != data.metaType()
#else
        value.type() != data.type()
#endif
        || !FieldValueMatchesData(value, data, foperator)) {
      return false;
    }
  }

  return true;

}

QVariant CollectionFilter::DataFromField(const QString &field, const Song &metadata) {

  if (field == QStringLiteral("albumartist")) return metadata.effective_albumartist();
  if (field == QStringLiteral("artist"))      return metadata.artist();
  if (field == QStringLiteral("album"))       return metadata.album();
  if (field == QStringLiteral("title"))       return metadata.title();
  if (field == QStringLiteral("composer"))    return metadata.composer();
  if (field == QStringLiteral("performer"))   return metadata.performer();
  if (field == QStringLiteral("grouping"))    return metadata.grouping();
  if (field == QStringLiteral("genre"))       return metadata.genre();
  if (field == QStringLiteral("comment"))     return metadata.comment();
  if (field == QStringLiteral("track"))       return metadata.track();
  if (field == QStringLiteral("year"))        return metadata.year();
  if (field == QStringLiteral("length"))      return metadata.length_nanosec();
  if (field == QStringLiteral("samplerate"))  return metadata.samplerate();
  if (field == QStringLiteral("bitdepth"))    return metadata.bitdepth();
  if (field == QStringLiteral("bitrate"))     return metadata.bitrate();
  if (field == QStringLiteral("rating"))      return metadata.rating();
  if (field == QStringLiteral("playcount"))   return metadata.playcount();
  if (field == QStringLiteral("skipcount"))   return metadata.skipcount();

  return QVariant();

}

bool CollectionFilter::FieldValueMatchesData(const QVariant &value, const QVariant &data, const QString &foperator) {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  switch (value.metaType().id()) {
#else
  switch (value.userType()) {
#endif
    case QMetaType::QString:{
      const QString str_value = value.toString();
      const QString str_data = data.toString();
      return str_data.contains(str_value, Qt::CaseInsensitive);
    }
    case QMetaType::Int:{
      return FieldIntValueMatchesData(value.toInt(), foperator, data.toInt());
    }
    case QMetaType::UInt:{
      return FieldUIntValueMatchesData(value.toUInt(), foperator, data.toUInt());
    }
    case QMetaType::LongLong:{
      return FieldLongLongValueMatchesData(value.toLongLong(), foperator, data.toLongLong());
    }
    case QMetaType::Float:{
      return FieldFloatValueMatchesData(value.toFloat(), foperator, data.toFloat());
    }
    default:{
      return false;
    }
  }

  return false;

}

template<typename T>
bool CollectionFilter::FieldNumericalValueMatchesData(const T value, const QString &foperator, const T data) {

  if (foperator == QStringLiteral("=") || foperator == QStringLiteral("==")) {
    return data == value;
  }
  else if (foperator == QStringLiteral("!=") || foperator == QStringLiteral("<>")) {
    return data != value;
  }
  else if (foperator == QStringLiteral("<")) {
    return data < value;
  }
  else if (foperator == QStringLiteral(">")) {
    return data > value;
  }
  else if (foperator == QStringLiteral(">=")) {
    return data >= value;
  }
  else if (foperator == QStringLiteral("<=")) {
    return data <= value;
  }

  return false;

}

bool CollectionFilter::FieldIntValueMatchesData(const int value, const QString &foperator, const int data) {

  return FieldNumericalValueMatchesData(value, foperator, data);

}

bool CollectionFilter::FieldUIntValueMatchesData(const uint value, const QString &foperator, const uint data) {

  return FieldNumericalValueMatchesData(value, foperator, data);

}

bool CollectionFilter::FieldLongLongValueMatchesData(const qint64 value, const QString &foperator, const qint64 data) {

  return FieldNumericalValueMatchesData(value, foperator, data);

}

bool CollectionFilter::FieldFloatValueMatchesData(const float value, const QString &foperator, const float data) {

  return FieldNumericalValueMatchesData(value, foperator, data);

}

QStringList CollectionFilter::FieldsFromGroupBy(const CollectionModel::GroupBy group_by) {

  switch (group_by) {
    case CollectionModel::GroupBy::AlbumArtist:
      return QStringList() << QStringLiteral("albumartist");
    case CollectionModel::GroupBy::Artist:
      return QStringList() << QStringLiteral("artist");
    case CollectionModel::GroupBy::Album:
      return QStringList() << QStringLiteral("album");
    case CollectionModel::GroupBy::AlbumDisc:
      return QStringList() << QStringLiteral("album")
                           << QStringLiteral("disc");
    case CollectionModel::GroupBy::YearAlbum:
      return QStringList() << QStringLiteral("year")
                           << QStringLiteral("album");
    case CollectionModel::GroupBy::YearAlbumDisc:
      return QStringList() << QStringLiteral("year")
                           << QStringLiteral("album")
                           << QStringLiteral("disc");
    case CollectionModel::GroupBy::OriginalYearAlbum:
      return QStringList() << QStringLiteral("originalyear")
                           << QStringLiteral("album");
    case CollectionModel::GroupBy::OriginalYearAlbumDisc:
      return QStringList() << QStringLiteral("originalyear")
                           << QStringLiteral("album")
                           << QStringLiteral("disc");
      break;
    case CollectionModel::GroupBy::Disc:
      return QStringList() << QStringLiteral("disc");
    case CollectionModel::GroupBy::Year:
      return QStringList() << QStringLiteral("year");
    case CollectionModel::GroupBy::OriginalYear:
      return QStringList() << QStringLiteral("originalyear");
      break;
    case CollectionModel::GroupBy::Genre:
      return QStringList() << QStringLiteral("genre");
      break;
    case CollectionModel::GroupBy::Composer:
      return QStringList() << QStringLiteral("composer");
      break;
    case CollectionModel::GroupBy::Performer:
      return QStringList() << QStringLiteral("performer");
      break;
    case CollectionModel::GroupBy::Grouping:
      return QStringList() << QStringLiteral("grouping");
      break;
    case CollectionModel::GroupBy::FileType:
      return QStringList() << QStringLiteral("filetype");
      break;
    case CollectionModel::GroupBy::Format:
      return QStringList() << QStringLiteral("format");
      break;
    case CollectionModel::GroupBy::Bitdepth:
      return QStringList() << QStringLiteral("bitdepth");
      break;
    case CollectionModel::GroupBy::Samplerate:
      return QStringList() << QStringLiteral("samplerate");
      break;
    case CollectionModel::GroupBy::Bitrate:
      return QStringList() << QStringLiteral("bitrate");
      break;
    case CollectionModel::GroupBy::None:
    case CollectionModel::GroupBy::GroupByCount:
      return QStringList();
      break;
  }

  return QStringList();

}

bool CollectionFilter::ContainsOperators(const QString &token) {

  for (const QString &foperator : Operators) {
    if (token.contains(foperator, Qt::CaseInsensitive)) return true;
  }

  return false;

}
