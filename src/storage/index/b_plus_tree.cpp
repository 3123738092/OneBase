#include "onebase/storage/index/b_plus_tree.h"
#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include <utility>
#include <vector>
#include "onebase/common/exception.h"
#include "onebase/storage/page/b_plus_tree_page.h"

namespace onebase {

template class BPlusTree<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
                           int leaf_max_size, int internal_max_size)
    : Index(std::move(name)), bpm_(bpm), comparator_(comparator),
      leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {
  if (leaf_max_size_ == 0) {
    leaf_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t)) /
        (sizeof(KeyType) + sizeof(ValueType)));
  }
  if (internal_max_size_ == 0) {
    internal_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage)) /
        (sizeof(KeyType) + sizeof(page_id_t)));
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  return root_page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  if (IsEmpty()) {
    page_id_t root_pid;
    auto *root_page = bpm_->NewPage(&root_pid);
    if (root_page == nullptr) {
      return false;
    }
    auto *root_leaf = reinterpret_cast<LeafPage *>(root_page->GetData());
    root_leaf->Init(leaf_max_size_);
    root_leaf->SetParentPageId(INVALID_PAGE_ID);
    root_leaf->Insert(key, value, comparator_);
    root_page_id_ = root_pid;
    bpm_->UnpinPage(root_pid, true);
    return true;
  }

  std::vector<page_id_t> path;
  page_id_t curr_pid = root_page_id_;
  auto *curr_page = bpm_->FetchPage(curr_pid);
  while (!reinterpret_cast<BPlusTreePage *>(curr_page->GetData())->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage *>(curr_page->GetData());
    path.push_back(curr_pid);
    page_id_t next_pid = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(curr_pid, false);
    curr_pid = next_pid;
    curr_page = bpm_->FetchPage(curr_pid);
  }

  auto *leaf = reinterpret_cast<LeafPage *>(curr_page->GetData());
  ValueType existing{};
  if (leaf->Lookup(key, &existing, comparator_)) {
    bpm_->UnpinPage(curr_pid, false);
    return false;
  }

  leaf->Insert(key, value, comparator_);
  if (leaf->GetSize() <= leaf->GetMaxSize()) {
    bpm_->UnpinPage(curr_pid, true);
    return true;
  }

  page_id_t new_leaf_pid;
  auto *new_leaf_page = bpm_->NewPage(&new_leaf_pid);
  if (new_leaf_page == nullptr) {
    bpm_->UnpinPage(curr_pid, true);
    return false;
  }
  auto *new_leaf = reinterpret_cast<LeafPage *>(new_leaf_page->GetData());
  new_leaf->Init(leaf_max_size_);
  new_leaf->SetParentPageId(leaf->GetParentPageId());
  new_leaf->SetNextPageId(leaf->GetNextPageId());
  leaf->MoveHalfTo(new_leaf);
  leaf->SetNextPageId(new_leaf_pid);

  KeyType up_key = new_leaf->KeyAt(0);
  page_id_t left_pid = curr_pid;
  page_id_t right_pid = new_leaf_pid;

  bpm_->UnpinPage(curr_pid, true);
  bpm_->UnpinPage(new_leaf_pid, true);

  while (true) {
    if (path.empty()) {
      page_id_t new_root_pid;
      auto *new_root_page = bpm_->NewPage(&new_root_pid);
      auto *new_root = reinterpret_cast<InternalPage *>(new_root_page->GetData());
      new_root->Init(internal_max_size_);
      new_root->SetParentPageId(INVALID_PAGE_ID);
      new_root->PopulateNewRoot(left_pid, up_key, right_pid);
      root_page_id_ = new_root_pid;

      auto *left_page = bpm_->FetchPage(left_pid);
      auto *left_node = reinterpret_cast<BPlusTreePage *>(left_page->GetData());
      left_node->SetParentPageId(new_root_pid);
      bpm_->UnpinPage(left_pid, true);

      auto *right_page = bpm_->FetchPage(right_pid);
      auto *right_node = reinterpret_cast<BPlusTreePage *>(right_page->GetData());
      right_node->SetParentPageId(new_root_pid);
      bpm_->UnpinPage(right_pid, true);

      bpm_->UnpinPage(new_root_pid, true);
      return true;
    }

    page_id_t parent_pid = path.back();
    path.pop_back();
    auto *parent_page = bpm_->FetchPage(parent_pid);
    auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
    parent->InsertNodeAfter(left_pid, up_key, right_pid);

    auto *right_page = bpm_->FetchPage(right_pid);
    auto *right_node = reinterpret_cast<BPlusTreePage *>(right_page->GetData());
    right_node->SetParentPageId(parent_pid);
    bpm_->UnpinPage(right_pid, true);

    if (parent->GetSize() <= parent->GetMaxSize()) {
      bpm_->UnpinPage(parent_pid, true);
      return true;
    }

    page_id_t new_internal_pid;
    auto *new_internal_page = bpm_->NewPage(&new_internal_pid);
    auto *new_internal = reinterpret_cast<InternalPage *>(new_internal_page->GetData());
    new_internal->Init(internal_max_size_);
    new_internal->SetParentPageId(parent->GetParentPageId());

    int old_size = parent->GetSize();
    int split = old_size / 2;
    KeyType promote = parent->KeyAt(split);
    int move_count = old_size - split;
    for (int i = 0; i < move_count; i++) {
      new_internal->SetKeyAt(i, parent->KeyAt(split + i));
      new_internal->SetValueAt(i, parent->ValueAt(split + i));
    }
    new_internal->SetKeyAt(0, promote);
    new_internal->SetSize(move_count);
    parent->SetSize(split);

    for (int i = 0; i < new_internal->GetSize(); i++) {
      page_id_t child_pid = new_internal->ValueAt(i);
      auto *child_page = bpm_->FetchPage(child_pid);
      auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
      child->SetParentPageId(new_internal_pid);
      bpm_->UnpinPage(child_pid, true);
    }

    bpm_->UnpinPage(parent_pid, true);
    bpm_->UnpinPage(new_internal_pid, true);

    up_key = promote;
    left_pid = parent_pid;
    right_pid = new_internal_pid;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  if (IsEmpty()) {
    return;
  }

  auto key_equal = [this](const KeyType &a, const KeyType &b) {
    return !comparator_(a, b) && !comparator_(b, a);
  };

  std::vector<std::pair<KeyType, ValueType>> entries;
  for (auto it = Begin(); it != End(); ++it) {
    entries.push_back(*it);
  }

  bool removed = false;
  std::vector<std::pair<KeyType, ValueType>> remaining;
  remaining.reserve(entries.size());
  for (const auto &kv : entries) {
    if (!removed && key_equal(kv.first, key)) {
      removed = true;
      continue;
    }
    remaining.push_back(kv);
  }
  if (!removed) {
    return;
  }

  root_page_id_ = INVALID_PAGE_ID;
  for (const auto &kv : remaining) {
    Insert(kv.first, kv.second);
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  result->clear();
  if (IsEmpty()) {
    return false;
  }

  page_id_t curr_pid = root_page_id_;
  auto *curr_page = bpm_->FetchPage(curr_pid);
  while (!reinterpret_cast<BPlusTreePage *>(curr_page->GetData())->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage *>(curr_page->GetData());
    page_id_t next_pid = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(curr_pid, false);
    curr_pid = next_pid;
    curr_page = bpm_->FetchPage(curr_pid);
  }

  auto *leaf = reinterpret_cast<LeafPage *>(curr_page->GetData());
  ValueType value{};
  bool found = leaf->Lookup(key, &value, comparator_);
  if (found) {
    result->push_back(value);
  }
  bpm_->UnpinPage(curr_pid, false);
  return found;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin() -> Iterator {
  if (IsEmpty()) {
    return End();
  }

  page_id_t curr_pid = root_page_id_;
  auto *curr_page = bpm_->FetchPage(curr_pid);
  while (!reinterpret_cast<BPlusTreePage *>(curr_page->GetData())->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage *>(curr_page->GetData());
    page_id_t next_pid = internal->ValueAt(0);
    bpm_->UnpinPage(curr_pid, false);
    curr_pid = next_pid;
    curr_page = bpm_->FetchPage(curr_pid);
  }

  auto *leaf = reinterpret_cast<LeafPage *>(curr_page->GetData());
  if (leaf->GetSize() == 0) {
    page_id_t next = leaf->GetNextPageId();
    bpm_->UnpinPage(curr_pid, false);
    return Iterator(next, 0, bpm_);
  }
  bpm_->UnpinPage(curr_pid, false);
  return Iterator(curr_pid, 0, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> Iterator {
  if (IsEmpty()) {
    return End();
  }

  page_id_t curr_pid = root_page_id_;
  auto *curr_page = bpm_->FetchPage(curr_pid);
  while (!reinterpret_cast<BPlusTreePage *>(curr_page->GetData())->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage *>(curr_page->GetData());
    page_id_t next_pid = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(curr_pid, false);
    curr_pid = next_pid;
    curr_page = bpm_->FetchPage(curr_pid);
  }

  auto *leaf = reinterpret_cast<LeafPage *>(curr_page->GetData());
  int idx = leaf->KeyIndex(key, comparator_);
  if (idx >= leaf->GetSize()) {
    page_id_t next = leaf->GetNextPageId();
    bpm_->UnpinPage(curr_pid, false);
    return Iterator(next, 0, bpm_);
  }
  bpm_->UnpinPage(curr_pid, false);
  return Iterator(curr_pid, idx, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0, bpm_);
}

}  // namespace onebase
