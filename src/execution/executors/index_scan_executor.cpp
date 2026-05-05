#include "onebase/execution/executors/index_scan_executor.h"

#include <vector>

#include "onebase/catalog/catalog.h"
#include "onebase/common/exception.h"
#include "onebase/type/type_id.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  cursor_ = 0;
  matching_rids_.clear();
  auto *catalog = GetExecutorContext()->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());
  index_info_ = catalog->GetIndex(plan_->GetIndexOid());
  if (table_info_ == nullptr || index_info_ == nullptr) {
    throw NotImplementedException("IndexScanExecutor::Init missing table or index");
  }

  if (!index_info_->SupportsPointLookup()) {
    throw NotImplementedException("IndexScanExecutor::Init unsupported index type");
  }

  const auto &schema = table_info_->schema_;
  Value key_val = plan_->GetLookupKey()->Evaluate(nullptr, &schema);
  if (key_val.GetTypeId() != TypeId::INTEGER) {
    throw NotImplementedException("IndexScanExecutor::Init non-integer lookup key");
  }

  if (const auto *rids = index_info_->LookupInteger(key_val.GetAsInteger())) {
    matching_rids_ = *rids;
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  const auto &schema = table_info_->schema_;

  while (cursor_ < matching_rids_.size()) {
    RID cur_rid = matching_rids_[cursor_++];

    Tuple cur = table_info_->table_->GetTuple(cur_rid);

    if (plan_->GetPredicate() != nullptr) {
      auto pred = plan_->GetPredicate()->Evaluate(&cur, &schema);
      if (!pred.GetAsBoolean()) {
        continue;
      }
    }

    std::vector<Value> vals;
    vals.reserve(schema.GetColumnCount());
    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
      vals.push_back(cur.GetValue(&schema, i));
    }
    *tuple = Tuple(std::move(vals));
    *rid = cur_rid;
    return true;
  }
  return false;
}

}  // namespace onebase
