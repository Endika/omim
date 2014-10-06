#include "active_maps_layout.hpp"

#include "framework.hpp"

using namespace storage;

ActiveMapsLayout::ActiveMapsLayout(Framework * framework)
  : m_framework(framework)
{
  m_subscribeSlotID = m_framework->Storage().Subscribe(bind(&ActiveMapsLayout::StatusChangedCallback, this, _1),
                                                       bind(&ActiveMapsLayout::ProgressChangedCallback, this, _1, _2));
}

ActiveMapsLayout::~ActiveMapsLayout()
{
  m_framework->Storage().Unsubscribe(m_subscribeSlotID);
}

void ActiveMapsLayout::Init()
{
  static bool s_inited = false;
  if (!s_inited)
  {
    Storage & storage = m_framework->Storage();
    auto insertIndexFn = [&](TIndex index)
    {
      TStatus status;
      TMapOptions options;
      storage.CountryStatusEx(index, status, options);
      ASSERT(status == TStatus::EOnDisk || status == TStatus::EOnDiskOutOfDate, ());
      m_items.push_back(Item{index, status, options, options});
    };

    TIndex root;
    size_t groupCount = storage.CountriesCount(root);
    for (size_t groupIt = 0; groupIt < groupCount; ++groupIt)
    {
      TIndex group(groupIt);
      size_t countryCount = storage.CountriesCount(group);
      for (size_t cntIt = 0; cntIt < countryCount; ++cntIt)
      {
        TIndex country(groupIt, cntIt);
        size_t regionCount = storage.CountriesCount(country);
        if (regionCount != 0)
        {
          for (size_t regionIt = 0; regionIt < regionCount; ++regionIt)
            insertIndexFn(TIndex(groupIt, cntIt, regionIt));
        }
        else
          insertIndexFn(country);
      }
    }

    auto comparatorFn = [&](Item const & lhs, Item const & rhs)
    {
      int lStatus = static_cast<int>(lhs.m_status);
      int rStatus = static_cast<int>(rhs.m_status);
      if (lStatus != rStatus)
        return lStatus > rStatus;
      else
      {
        return storage.CountryName(lhs.m_index) < storage.CountryName(rhs.m_index);
      }
    };

    sort(m_items.begin(), m_items.end(), comparatorFn);

    typedef vector<Item>::const_iterator TItemIter;
    auto iter = lower_bound(m_items.begin(), m_items.end(), TStatus::EOnDisk, [](Item const & lhs, TStatus const & status)
    {
      return lhs.m_status < status;
    });
    m_split = make_pair(0, distance(m_items.begin(), iter));
  }
}

void ActiveMapsLayout::UpdateAll()
{
  vector<Item> toDownload;
  toDownload.reserve(m_items.size());
  for (int i = m_split.first; i < m_split.second; ++i)
  {
    Item const & item = m_items[i];
    if (item.m_status != TStatus::EInQueue && item.m_status != TStatus::EDownloading)
      toDownload.push_back(item);
  }

  for (Item const & item : toDownload)
    DownloadMap(item.m_index, item.m_options);
}

void ActiveMapsLayout::CancelAll()
{
  vector<TIndex> downloading;
  downloading.reserve(m_items.size());
  for (Item const & item : m_items)
  {
    if (item.m_status == TStatus::EInQueue || item.m_status == TStatus::EDownloading)
      downloading.push_back(item.m_index);
  }

  Storage & st = m_framework->Storage();
  for (TIndex const & index : downloading)
    st.DeleteFromDownloader(index);
}

int ActiveMapsLayout::GetCountInGroup(TGroup const & group) const
{
  int result = 0;
  switch (group) {
  case TGroup::ENewMap:
    result = m_split.first;
    break;
  case TGroup::EOutOfDate:
    result = m_split.second - m_split.first;
    break;
  case TGroup::EUpToDate:
    result = m_items.size() - m_split.second;
    break;
  }

  return result;
}

string const & ActiveMapsLayout::GetCountryName(TGroup const & group, int position) const
{
  return m_framework->GetCountryName(GetItemInGroup(group, position).m_index);
}

TStatus ActiveMapsLayout::GetCountryStatus(const ActiveMapsLayout::TGroup & group, int position) const
{
  return GetItemInGroup(group, position).m_status;
}

TMapOptions ActiveMapsLayout::GetCountryOptions(const ActiveMapsLayout::TGroup & group, int position) const
{
  return GetItemInGroup(group, position).m_options;
}

void ActiveMapsLayout::SetListener(ActiveMapsLayout::ActiveMapsListener * listener)
{
  m_listener = listener;
}

void ActiveMapsLayout::DownloadMap(TIndex const & index, TMapOptions const & options)
{
  TMapOptions validOptions = ValidOptionsForDownload(options);
  Item * item = nullptr;
  if (IsExist(index, &item))
  {
    ASSERT(item != nullptr, ());
    item->m_downloadRequest = validOptions;
  }
  else
  {
    int position = InsertInGroup(TGroup::ENewMap, Item{ index, TStatus::ENotDownloaded, validOptions, validOptions });
    NotifyInsertion(TGroup::ENewMap, position);
  }

  m_framework->DownloadCountry(index, validOptions);
}

void ActiveMapsLayout::DownloadMap(TGroup const & group, int position, TMapOptions const & options)
{
  TMapOptions validOptions = ValidOptionsForDownload(options);
  Item & item = GetItemInGroup(group, position);
  item.m_downloadRequest = validOptions;
  m_framework->DownloadCountry(item.m_index, item.m_downloadRequest);
}

void ActiveMapsLayout::DeleteMap(TIndex const & index, const TMapOptions & options)
{
  TGroup group;
  int position;
  VERIFY(GetGroupAndPositionByIndex(index, group, position), ());
  DeleteMap(group, position, options);
}

void ActiveMapsLayout::DeleteMap(TGroup const & group, int position, TMapOptions const & options)
{
  m_framework->DeleteCountry(GetItemInGroup(group, position).m_index, ValidOptionsForDelete(options));
}

void ActiveMapsLayout::RetryDownloading(TGroup const & group, int position)
{
  Item const & item = GetItemInGroup(group, position);
  ASSERT(item.m_options != item.m_downloadRequest, ());
  m_framework->DownloadCountry(item.m_index, item.m_downloadRequest);
}

bool ActiveMapsLayout::IsDownloadingActive() const
{
  auto iter = find_if(m_items.begin(), m_items.end(), [](Item const & item)
  {
    return item.m_status == TStatus::EDownloading || item.m_status == TStatus::EInQueue;
  });

  return iter != m_items.end();
}

void ActiveMapsLayout::CancelDownloading(TGroup const & group, int position)
{
  Item & item = GetItemInGroup(group, position);
  m_framework->Storage().DeleteFromDownloader(item.m_index);
  item.m_downloadRequest = item.m_options;
}

void ActiveMapsLayout::StatusChangedCallback(TIndex const & index)
{
  TStatus newStatus = TStatus::EUnknown;
  TMapOptions options = TMapOptions::EMapOnly;
  m_framework->Storage().CountryStatusEx(index, newStatus, options);

  TGroup group = TGroup::ENewMap;
  int position = 0;
  VERIFY(GetGroupAndPositionByIndex(index, group, position), ());
  Item & item = GetItemInGroup(group, position);

  if (newStatus == TStatus::EOnDisk)
  {
    if (group != TGroup::EUpToDate)
    {
      /// Here we handle
      /// "NewMap" -> "Actual Map without routing"
      /// "NewMap" -> "Actual Map with routing"
      /// "OutOfDate without routing" -> "Actual map without routing"
      /// "OutOfDate without routing" -> "Actual map with routing"
      /// "OutOfDate with Routing" -> "Actual map with routing"
      ///   For "NewMaps" always true - m_options == m_m_downloadRequest == options
      ///   but we must notify that options changed because for "NewMaps" m_options is virtual state
      if (item.m_options != options || group == TGroup::ENewMap)
      {
        item.m_downloadRequest = item.m_options = options;
        NotifyOptionsChanged(group, position);
      }

      ASSERT(item.m_status != newStatus, ());
      item.m_status = newStatus;
      NotifyStatusChanged(group, position);

      int newPosition = MoveItemToGroup(group, position, TGroup::EUpToDate);
      NotifyMove(group, position, TGroup::EUpToDate, newPosition);
    }
    else
    {
      /// Here we handle
      /// "Actual map without routing" -> "Actual map with routing"
      /// "Actual map with routing" -> "Actual map without routing"
      ASSERT(item.m_status == newStatus, ());
      ASSERT(item.m_options != options, ());
      item.m_options = item.m_downloadRequest = options;
      NotifyOptionsChanged(group, position);
    }
  }
  else if (newStatus == TStatus::ENotDownloaded)
  {
    if (group == TGroup::ENewMap)
    {
      /// "NewMap downloading" -> "Cancel downloading"
      /// We handle here only status change for "New maps"
      /// because if new status ENotDownloaded than item.m_options is invalid.
      /// Map have no options and gui not show routing icon
      item.m_status = newStatus;
      NotifyStatusChanged(group, position);
    }
    else
    {
      /// "Actual of not map been deleted"
      /// We not notify about options changed!
      DeleteFromGroup(group, position);
      NotifyDeletion(group, position);
    }
  }
  else if (newStatus == TStatus::EOnDiskOutOfDate)
  {
    /// We can drop here only if user start update some map and cancel it
    item.m_status = newStatus;
    NotifyStatusChanged(group, position);

    ASSERT(item.m_options == options, ());
    item.m_downloadRequest = item.m_options = options;
  }
  else
  {
    /// EDownloading
    /// EInQueue
    /// downloadig faild for some reason
    item.m_status = newStatus;
    NotifyStatusChanged(group, position);
  }
}

void ActiveMapsLayout::ProgressChangedCallback(TIndex const & index, LocalAndRemoteSizeT const & sizes)
{
  if (m_listener)
  {
    TGroup group = TGroup::ENewMap;
    int position = 0;
    VERIFY(GetGroupAndPositionByIndex(index, group, position), ());
    m_listener->DownloadingProgressUpdate(group, position, sizes);
  }
}

ActiveMapsLayout::Item const & ActiveMapsLayout::GetItemInGroup(TGroup const & group, int position) const
{
  int index = GetStartIndexInGroup(group) + position;
  ASSERT(index < m_items.size(), ());
  return m_items[index];
}

ActiveMapsLayout::Item & ActiveMapsLayout::GetItemInGroup(TGroup const & group, int position)
{
  int index = GetStartIndexInGroup(group) + position;
  ASSERT(index < m_items.size(), ());
  return m_items[index];
}

int ActiveMapsLayout::GetStartIndexInGroup(TGroup const & group) const
{
  switch (group)
  {
    case TGroup::ENewMap: return 0;
    case TGroup::EOutOfDate: return m_split.first;
    case TGroup::EUpToDate: return m_split.second;
    default:
      ASSERT(false, ());
  }

  return m_items.size();
}

bool ActiveMapsLayout::IsExist(TIndex const & index, Item ** item)
{
  vector<Item>::iterator iter = find_if(m_items.begin(), m_items.end(), [&index](Item const & item)
  {
    return item.m_index == index;
  });

  bool exist = iter != m_items.end();
  if (exist)
    *item = &(*iter);

  return exist;
}

bool ActiveMapsLayout::GetGroupAndPositionByIndex(TIndex const & index, TGroup & group, int & position)
{
  auto it = find_if(m_items.begin(), m_items.end(), [&index] (Item const & item)
  {
    return item.m_index == index;
  });

  if (it == m_items.end())
    return false;

  int vectorIndex = distance(m_items.begin(), it);
  if (vectorIndex >= m_split.second)
  {
    group = TGroup::EUpToDate;
    position = vectorIndex - m_split.second;
  }
  else if (vectorIndex >= m_split.first)
  {
    group = TGroup::EOutOfDate;
    position = vectorIndex - m_split.first;
  }
  else
  {
    group = TGroup::ENewMap;
    position = vectorIndex;
  }

  return true;
}

int ActiveMapsLayout::InsertInGroup(TGroup const & group, Item const & item)
{
  typedef vector<Item>::iterator TItemIter;
  TItemIter startSort;
  TItemIter endSort;

  if (group == TGroup::ENewMap)
  {
    m_items.insert(m_items.begin(), item);
    ++m_split.first;
    ++m_split.second;
    startSort = m_items.begin();
    endSort = m_items.begin() + m_split.first;
  }
  else if (group == TGroup::EOutOfDate)
  {
    m_items.insert(m_items.begin() + m_split.first, item);
    ++m_split.second;
    startSort = m_items.begin() + m_split.first;
    endSort = m_items.begin() + m_split.second;
  }
  else
  {
    m_items.insert(m_items.begin() + m_split.second, item);
    startSort = m_items.begin() + m_split.second;
    endSort = m_items.end();
  }

  Storage & st = m_framework->Storage();
  sort(startSort, endSort, [&](Item const & lhs, Item const & rhs)
  {
    ASSERT(lhs.m_status == rhs.m_status, ());
    return st.CountryName(lhs.m_index) < st.CountryName(rhs.m_index);
  });

  TItemIter newPosIter = m_items.end();
  for (TItemIter it = startSort; it != endSort; ++it)
  {
    if (it->m_index == item.m_index)
    {
      newPosIter = it;
      break;
    }
  }

  ASSERT(newPosIter != m_items.end(), ());
  return distance(startSort, newPosIter);
}

void ActiveMapsLayout::DeleteFromGroup(TGroup const & group, int position)
{
  if (group == TGroup::ENewMap)
  {
    --m_split.first;
    --m_split.second;
  }
  else if (group == TGroup::EOutOfDate)
  {
    --m_split.second;
  }

  m_items.erase(m_items.begin() + GetStartIndexInGroup(group) + position);
}

int ActiveMapsLayout::MoveItemToGroup(TGroup const & group, int position, TGroup const & newGroup)
{
  Item item = GetItemInGroup(group, position);
  DeleteFromGroup(group, position);
  return InsertInGroup(newGroup, item);
}

void ActiveMapsLayout::NotifyInsertion(TGroup const & group, int position)
{
  if (m_listener)
    m_listener->CountryGroupChanged(group, -1, group, position);
}

void ActiveMapsLayout::NotifyDeletion(TGroup const & group, int position)
{
  if (m_listener)
    m_listener->CountryGroupChanged(group, position, group, position);
}

void ActiveMapsLayout::NotifyMove(TGroup const & oldGroup, int oldPosition,
                                  TGroup const & newGroup, int newPosition)
{
  if (m_listener)
    m_listener->CountryGroupChanged(oldGroup, oldPosition, newGroup, newPosition);
}

void ActiveMapsLayout::NotifyStatusChanged(TGroup const & group, int position)
{
  if (m_listener)
    m_listener->CountryStatusChanged(group, position);
}

void ActiveMapsLayout::NotifyOptionsChanged(TGroup const & group, int position)
{
  if (m_listener)
    m_listener->CountryOptionsChanged(group, position);
}

TMapOptions ActiveMapsLayout::ValidOptionsForDownload(TMapOptions const & options)
{
  return options | TMapOptions::EMapOnly;
}

TMapOptions ActiveMapsLayout::ValidOptionsForDelete(TMapOptions const & options)
{
  if (options & TMapOptions::EMapOnly)
    return options | TMapOptions::ECarRouting;
  return options;
}