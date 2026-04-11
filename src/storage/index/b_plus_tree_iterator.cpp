#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include <stdexcept>
#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

template class BPlusTreeIterator<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_ITERATOR_TYPE::BPlusTreeIterator(page_id_t page_id, int index, BufferPoolManager *bpm)
    : page_id_(page_id), index_(index), bpm_(bpm) {}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::IsEnd() const -> bool {
  return page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator*() -> const std::pair<KeyType, ValueType> & {
  if (IsEnd() || bpm_ == nullptr) {
    throw std::out_of_range("Dereference end iterator");
  }
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  page_id_t old_pid = page_id_;
  auto *page = bpm_->FetchPage(old_pid);
  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
  current_.first = leaf->KeyAt(index_);
  current_.second = leaf->ValueAt(index_);
  bpm_->UnpinPage(old_pid, false);
  return current_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator++() -> BPlusTreeIterator & {
  if (IsEnd() || bpm_ == nullptr) {
    return *this;
  }

  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  page_id_t old_pid = page_id_;
  auto *page = bpm_->FetchPage(old_pid);
  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());

  index_++;
  if (index_ >= leaf->GetSize()) {
    page_id_ = leaf->GetNextPageId();
    index_ = 0;
  }
  bpm_->UnpinPage(old_pid, false);
  return *this;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator==(const BPlusTreeIterator &other) const -> bool {
  return page_id_ == other.page_id_ && index_ == other.index_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator!=(const BPlusTreeIterator &other) const -> bool {
  return !(*this == other);
}

}  // namespace onebase
