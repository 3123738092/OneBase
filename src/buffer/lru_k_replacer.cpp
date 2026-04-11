#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/common/exception.h"
#include <limits>
#include <stdexcept>

namespace onebase {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock lock(latch_);

  bool found_inf = false;
  frame_id_t inf_victim = INVALID_FRAME_ID;
  size_t inf_first_access = std::numeric_limits<size_t>::max();

  bool found_finite = false;
  frame_id_t finite_victim = INVALID_FRAME_ID;
  size_t max_k_distance = 0;

  for (const auto &[fid, entry] : entries_) {
    if (!entry.is_evictable_) {
      continue;
    }
    if (entry.history_.empty()) {
      continue;
    }

    if (entry.history_.size() < k_) {
      const size_t first_access = entry.history_.front();
      if (!found_inf || first_access < inf_first_access) {
        found_inf = true;
        inf_victim = fid;
        inf_first_access = first_access;
      }
      continue;
    }

    const size_t k_distance = current_timestamp_ - entry.history_.front();
    if (!found_finite || k_distance > max_k_distance) {
      found_finite = true;
      finite_victim = fid;
      max_k_distance = k_distance;
    }
  }

  if (!found_inf && !found_finite) {
    return false;
  }

  const frame_id_t victim = found_inf ? inf_victim : finite_victim;
  entries_.erase(victim);
  curr_size_--;
  *frame_id = victim;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  if (frame_id < 0 || static_cast<size_t>(frame_id) >= max_frames_) {
    throw std::out_of_range("frame_id out of range");
  }

  std::scoped_lock lock(latch_);
  auto &entry = entries_[frame_id];
  entry.history_.push_back(current_timestamp_);
  if (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }
  current_timestamp_++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock lock(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  if (!it->second.is_evictable_ && set_evictable) {
    curr_size_++;
  } else if (it->second.is_evictable_ && !set_evictable) {
    curr_size_--;
  }

  it->second.is_evictable_ = set_evictable;
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock lock(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }
  if (!it->second.is_evictable_) {
    throw std::runtime_error("Cannot remove a non-evictable frame");
  }
  entries_.erase(it);
  curr_size_--;
}

auto LRUKReplacer::Size() const -> size_t {
  return curr_size_;
}

}  // namespace onebase
